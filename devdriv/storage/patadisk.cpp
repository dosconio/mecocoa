// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: Disk - PATA
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

// unchk: IDE1:0, IDE1:1

#include "../../include/mecocoa.hpp"
#include <c/storage/harddisk.h>
#include <cpp/atomic>
#include <cpp/Device/Bus/PCI.hpp>
#if _MCCA == 0x8664
#include "../../mecocoa/devsman-acpi-gsi.h"
#endif

_ESYM_C void R_HDD_INIT();

#if _MCCA == 0x8632 || _MCCA == 0x8664

#if 1
__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_HDD{
	.init = R_HDD_INIT,
	.name = "DISK-PATA",
};
#endif

Harddisk_PATA* disks[MAX_DRIVES];// referenced
static DeviceNode* pata_storage_nodes[MAX_DRIVES] = {};
static char hdd_buf[byteof(**disks) * numsof(disks)];
static constexpr byte PATA_PRIMARY_NATIVE_MODE = 0x01u;
static constexpr byte PATA_SECONDARY_NATIVE_MODE = 0x04u;
static constexpr byte PATA_NATIVE_MODE_MASK = PATA_PRIMARY_NATIVE_MODE | PATA_SECONDARY_NATIVE_MODE;
static DeviceNode* pata_controller_node = nullptr;
#if _MCCA == 0x8664
enum class PataPIOPhase : byte {
	DataIn,
	// Initial DRQ for a write requests data; it does not complete the command.
	WriteCommand,
	WriteData,
	Dma,
};
#endif
struct IdeChannelState {
	// Master/slave register sequences share a channel; a spinlock would mask the completion IRQ while waiting.
	Mutex lock;
	// The GSI handler can run on the BSP while another CPU owns the channel transaction.
	uni::Atomic<byte> irq_waiting = 0;
	uni::Atomic<byte> irq_seen = 0;
	#if _MCCA == 0x8664
	ACPIGSIBinding gsi_binding{};
	uni::Atomic<PataPIOPhase> pio_phase = PataPIOPhase::DataIn;
	bool irq_fallback_reported = false;
	#endif
};
static IdeChannelState g_ide_channels[2];
static char* single_sector = NULL;// file-hd used buffer

struct PataDmaChannelState {
	word bmide_base = 0;
	PataBmidePrd* prdt = nullptr;
	byte* bounce = nullptr;
	uint32 prdt_address = 0;
	uint32 bounce_address = 0;
	// Serialize BSP GSI acknowledgement against another CPU retiring or rearming the DMA request.
	Spinlock irq_lock;
	uni::Atomic<byte> irq_status = 0;
	bool quiesced = true;
	bool ready = false;
};

static PataDmaChannelState pata_dma_channels[2];
// A drive stays on PIO until its DMA read matches the PIO reference sector.
static bool pata_dma_capable[MAX_DRIVES] = {};
static bool pata_dma_enabled[MAX_DRIVES] = {};
static constexpr stduint PataDmaPageSize = 0x1000;
// Intel 82801DB ICH4 datasheet, section 10.2.2: BMIS bit 0 reports engine activity.
static constexpr byte PataBmideStatusActive = 0x01;

static void free_pata_dma_page(void* buffer) {
	if (!buffer) return;
	if (dma_low_pool.IsInRange(buffer)) dma_low_pool.Deallocate(buffer, PataDmaPageSize);
	else mempool.deallocate(buffer, PataDmaPageSize);
}

static void* allocate_pata_dma_page(DeviceNode* node, uint32& device_address) {
	void* buffer = dma_low_pool.Allocate(PataDmaPageSize);
	if (!buffer) buffer = mempool.allocate(PataDmaPageSize, 12);
	if (!buffer) return nullptr;
	const uint64 physical = uint64(_IMM(buffer));
	// ICH4 sections 5.15.2.1/10.2.3: 32-bit DMA addresses, aligned PRDT, no 64KB crossing.
	if ((physical & (PataDmaPageSize - 1)) || physical > 0x100000000ull - PataDmaPageSize) {
		free_pata_dma_page(buffer);
		return nullptr;
	}
	Iova::DmaMapping mapping{};
	if (!Iova::MapDeviceDma(node, _IMM(buffer), PataDmaPageSize, Iova::DmaMappingFlag::Identity, mapping)) {
		free_pata_dma_page(buffer);
		return nullptr;
	}
	if ((mapping.device_address & (PataDmaPageSize - 1)) || mapping.device_address > 0x100000000ull - PataDmaPageSize) {
		if (!mapping.translated || Iova::UnmapDeviceDma(node, mapping.device_address, PataDmaPageSize)) free_pata_dma_page(buffer);
		return nullptr;
	}
	device_address = uint32(mapping.device_address);
	MemSet(buffer, 0, PataDmaPageSize);
	return buffer;
}

static bool stop_pata_dma(PataDmaChannelState& state) {
	const byte command = innpb(state.bmide_base + BMIDE_REG_CMD);
	outpb(state.bmide_base + BMIDE_REG_CMD, byte(command & ~BMIDE_CMD_START));
	for0(spin, 1000000) {
		if (!(innpb(state.bmide_base + BMIDE_REG_STATUS) & PataBmideStatusActive)) {
			state.quiesced = true;
			return true;
		}
		_ASM volatile("pause" ::: "memory");
	}
	state.quiesced = false;
	state.ready = false;
	plogwarn("[PATA-DMA] BMIDE=%[16H] engine did not stop; channel I/O disabled", (stduint)state.bmide_base);
	return false;
}

static bool build_pata_pci_device(DeviceNode* pata_node, uni::PCI::Device& pci_dev) {
	if (!pata_node) return false;
	pci_dev.segment_group = pata_node->fields.pci_segment;
	pci_dev.bus = pata_node->fields.pci_bus;
	pci_dev.device = pata_node->fields.pci_device;
	pci_dev.function = pata_node->fields.pci_function;
	pci_dev.header_type = 0;
	pci_dev.class_code.base = pata_node->fields.class_base;
	pci_dev.class_code.sub = pata_node->fields.class_sub;
	pci_dev.class_code.interface = pata_node->fields.class_if;
	return true;
}

static void initialize_pata_dma_channels_once(DeviceNode* pata_node) {
	static bool done = false;
	if (done) return;
	done = true;
	if (!pata_node || !(pata_node->fields.class_if & 0x80u)) return;

	uni::PCI::Device pci_dev{};
	if (!build_pata_pci_device(pata_node, pci_dev)) {
		// ploginfo("[PATA-DMA] skip channel init: no PCI IDE controller node");
		return;
	}
	if (!(uni::PCI::read_config_register(pci_dev, 0x04) & 0x0004u)) {
		plogwarn("[PATA-DMA] PCI bus mastering is unavailable; using PIO");
		return;
	}

	const auto* bmide_bar = Devsman::FindResource(pata_node, DeviceResourceType::PciBarIo, 4);
	if (!bmide_bar || !bmide_bar->start || (bmide_bar->start & 0xFu) || bmide_bar->start > 0xFFF0u || bmide_bar->length < 16) {
		// ploginfo("[PATA-DMA] skip channel init: BAR4(BMIDE) absent");
		return;
	}

	const word bmide_base = word(bmide_bar->start & 0xFFF8u);
	for0(channel, 2) {
		if ((!disks[channel * 2] || disks[channel * 2]->Block_Size != 512) &&
			(!disks[channel * 2 + 1] || disks[channel * 2 + 1]->Block_Size != 512)) continue;
		auto& state = pata_dma_channels[channel];
		state.bmide_base = word(bmide_base + channel * 8);
		if (!stop_pata_dma(state)) continue;
		state.prdt = static_cast<PataBmidePrd*>(allocate_pata_dma_page(pata_node, state.prdt_address));
		state.bounce = static_cast<byte*>(allocate_pata_dma_page(pata_node, state.bounce_address));
		state.ready = state.prdt && state.bounce;
		if (!state.ready) {
			if (state.prdt && Iova::UnmapDeviceDma(pata_node, state.prdt_address, PataDmaPageSize)) {
				free_pata_dma_page(state.prdt);
				state.prdt = nullptr;
			}
			if (state.bounce && Iova::UnmapDeviceDma(pata_node, state.bounce_address, PataDmaPageSize)) {
				free_pata_dma_page(state.bounce);
				state.bounce = nullptr;
			}
		}
		ploginfo("[PATA-DMA] ch%u init ready=%u BMIDE=%[16H] prdt=%p->%[32H] bounce=%p->%[32H] low=%u",
			(stduint)channel,
			(stduint)(state.ready ? 1u : 0u),
			(stduint)state.bmide_base,
			state.prdt, state.prdt_address, state.bounce, state.bounce_address,
			(stduint)((state.prdt && dma_low_pool.IsInRange(state.prdt)) ? 1u : 0u));
	}
}

static void ensure_pata_pci_bus_master_once(DeviceNode* pata_node) {
	static bool done = false;
	if (done) return;
	done = true;
	if (!pata_node || !(pata_node->fields.class_if & 0x80u)) return;

	uni::PCI::Device pci_dev{};
	if (!build_pata_pci_device(pata_node, pci_dev)) {
		// ploginfo("[PATA-DMA] no PCI IDE controller node for pci-cmd setup");
		return;
	}

	const uint16 old_cmd = uint16(uni::PCI::read_config_register(pci_dev, 0x04) & 0xFFFFu);
	if (old_cmd & 0x0004u) {
		// ploginfo("[PATA-DMA] pci bus-master already enabled cmd=%[16H]", (stduint)old_cmd);
		return;
	}

	const uint16 new_cmd = uint16(old_cmd | 0x0004u);
	uni::PCI::write_config_register(pci_dev, 0x04, new_cmd);
	// const uint16 verify_cmd = uint16(uni::PCI::read_config_register(pci_dev, 0x04) & 0xFFFFu);
	// ploginfo("[PATA-DMA] pci-cmd enable bus-master %[16H] -> %[16H]",
	// 	(stduint)old_cmd, (stduint)verify_cmd);
}

static void probe_pata_bus_master_capability_once(DeviceNode* pata_node) {
	static bool probed = false;
	if (probed) return;
	probed = true;

	uni::PCI::Device pci_dev{};
	if (!build_pata_pci_device(pata_node, pci_dev)) {
		// ploginfo("[PATA-DMA] no PCI IDE controller node");
		return;
	}

	// const byte prog_if = pata_node->fields.class_if;
	// const uint16 pci_cmd = uint16(uni::PCI::read_config_register(pci_dev, 0x04) & 0xFFFFu);
	// ploginfo("[PATA-DMA] pci %02x:%02x.%u prog_if=%[8H] bus-master=%u",
	// 	(stduint)pata_node->fields.pci_bus,
	// 	(stduint)pata_node->fields.pci_device,
	// 	(stduint)pata_node->fields.pci_function,
	// 	(stduint)prog_if,
	// 	(stduint)((prog_if & 0x80u) ? 1u : 0u));
	// ploginfo("[PATA-DMA] pci-cmd=%[16H] io=%u bus-master-en=%u",
	// 	(stduint)pci_cmd,
	// 	(stduint)((pci_cmd & 0x0001u) ? 1u : 0u),
	// 	(stduint)((pci_cmd & 0x0004u) ? 1u : 0u));

	const auto* bmide_bar = Devsman::FindResource(pata_node, DeviceResourceType::PciBarIo, 4);
	if (!bmide_bar || !bmide_bar->start) {
		// ploginfo("[PATA-DMA] BAR4(BMIDE) absent");
		return;
	}

	// const word bmide_base = word(bmide_bar->start & 0xFFF8u);
	// const byte pri_cmd = innpb(bmide_base + 0);
	// const byte pri_sts = innpb(bmide_base + 2);
	// const byte sec_cmd = innpb(bmide_base + 8);
	// const byte sec_sts = innpb(bmide_base + 10);
	// ploginfo("[PATA-DMA] BAR4=%p BMIDE=%[16H]", (void*)bmide_bar->start, (stduint)bmide_base);
	// ploginfo("[PATA-DMA] primary cmd=%[8H] sts=%[8H] secondary cmd=%[8H] sts=%[8H]",
	// 	(stduint)pri_cmd, (stduint)pri_sts, (stduint)sec_cmd, (stduint)sec_sts);
}

static void register_pata_storage_nodes(DeviceNode* pata_node) {
	if (!pata_node) return;
	for0(i, MAX_DRIVES) {
		pata_storage_nodes[i] = nullptr;
		if (!disks[i]) continue;
		const bool is_cdrom = disks[i]->Block_Size == 2048;
		String node_name;
		node_name.Format(is_cdrom ? "pata-cd@%u:%u" : "pata-disk@%u:%u",
			(stduint)disks[i]->getHigID(),
			(stduint)disks[i]->getLowID());
		pata_storage_nodes[i] = Devsman::RegisterStorageDevice(
			pata_node,
			node_name.reference(),
			DeviceBusType::PCI,
			is_cdrom ? "pata-cdrom" : "pata-disk",
			disks[i]);
		if (is_cdrom) {
			String alias = String::newFormat("sr-pata%u", (stduint)i);
			Devsman::RegisterDevAlias(pata_storage_nodes[i], alias.reference());
		}
		else {
			char alias[] = { 'h', 'd', char('a' + i), '\0' };
			Devsman::RegisterDevAlias(pata_storage_nodes[i], alias);
		}
	}
}

static bool start_pata_driver(DeviceNode* pata_node);

#if _MCCA == 0x8632
_ESYM_C void Handint_HDD1_Entry();
_ESYM_C void Handint_HDD_Entry();

void Handint_HDD1()
{
	if (disks[2]) disks[2]->getStatus();
	else if (disks[3]) disks[3]->getStatus();
	auto& ch = g_ide_channels[1];
	if (ch.irq_waiting) {
		ch.irq_waiting = 0;
		ch.irq_seen = 1;
	}
	IC.SendEOI(IRQ_ATA_DISK1);
}

void Handint_HDD()// HDD Master
{
	if (disks[0]) disks[0]->getStatus();
	else if (disks[1]) disks[1]->getStatus();
	auto& ch = g_ide_channels[0];
	if (ch.irq_waiting) {
		ch.irq_waiting = 0;
		ch.irq_seen = 1;
	}
	IC.SendEOI(IRQ_ATA_DISK0); // Acknowledge interrupt
}
#endif

//
typedef HD_Info hd_info_t[4];// 0:0 0:1 1:0 1:1
static hd_info_t* hd_info = nullptr;
static bool hd_info_valid[4] = { 0 };

#define	STATUS_BSY	0x80
#define	STATUS_DRQ	0x08
#define STATUS_ERR 0x01
#define STATUS_DF 0x20
#define	HD_TIMEOUT		10000	/* in millisec */
#define	HD_WAITFOR_TIMEOUT	(HD_TIMEOUT / 1000)

#if _MCCA == 0x8664
static bool PataPIOReady(byte status, const IdeChannelState& channel) {
	if (status == 0 || status == 0xFF || (status & STATUS_BSY)) return false;
	if (status & (STATUS_ERR | STATUS_DF)) return true;
	const auto phase = channel.pio_phase.load(uni::MemoryOrder_Acquire);
	if (phase == PataPIOPhase::WriteCommand) return false;
	return bool(status & STATUS_DRQ) == (phase == PataPIOPhase::DataIn);
}

static bool DispatchPataGSI(void* context) {
	if (context != &g_ide_channels[0] && context != &g_ide_channels[1]) return false;
	const byte channel = context == &g_ide_channels[0] ? 0 : 1;
	auto& state = g_ide_channels[channel];
	const word io_base = channel == 0 ? PORT_IDE_CommandBlock_0 : PORT_IDE_CommandBlock_1;
	const word ctrl_base = channel == 0 ? PORT_IDE_ControlBlock_0 : PORT_IDE_ControlBlock_1;
	if (state.pio_phase.load(uni::MemoryOrder_Acquire) == PataPIOPhase::Dma) {
		auto& dma = pata_dma_channels[channel];
		if (!dma.bmide_base) return false;
		SpinlockLocal irq_guard(&dma.irq_lock);
		const byte bmide_status = innpb(dma.bmide_base + BMIDE_REG_STATUS);
		if (!(bmide_status & (BMIDE_STATUS_IRQ | BMIDE_STATUS_ERROR))) return false;
		// Preserve completion/error bits before W1C; the waiting CPU may not run on the BSP.
		dma.irq_status.store(byte(dma.irq_status.load(uni::MemoryOrder_Relaxed) | bmide_status), uni::MemoryOrder_Release);
		(void)innpb(io_base + 7);
		outpb(dma.bmide_base + BMIDE_REG_STATUS, bmide_status);
		if (state.irq_waiting) {
			state.irq_waiting = 0;
			state.irq_seen = 1;
		}
		return true;
	}
	const byte status = innpb(ctrl_base);
	if (status == 0 || status == 0xFF || (status & STATUS_BSY)) return false;
	if (state.irq_waiting && !PataPIOReady(status, state)) {
		if (state.pio_phase.load(uni::MemoryOrder_Acquire) != PataPIOPhase::WriteCommand || !(status & STATUS_DRQ)) return false;
		(void)innpb(io_base + 7);
		return true;
	}
	// ATA/ATAPI-6: Alternate Status preserves INTRQ; reading Status acknowledges it.
	(void)innpb(io_base + 7);
	if (!state.irq_waiting) return false;
	state.irq_waiting = 0;
	state.irq_seen = 1;
	// The shared GSI dispatcher sends EOI after all device filters return.
	return true;
}

static bool BindPataGSI(byte channel) {
	if (channel >= numsof(g_ide_channels)) return false;
	auto& state = g_ide_channels[channel];
	if (state.gsi_binding.active) return true;
	const word io_base = channel == 0 ? PORT_IDE_CommandBlock_0 : PORT_IDE_CommandBlock_1;
	const word ctrl_base = channel == 0 ? PORT_IDE_ControlBlock_0 : PORT_IDE_ControlBlock_1;
	constexpr byte PataControlNIEN = 1u << 1;
	outpb(ctrl_base, PataControlNIEN);
	(void)innpb(io_base + 7);
	const byte isa_irq = byte(8 + (channel == 0 ? DEV_SLV_ATA_DISK0 : DEV_SLV_ATA_DISK1));
	const auto status = Devs::BindISAGSI(isa_irq, DispatchPataGSI, &state, state.gsi_binding);
	if (status != ACPIGSIStatus::Success) {
		plogwarn("[PATA] ch%u ISA IRQ%u binding failed status=%u; using polling", (stduint)channel, (stduint)isa_irq, uint8(status));
		return false;
	}
	ploginfo("[PATA] ch%u ISA IRQ%u bound to GSI%u", (stduint)channel, (stduint)isa_irq, state.gsi_binding.gsi);
	return true;
}
#endif

static bool waitfor(Harddisk_PATA* hdd, stduint mask, stduint val, stduint timeout_second)// return seccess
{
	if (!hdd) return false;
	const stduint safe_timeout = timeout_second ? timeout_second : 1;
	const stduint timeout_ticks = safe_timeout * CONFIG_SysTickFreq;
	const stduint start_tick = tick;
	const stduint max_spins = safe_timeout * 2000000;
	stduint spin = 0;

	while (true) {
		if ((hdd->getStatus() & mask) == val)
			return true;
		if (tick != start_tick && (tick - start_tick >= timeout_ticks))
			return false;
		if (++spin >= max_spins)
			return false;
		__asm__ __volatile__("pause" ::: "memory");
	}
}
static bool hd_cmd_wait(Harddisk_PATA* hdd) {
	return waitfor(hdd, STATUS_BSY, 0, HD_WAITFOR_TIMEOUT);
}

static bool hd_int_wait_channel(byte channel) {
	if (channel >= 2) return false;
	auto& ch = g_ide_channels[channel];
	const word io_base = (channel == 0) ? PORT_IDE_CommandBlock_0 : PORT_IDE_CommandBlock_1;
	#if _MCCA == 0x8664
	const word ctrl_base = channel == 0 ? PORT_IDE_ControlBlock_0 : PORT_IDE_ControlBlock_1;
	// Boot probing with IF clear must poll rather than await an undeliverable IRQ.
	const bool wait_for_irq = ch.gsi_binding.active && (getFlags() & 0x200u);
	#endif
	const stduint start_tick = tick;
	const stduint timeout_ticks = HD_WAITFOR_TIMEOUT * CONFIG_SysTickFreq;
	const stduint max_spins = HD_WAITFOR_TIMEOUT * 2000000;
	stduint spin = 0;

	while (true) {
		#if _MCCA == 0x8664
		// Do not acknowledge INTRQ while the routed handler is still expected to run.
		const byte status = innpb(ctrl_base);
		if (PataPIOReady(status, ch) && (ch.irq_seen || !wait_for_irq)) {
			if (!wait_for_irq) (void)innpb(io_base + 7);
			ch.irq_seen = 0;
			ch.irq_waiting = 0;
			return !(status & (STATUS_ERR | STATUS_DF));
		}
		#else
		if (ch.irq_seen) {
			ch.irq_seen = 0;
			ch.irq_waiting = 0;
			return true;
		}
		const byte status = innpb(io_base + 7);
		if (!(status & STATUS_BSY)) {
			ch.irq_seen = 0;
			ch.irq_waiting = 0;
			return true;
		}
		#endif
		if (tick != start_tick && (tick - start_tick >= timeout_ticks))
			break;
		if (++spin >= max_spins)
			break;
		__asm__ __volatile__("pause" ::: "memory");
	}
	ch.irq_waiting = 0;
	#if _MCCA == 0x8664
	const byte status = innpb(io_base + 7);
	ch.irq_seen = 0;
	if (PataPIOReady(status, ch)) {
		if (wait_for_irq && !ch.irq_fallback_reported) {
			ch.irq_fallback_reported = true;
			plogwarn("[PATA] ch%u IRQ wait timed out; falling back to status", (stduint)channel);
		}
		return !(status & (STATUS_ERR | STATUS_DF));
	}
	#endif
	return false;
}

static bool hd_int_wait_0() {
	return hd_int_wait_channel(0);
}

static bool hd_int_wait_1() {
	return hd_int_wait_channel(1);
}

static void hd_rw_foreback_0() {
	#if _MCCA == 0x8664
	g_ide_channels[0].pio_phase = PataPIOPhase::DataIn;
	#endif
	g_ide_channels[0].irq_seen = 0;
	g_ide_channels[0].irq_waiting = 1;
}

static void hd_rw_foreback_1() {
	#if _MCCA == 0x8664
	g_ide_channels[1].pio_phase = PataPIOPhase::DataIn;
	#endif
	g_ide_channels[1].irq_seen = 0;
	g_ide_channels[1].irq_waiting = 1;
}

static bool finish_pata_dma(Harddisk_PATA& hd, PataDmaChannelState& state, bool completed, byte observed_status) {
	const bool stopped = stop_pata_dma(state);
	byte bmide_status = 0;
	byte ata_status = 0;
	{
		SpinlockLocal irq_guard(&state.irq_lock);
		const byte live_status = innpb(state.bmide_base + BMIDE_REG_STATUS);
		bmide_status = byte(observed_status | live_status | state.irq_status.load(uni::MemoryOrder_Acquire));
		ata_status = hd.getStatus();
		outpb(state.bmide_base + BMIDE_REG_STATUS, byte(live_status | BMIDE_STATUS_IRQ | BMIDE_STATUS_ERROR));
		auto& channel = g_ide_channels[hd.getHigID()];
		channel.irq_waiting = 0;
		channel.irq_seen = 0;
	}
	// Stopping BMIDE does not cancel the drive's ATA command; an unfinished command needs channel recovery.
	if (ata_status & (STATUS_BSY | STATUS_DRQ)) {
		state.quiesced = false;
		plogwarn("[PATA-DMA] ide%u:%u ATA command is still active; channel I/O disabled",
			(stduint)hd.getHigID(), (stduint)hd.getLowID());
	}
	if (!stopped || !completed || !(bmide_status & BMIDE_STATUS_IRQ) || (bmide_status & BMIDE_STATUS_ERROR) ||
		(ata_status & (STATUS_BSY | STATUS_DRQ | STATUS_ERR | STATUS_DF))) {
		state.ready = false;
		plogwarn("[PATA-DMA] ide%u:%u transfer failed BM=%[8H] ATA=%[8H]; DMA disabled",
			(stduint)hd.getHigID(), (stduint)hd.getLowID(), (stduint)bmide_status, (stduint)ata_status);
		return false;
	}
	_ASM volatile("" ::: "memory");
	return true;
}

static bool hd_read_dma_once(Harddisk_PATA& hd, stduint BlockIden, void* Dest) {
	if (hd.Block_Size != 512 || hd.getHigID() >= numsof(pata_dma_channels) || !Dest) return false;
	if (hd.getID() >= MAX_DRIVES || !pata_dma_capable[hd.getID()] || BlockIden > 0x0FFFFFFFu) return false;
	auto& state = pata_dma_channels[hd.getHigID()];
	if (!state.ready || !state.quiesced || !state.prdt || !state.bounce) return false;
	if (!hd.fn_int_wait || !hd.fn_lup_wait) return false;

	MemSet(state.prdt, 0, sizeof(*state.prdt));
	MemSet(state.bounce, 0, 512);
	state.prdt[0].phys_base = state.bounce_address;
	state.prdt[0].byte_count = 512;
	state.prdt[0].flags = 0x8000u;

	HdiskCommand cmd = {};
	cmd.feature = 0;
	cmd.count = 1;
	for0(i, 3) cmd.LBA[i] = (BlockIden >> (i * 8));
	cmd.device = MAKE_DEVICE_REG(1, hd.getLowID(), (BlockIden >> 24) & 0xF);
	cmd.command = ATA_READ_DMA;
	{
		SpinlockLocal irq_guard(&state.irq_lock);
		outpb(state.bmide_base + BMIDE_REG_CMD, BMIDE_CMD_READ);
		const byte old_status = innpb(state.bmide_base + BMIDE_REG_STATUS);
		outpb(state.bmide_base + BMIDE_REG_STATUS, byte(old_status | BMIDE_STATUS_IRQ | BMIDE_STATUS_ERROR));
		state.irq_status = 0;
		outpd(state.bmide_base + BMIDE_REG_PRDT, state.prdt_address);
		asserv(hd.fn_feedback)();
		#if _MCCA == 0x8664
		g_ide_channels[hd.getHigID()].pio_phase = PataPIOPhase::Dma;
		#endif
	}
	if (!hd.Hdisk_OUT(&cmd)) {
		g_ide_channels[hd.getHigID()].irq_waiting = 0;
		return false;
	}

	_ASM volatile("" ::: "memory");
	state.quiesced = false;
	outpb(state.bmide_base + BMIDE_REG_CMD, BMIDE_CMD_READ | BMIDE_CMD_START);

	const word ctrl_base = (hd.getHigID() == 0) ? PORT_IDE_ControlBlock_0 : PORT_IDE_ControlBlock_1;
	const stduint start_tick = tick;
	const stduint timeout_ticks = (HD_TIMEOUT / 1000) * CONFIG_SysTickFreq;
	const stduint max_spins = (HD_TIMEOUT / 1000) * 2000000;
	stduint spin = 0;
	bool completed = false;
	byte observed_status = 0;

	while (true) {
		const byte bmide_status = byte(innpb(state.bmide_base + BMIDE_REG_STATUS) | state.irq_status.load(uni::MemoryOrder_Acquire));
		observed_status |= bmide_status;
		if (bmide_status & BMIDE_STATUS_ERROR) break;
		if (bmide_status & BMIDE_STATUS_IRQ) {
			const byte status = innpb(ctrl_base);
			if (!(status & STATUS_BSY)) {
				completed = true;
				break;
			}
		}
		if (tick != start_tick && (tick - start_tick >= timeout_ticks))
			break;
		if (++spin >= max_spins)
			break;
		__asm__ __volatile__("pause" ::: "memory");
	}

	if (!finish_pata_dma(hd, state, completed, observed_status)) return false;

	MemCopyN(Dest, state.bounce, 512);
	return true;
}

static bool hd_write_dma_once(Harddisk_PATA& hd, stduint BlockIden, const void* Sors) {
	if (hd.Block_Size != 512 || hd.getHigID() >= numsof(pata_dma_channels) || !Sors) return false;
	if (hd.getID() >= MAX_DRIVES || !pata_dma_capable[hd.getID()] || BlockIden > 0x0FFFFFFFu) return false;
	auto& state = pata_dma_channels[hd.getHigID()];
	if (!state.ready || !state.quiesced || !state.prdt || !state.bounce) return false;
	if (!hd.fn_int_wait || !hd.fn_lup_wait) return false;

	MemSet(state.prdt, 0, sizeof(*state.prdt));
	MemCopyN(state.bounce, Sors, 512);
	state.prdt[0].phys_base = state.bounce_address;
	state.prdt[0].byte_count = 512;
	state.prdt[0].flags = 0x8000u;

	HdiskCommand cmd = {};
	cmd.feature = 0;
	cmd.count = 1;
	for0(i, 3) cmd.LBA[i] = (BlockIden >> (i * 8));
	cmd.device = MAKE_DEVICE_REG(1, hd.getLowID(), (BlockIden >> 24) & 0xF);
	cmd.command = ATA_WRITE_DMA;
	{
		SpinlockLocal irq_guard(&state.irq_lock);
		outpb(state.bmide_base + BMIDE_REG_CMD, 0);
		const byte old_status = innpb(state.bmide_base + BMIDE_REG_STATUS);
		outpb(state.bmide_base + BMIDE_REG_STATUS, byte(old_status | BMIDE_STATUS_IRQ | BMIDE_STATUS_ERROR));
		state.irq_status = 0;
		outpd(state.bmide_base + BMIDE_REG_PRDT, state.prdt_address);
		asserv(hd.fn_feedback)();
		#if _MCCA == 0x8664
		g_ide_channels[hd.getHigID()].pio_phase = PataPIOPhase::Dma;
		#endif
	}
	if (!hd.Hdisk_OUT(&cmd)) {
		g_ide_channels[hd.getHigID()].irq_waiting = 0;
		return false;
	}

	_ASM volatile("" ::: "memory");
	state.quiesced = false;
	outpb(state.bmide_base + BMIDE_REG_CMD, BMIDE_CMD_START);

	const word ctrl_base = (hd.getHigID() == 0) ? PORT_IDE_ControlBlock_0 : PORT_IDE_ControlBlock_1;
	const stduint start_tick = tick;
	const stduint timeout_ticks = (HD_TIMEOUT / 1000) * CONFIG_SysTickFreq;
	const stduint max_spins = (HD_TIMEOUT / 1000) * 2000000;
	stduint spin = 0;
	bool completed = false;
	byte observed_status = 0;

	while (true) {
		const byte bmide_status = byte(innpb(state.bmide_base + BMIDE_REG_STATUS) | state.irq_status.load(uni::MemoryOrder_Acquire));
		observed_status |= bmide_status;
		if (bmide_status & BMIDE_STATUS_ERROR) break;
		if (bmide_status & BMIDE_STATUS_IRQ) {
			const byte status = innpb(ctrl_base);
			if (!(status & STATUS_BSY)) {
				completed = true;
				break;
			}
		}
		if (tick != start_tick && (tick - start_tick >= timeout_ticks))
			break;
		if (++spin >= max_spins)
			break;
		__asm__ __volatile__("pause" ::: "memory");
	}

	return finish_pata_dma(hd, state, completed, observed_status);
}



static void probe_pata_dma_read_once() {
	static bool probed = false;
	if (probed) return;
	probed = true;
	if (!single_sector) {
		// ploginfo("[PATA-DMA] skip dma-read probe: no sector buffer");
		return;
	}

	// Reference: plain PIO read of LBA 0.
	static byte pio_sector[512];
	static byte dma_sector[512];
	for0(drive, MAX_DRIVES) {
		if (!disks[drive] || disks[drive]->Block_Size != 512 || !pata_dma_capable[drive]) continue;
		auto& state = pata_dma_channels[disks[drive]->getHigID()];
		if (!state.ready) continue;
		const bool pio_ok = disks[drive]->Read(0, pio_sector, 1);
		if (!pio_ok) {
			plogwarn("[PATA-DMA] ide%u:%u PIO reference read failed; DMA left disabled", drive / 2, drive % 2);
			continue;
		}
		MutexLocal lock(&g_ide_channels[disks[drive]->getHigID()].lock);
		if (!hd_read_dma_once(*disks[drive], 0, dma_sector)) {
			plogwarn("[PATA-DMA] ide%u:%u DMA read probe failed; using PIO", drive / 2, drive % 2);
			continue;
		}
		stduint diff = 0;
		for0(i, sizeof(dma_sector)) {
			if (dma_sector[i] != pio_sector[i]) diff++;
		}
		if (diff) {
			state.ready = false;
			plogwarn("[PATA-DMA] ide%u:%u DMA/PIO mismatch on LBA0 (%u bytes); using PIO", drive / 2, drive % 2, diff);
			plogwarn("[PATA-DMA] dma=%02X %02X %02X %02X pio=%02X %02X %02X %02X",
				(unsigned)dma_sector[0], (unsigned)dma_sector[1],
				(unsigned)dma_sector[2], (unsigned)dma_sector[3],
				(unsigned)pio_sector[0], (unsigned)pio_sector[1],
				(unsigned)pio_sector[2], (unsigned)pio_sector[3]);
			continue;
		}
		pata_dma_enabled[drive] = true;
		ploginfo("[PATA-DMA] ide%u:%u DMA read verified against PIO on LBA0", drive / 2, drive % 2);
	}
}

// Use after print_identify_info
stduint uni::Harddisk_PATA::getUnits() {
	return (*hd_info)[getHigID() * 2 + getLowID()].whole_disk.length;
}

struct iden_info_ascii {
	int idx;
	int len;
	rostr desc;
} iinfo[] = {
	{10, 20, "HD SN"}, // Serial number in ASCII
	{27, 40, "Model"}, // Model number in ASCII
};

static int highest_mode_bit(word value) {
	for (int bit = 7; bit >= 0; --bit) {
		if (value & (1u << bit)) return bit;
	}
	return -1;
}

// Initialize HD_Info
static void print_identify_info(uint16* hdinfo, Harddisk_PATA& hd)
{
	int i, k;
	char s[64];

	for0a(k, iinfo) {
		char* p = (char*)&hdinfo[iinfo[k].idx];
		for (i = 0; i < iinfo[k].len / 2; i++) {
			s[i * 2 + 1] = *p++;
			s[i * 2] = *p++;
		}
		s[i * 2] = 0;
		if (false) outsfmt("[Hrddisk] %s: %s\n\r", iinfo[k].desc, s);
	}

	int capabilities = hdinfo[49];
	pata_dma_capable[hd.getID()] = hd.Block_Size == 512 && (capabilities & 0x0100u);
	int cmd_set_supported = hdinfo[83];
	if (false) outsfmt("[Hrddisk] LBA  : %s\n\r",
		(capabilities & 0x0200) ? (cmd_set_supported & 0x0400) ? "Supported LBA48" : "Supported" : "No");

	// const word dma_caps = word(capabilities);
	const word mwdma = hdinfo[63];
	const word udma = hdinfo[88];
	const int mwdma_supported = highest_mode_bit(word(mwdma & 0x00FFu));
	const int mwdma_active = highest_mode_bit(word((mwdma >> 8) & 0x00FFu));
	const int udma_supported = highest_mode_bit(word(udma & 0x00FFu));
	const int udma_active = highest_mode_bit(word((udma >> 8) & 0x00FFu));
	// ploginfo("[PATA-DMA] ide%u:%u identify dma=%u mwdma=%[16H] udma=%[16H]",
	// 	(stduint)hd.getHigID(), (stduint)hd.getLowID(),
	// 	(stduint)((dma_caps & 0x0100u) ? 1u : 0u),
	// 	(stduint)mwdma, (stduint)udma);
	ploginfo("[PATA-DMA] ide%u:%u supported mwdma=%d active=%d supported udma=%d active=%d",
		(stduint)hd.getHigID(), (stduint)hd.getLowID(),
		mwdma_supported, mwdma_active, udma_supported, udma_active);

	int sectors = ((int)hdinfo[61] << 16) + hdinfo[60];
	// outsfmt("[Hrddisk] Size : %lf MB\n\r", (double)sectors * hd.Block_Size / 1024 / 1024);// care for #NM FPU Loss

	(*hd_info)[hd.getHigID() * 2 + hd.getLowID()].whole_disk.address = nil;
	(*hd_info)[hd.getHigID() * 2 + hd.getLowID()].whole_disk.length = sectors;
}

inline static void print_hdinfo(Harddisk_PATA& hd)
{
	HD_Info& hdinfo = (*hd_info)[hd.getHigID() * 2 + hd.getLowID()];
	auto end_lba = [](const uni::PartitionSlice& slice) -> stduint {
		return slice.length ? slice.address + slice.length - 1 : slice.address;
	};
	Console.OutFormat("driver %u:%u\n\r", hd.getHigID(), hd.getLowID());
	Console.OutFormat("whole  %u..%u\n\r",
		hdinfo.whole_disk.address,
		end_lba(hdinfo.whole_disk));
	Console.OutFormat("parts  %u (+%u overflow)\n\r", hdinfo.part_count, hdinfo.part_overflow);
	for0(i, hdinfo.part_count) {
		auto part = hdinfo.parts[i];
		Console.OutFormat("%u      %u..%u type=%x\n\r",
			i + 1,
			part.address,
			end_lba(part),
			part.sys_id);
	}
}

enum { REG_DATA = 0 };
//{TODO} into harddisk.cpp:Open()
static void hd_open(Harddisk_PATA& hd) { // 0x00
	const byte ch = hd.getHigID();
	const byte low_id = hd.getLowID();
	{
		MutexLocal lock(&g_ide_channels[ch].lock);
		HdiskCommand cmd = {};
		cmd.command = (hd.Block_Size == 2048) ? ATAPI_CMD_IDENTIFY : ATA_IDENTIFY;
		cmd.device = MAKE_DEVICE_REG(0, low_id, 0);
		if (ch == 0) hd_rw_foreback_0();
		else hd_rw_foreback_1();
		hd.Hdisk_OUT(&cmd);
		if (ch == 0) hd_int_wait_0();
		else hd_int_wait_1();
		word io_base = ch == 0 ? PORT_IDE_CommandBlock_0 : PORT_IDE_CommandBlock_1;
		IN_wn(io_base + REG_DATA, (word*)single_sector, 512); // IDENTIFY payload is always 512 bytes
		print_identify_info((uint16*)single_sector, hd);
	}
	if (!hd_info_valid[ch * 2 + low_id]) {
		HD_Info& hdi = (*hd_info)[ch * 2 + low_id];
		if (hd.Block_Size == 512) { // Skip MBR mounting for ATAPI CD-ROMs
			DiscPartition::Partition(hd, hdi, (byte*)single_sector, NR_PRIM_PER_DRIVE * low_id);
		}
		hd_info_valid[ch * 2 + low_id] = true;
	}
}

static void hd_close(Harddisk_PATA& hd) { // 0x02
	hd_info_valid[hd.getHigID() * 2 + hd.getLowID()] = false;
}

PartitionSlice Harddisk_PATA::getSlice(stduint dev) {
	if (Block_Size == 2048) {
		PartitionSlice slice = {};
		if (dev != 0) {
			return slice;
		}
		slice.address = 0;
		slice.length = (getID() < numsof(disks) && disks[getID()]) ? disks[getID()]->getUnits() : 0;
		slice.sys_id = ClassicFilesysType::None;
		return slice;
	}
	HD_Info& hdinfo = (*hd_info)[getHigID() * 2 + getLowID()];
	return GetPartitionSlice(hdinfo, dev);
}

bool Harddisk_PATA::Read(stduint BlockIden, void* Dest, stduint Times) {
	if (BlockIden + Times > getUnits() || !Dest) return false;
	if (BlockIden > 0x0FFFFFFFu || Times > 0x10000000u - BlockIden) return false;
	const byte ch = getHigID();
	if (ch >= 2) return false;
	MutexLocal lock(&g_ide_channels[ch].lock);
	if (!pata_dma_channels[ch].quiesced) return false;
	for0(t, Times) {
		stduint blk = BlockIden + t;
		byte* dst = (byte*)Dest + t * Block_Size;
		if (Block_Size == 512 && pata_dma_enabled[getID()] && hd_read_dma_once(*this, blk, dst)) {
			continue;
		}
		if (!pata_dma_channels[ch].quiesced) return false;
		// Fallback to PIO
		HdiskCommand cmd = {};
		cmd.feature = 0;
		cmd.count = 1;
		for0(i, 3) cmd.LBA[i] = (blk >> (i * 8));
		cmd.device = MAKE_DEVICE_REG(1, getLowID(), (blk >> 24) & 0xF);
		cmd.command = ATA_READ;
		asserv(fn_feedback)();
		if (!Harddisk_PATA::Hdisk_OUT(&cmd)) return false;
		if (fn_int_wait && fn_lup_wait) {
			const bool use_loop_fallback = !fn_int_wait();
			if (use_loop_fallback) {
				if (!PataWaitRetry(this, fn_lup_wait, STATUS_DRQ, STATUS_DRQ)) return false;
			}
			else if (!fn_lup_wait(this, STATUS_DRQ, STATUS_DRQ, HD_TIMEOUT / 1000)) {
				return false;
			}
			word io_base = (ch == 0) ? PORT_IDE_CommandBlock_0 : PORT_IDE_CommandBlock_1;
			IN_wn(io_base + 0, (word*)dst, Block_Size);
		}
		else return false;
	}
	return true;
}

bool Harddisk_PATA::Write(stduint BlockIden, const void* Sors, stduint Times) {
	if (BlockIden + Times > getUnits() || !Sors) return false;
	if (BlockIden > 0x0FFFFFFFu || Times > 0x10000000u - BlockIden) return false;
	const byte ch = getHigID();
	if (ch >= 2) return false;
	MutexLocal lock(&g_ide_channels[ch].lock);
	if (!pata_dma_channels[ch].quiesced) return false;
	for0(t, Times) {
		stduint blk = BlockIden + t;
		const byte* src = (const byte*)Sors + t * Block_Size;
		if (Block_Size == 512 && pata_dma_enabled[getID()] && hd_write_dma_once(*this, blk, src)) {
			continue;
		}
		if (!pata_dma_channels[ch].quiesced) return false;
		// Fallback to PIO
		HdiskCommand cmd = {};
		cmd.feature = 0;
		cmd.count = 1;
		for0(i, 3) cmd.LBA[i] = (blk >> (i * 8));
		cmd.device = MAKE_DEVICE_REG(1, getLowID(), (blk >> 24) & 0xF);
		cmd.command = ATA_WRITE;
		asserv(fn_feedback)();
		#if _MCCA == 0x8664
		g_ide_channels[ch].pio_phase = PataPIOPhase::WriteCommand;
		#endif
		if (!Harddisk_PATA::Hdisk_OUT(&cmd)) return false;
		if (fn_int_wait && fn_lup_wait) {
			if (!fn_lup_wait(this, STATUS_DRQ, STATUS_DRQ, HD_TIMEOUT / 1000)) return false;
			word io_base = (ch == 0) ? PORT_IDE_CommandBlock_0 : PORT_IDE_CommandBlock_1;
			#if _MCCA == 0x8664
			g_ide_channels[ch].pio_phase = PataPIOPhase::WriteData;
			#endif
			OUT_wn(io_base + 0, (word*)src, Block_Size);
			const bool use_loop_fallback = !fn_int_wait();
			if (use_loop_fallback) {
				if (!PataWaitRetry(this, fn_lup_wait, STATUS_BSY, 0)) return false;
			}
			if (getStatus() & (STATUS_ERR | STATUS_DF)) return false;
		}
		else return false;
	}
	return true;
}

static void register_pata_partition_nodes(stduint disk_id) {
	if (disk_id >= MAX_DRIVES || !pata_storage_nodes[disk_id] || !disks[disk_id] || !hd_info) return;
	HD_Info& hdinfo = (*hd_info)[disk_id];
	for (stduint part_dev = 1; part_dev <= hdinfo.part_count; ++part_dev) {
		uni::PartitionSlice slice = GetPartitionSlice(hdinfo, part_dev);
		if (slice.sys_id == ClassicFilesysType::None || slice.sys_id == ClassicFilesysType::EXT || slice.length == 0) continue;
		String node_name = String::newFormat("partition@%u", part_dev);
		Devsman::RegisterStoragePartition(
			pata_storage_nodes[disk_id],
			node_name.reference(),
			(*disks[disk_id]),
			part_dev);
	}
}

// xCD_ATAPI::Read overrides Harddisk_PATA::Read, so the channel lock taken there
// is never held for a CD-ROM. While the disk was serviced by its own process every
// ATAPI transaction was serialised; now any thread drives the register sequence in
// place, so two readers interleaving on one drive leave it in an error state.
struct MecocoaCD_ATAPI : public xCD_ATAPI {
	MecocoaCD_ATAPI(byte id = 0) : xCD_ATAPI(id) {}
	virtual bool Read(stduint BlockIden, void* Dest, stduint Times = 1) override {
		const byte ch = getHigID();
		if (ch >= 2) return false;
		MutexLocal lock(&g_ide_channels[ch].lock);
		if (!pata_dma_channels[ch].quiesced) return false;
		const bool ok = xCD_ATAPI::Read(BlockIden, Dest, Times);
		if (!ok) {
			// 1 = Error, 7 = Status: 0x80 BSY, 0x08 DRQ, 0x01 ERR
			const byte status = innpb(io_base + 7);
			plogwarn("[PATA-CD] atapi read fail lba=%u times=%u ch=%u status=%u err=%u%s",
				(stduint)BlockIden, (stduint)Times, (stduint)ch,
				(stduint)status, (stduint)innpb(io_base + 1),
				(status & 0x80) ? " busy" : ((status & 0x01) ? " err" : ""));
		}
		return ok;
	}
};

static bool start_pata_driver(DeviceNode* pata_node) {
	if (!pata_node) return false;
	if (pata_controller_node) {
		if (pata_controller_node == pata_node) {
			pata_node->fields.binding.driver_data = disks;
			return true;
		}
		plogwarn("[PATA] additional controller unsupported: node=%s owner=%s",
			pata_node->link.addr ? pata_node->link.addr : "(unnamed)",
			pata_controller_node->link.addr ? pata_controller_node->link.addr : "(unnamed)");
		return false;
	}
	if (pata_node->fields.class_if & PATA_NATIVE_MODE_MASK) {
		plogwarn("[PATA] native mode controller unsupported: %s prog-if=%[8H]",
			pata_node->link.addr ? pata_node->link.addr : "(unnamed)",
			(stduint)pata_node->fields.class_if);
		return false;
	}
	uni::PCI::Device pci_dev{};
	if (!build_pata_pci_device(pata_node, pci_dev)) return false;
	const uint16 pci_command = uint16(uni::PCI::read_config_register(pci_dev, 0x04));
	if (!(pci_command & 0x0001u)) {
		uni::PCI::write_config_register(pci_dev, 0x04, uint16(pci_command | 0x0001u));
		if (!(uni::PCI::read_config_register(pci_dev, 0x04) & 0x0001u)) {
			plogwarn("[PATA] PCI I/O space enable failed");
			return false;
		}
	}
	pata_controller_node = pata_node;
	ensure_pata_pci_bus_master_once(pata_node);
	probe_pata_bus_master_capability_once(pata_node);

	for0(i, MAX_DRIVES) {
		Harddisk_PATA probe(i);
		byte dev_type = probe.ProbeDevice();
		if (dev_type == 1) {
			disks[i] = new (hdd_buf + i * byteof(**disks)) Harddisk_PATA(i);
		} else if (dev_type == 2) {
			disks[i] = new (hdd_buf + i * byteof(**disks)) MecocoaCD_ATAPI(i);
		} else {
			disks[i] = nullptr;
		}
		if (disks[i]) {
			disks[i]->fn_cmd_wait = hd_cmd_wait;
			disks[i]->fn_int_wait = (disks[i]->getHigID() == 0) ? hd_int_wait_0 : hd_int_wait_1;
			disks[i]->fn_lup_wait = waitfor;
			disks[i]->fn_feedback = (disks[i]->getHigID() == 0) ? hd_rw_foreback_0 : hd_rw_foreback_1;
			#if _MCCA == 0x8632
			disks[i]->react_type = Harddisk_PATA::ReactType::Rupt;
			#else
			// Keep INTRQ masked until this channel has an installed GSI route.
			disks[i]->react_type = Harddisk_PATA::ReactType::Loop;
			#endif
		}
	}
	#if _MCCA == 0x8632
	if (disks[0] || disks[1]) (disks[0] ? disks[0] : disks[1])->setInterrupt(NULL);
	if (disks[2] || disks[3]) (disks[2] ? disks[2] : disks[3])->setInterrupt(NULL);

	#else
	for0(channel, numsof(g_ide_channels)) {
		const stduint first_drive = channel * 2;
		if (!disks[first_drive] && !disks[first_drive + 1]) continue;
		if (!BindPataGSI(byte(channel))) continue;
		for0(drive, 2) {
			if (disks[first_drive + drive]) disks[first_drive + drive]->react_type = Harddisk_PATA::ReactType::Rupt;
		}
	}
	#endif
	initialize_pata_dma_channels_once(pata_node);

	if (!single_sector) single_sector = new char[0x1000];
	if (!hd_info) hd_info = new HD_Info[1][4];

	register_pata_storage_nodes(pata_node);

	for0(i, MAX_DRIVES) {
		if (disks[i]) {
			hd_open(*disks[i]);
			stduint block_size = disks[i]->Block_Size;
			stduint total_units = 0;
			uint64 total_bytes = 0;
			if (pata_storage_nodes[i]) {
				(void)Devsman::Ctrl(pata_storage_nodes[i],
					(stduint)DeviceCtrlCommand::GetBlockSize, &block_size);
				(void)Devsman::Ctrl(pata_storage_nodes[i],
					(stduint)DeviceCtrlCommand::GetUnitCount, &total_units);
				(void)Devsman::Ctrl(pata_storage_nodes[i],
					(stduint)DeviceCtrlCommand::GetByteSize, &total_bytes);
			}
			if (!total_units) {
				total_units = disks[i]->getUnits();
			}
			if (!total_bytes) {
				total_bytes = uint64(total_units) * uint64(block_size);
			}
			ploginfo("[Hrddisk] Detect %s on IDE%u:%u : %u MB",
				(block_size == 2048) ? "CD-ROM" : "Disk",
				i / 2, i % 2,
				stduint(total_bytes >> 20));
		}
	}

	probe_pata_dma_read_once();

	for0(i, MAX_DRIVES) {
		if (!disks[i] || disks[i]->Block_Size != 2048) continue;
		String lab = String::newFormat("/mnt/ide%u.0", i);
		ploginfo("[Hrddisk] probe cdrom%u whole-disk", i);
		if (auto fs = Filesys::Mount(*disks[i], 0, lab.reference(), pata_storage_nodes[i])) {
			ploginfo("[Hrddisk] mount %s on %s", fs->name, lab.reference());
		} else {
			ploginfo("[Hrddisk] no filesystem recognized on cdrom%u", i);
		}
	}

	for0(i, MAX_DRIVES) {
		if (!disks[i] || disks[i]->Block_Size != 512) continue;
		register_pata_partition_nodes(i);
		HD_Info& hdinfo = (*hd_info)[i];
		for (unsigned part_dev = 1; part_dev <= hdinfo.part_count; part_dev++) {
			uni::PartitionSlice slice = GetPartitionSlice(hdinfo, part_dev);
			auto sys_id = slice.sys_id;
			if (sys_id == ClassicFilesysType::None || slice.sys_id == ClassicFilesysType::EXT || slice.length == 0) continue;
			String node_name = String::newFormat("partition@%u", part_dev);
			DeviceNode* part_node = Devsman::RegisterStoragePartition(
				pata_storage_nodes[i], node_name.reference(), (*disks[i]), part_dev);
			String lab = String::newFormat("/mnt/ide%u.%u", i, part_dev);
			plogtrac("[Hrddisk] probe disk%u part%u: %x", i, part_dev, sys_id);
			if (auto fs = Filesys::Mount(*disks[i], part_dev, lab.reference(), part_node)) {
				ploginfo("[Hrddisk] mount %s on %s", fs->name, lab.reference());
			}
		}
	}

	pata_node->fields.binding.driver_data = disks;
	return true;
}

void R_HDD_INIT() {
	#if _MCCA == 0x8632
	IC[IRQ_ATA_DISK0].setRange(mglb(Handint_HDD_Entry), SegCo32);
	register_interrupt_handler(IRQ_ATA_DISK0, Handint_HDD);
	IC[IRQ_ATA_DISK1].setRange(mglb(Handint_HDD1_Entry), SegCo32);
	register_interrupt_handler(IRQ_ATA_DISK1, Handint_HDD1);
	#endif
	Devsman::RegisterDriverStarter("pata", start_pata_driver);
	Devsman::StartKnownDrivers();
}


#endif
