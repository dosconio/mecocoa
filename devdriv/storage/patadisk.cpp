// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: Disk - PATA
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

// unchk: IDE1:0, IDE1:1

#include "../../include/mecocoa.hpp"
#include <c/storage/harddisk.h>
#include <c/format/filesys.h>
#include <cpp/atomic>
#include <cpp/Device/Bus/PCI.hpp>

_ESYM_C void R_HDD_INIT();

#if _MCCA == 0x8632

#if 1
__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_HDD{
	.init = R_HDD_INIT,
	.name = "DISK-PATA",
};
#endif

Harddisk_PATA* disks[MAX_DRIVES];// referenced
static char hdd_buf[byteof(**disks) * numsof(disks)];
static uni::Atomic<byte> lock[2] = {1, 1};
static char* single_sector = NULL;// file-hd used buffer

struct PataDmaChannelState {
	word bmide_base = 0;
	PataBmidePrd* prdt = nullptr;
	byte* bounce = nullptr;
	bool ready = false;
};

static PataDmaChannelState pata_dma_channels[2];

static bool build_pata_pci_device(uni::PCI::Device& pci_dev, DeviceNode*& pata_node) {
	pata_node = Devsman::FindPCIDeviceByClass(0x01u, 0x01u, 0xFFu);
	if (!pata_node) return false;
	pci_dev.bus = pata_node->fields.pci_bus;
	pci_dev.device = pata_node->fields.pci_device;
	pci_dev.function = pata_node->fields.pci_function;
	pci_dev.header_type = 0;
	pci_dev.class_code.base = pata_node->fields.class_base;
	pci_dev.class_code.sub = pata_node->fields.class_sub;
	pci_dev.class_code.interface = pata_node->fields.class_if;
	return true;
}

static void initialize_pata_dma_channels_once() {
	static bool done = false;
	if (done) return;
	done = true;

	uni::PCI::Device pci_dev{};
	DeviceNode* pata_node = nullptr;
	if (!build_pata_pci_device(pci_dev, pata_node)) {
		// ploginfo("[PATA-DMA] skip channel init: no PCI IDE controller node");
		return;
	}

	const auto* bmide_bar = Devsman::FindResource(pata_node, DeviceResourceType::PciBarIo, 4);
	if (!bmide_bar || !bmide_bar->start) {
		// ploginfo("[PATA-DMA] skip channel init: BAR4(BMIDE) absent");
		return;
	}

	const word bmide_base = word(bmide_bar->start & 0xFFF8u);
	for0(channel, 2) {
		if (!disks[channel * 2] && !disks[channel * 2 + 1]) continue;
		auto& state = pata_dma_channels[channel];
		state.bmide_base = word(bmide_base + channel * 8);
		if (!state.prdt) state.prdt = (PataBmidePrd*)mempool.allocate(0x1000, 12);
		if (!state.bounce) state.bounce = (byte*)mempool.allocate(0x1000, 12);
		if (state.prdt) MemSet(state.prdt, 0, 0x1000);
		if (state.bounce) MemSet(state.bounce, 0, 0x1000);
		state.ready = state.prdt && state.bounce;
		ploginfo("[PATA-DMA] ch%u init ready=%u BMIDE=%[16H] prdt=%p bounce=%p",
			(stduint)channel,
			(stduint)(state.ready ? 1u : 0u),
			(stduint)state.bmide_base,
			state.prdt, state.bounce);
	}
}

static void ensure_pata_pci_bus_master_once() {
	static bool done = false;
	if (done) return;
	done = true;

	uni::PCI::Device pci_dev{};
	DeviceNode* pata_node = nullptr;
	if (!build_pata_pci_device(pci_dev, pata_node)) {
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

static void probe_pata_bus_master_capability_once() {
	static bool probed = false;
	if (probed) return;
	probed = true;

	uni::PCI::Device pci_dev{};
	DeviceNode* pata_node = nullptr;
	if (!build_pata_pci_device(pci_dev, pata_node)) {
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

static void register_pata_storage_nodes() {
	auto* pata_node = Devsman::FindPCIDeviceByClass(0x01u, 0x01u, 0xFFu);
	if (!pata_node) return;
	for0(i, MAX_DRIVES) {
		if (!disks[i]) continue;
		const bool is_cdrom = disks[i]->Block_Size == 2048;
		String node_name;
		node_name.Format(is_cdrom ? "pata-cd@%u:%u" : "pata-disk@%u:%u",
			(stduint)disks[i]->getHigID(),
			(stduint)disks[i]->getLowID());
		Devsman::RegisterStorageDevice(
			pata_node,
			node_name.reference(),
			DeviceBusType::PCI,
			is_cdrom ? "pata-cdrom" : "pata-disk",
			disks[i]);
	}
}

_ESYM_C void Handint_HDD1_Entry();

void Handint_HDD1()
{
	if (disks[2]) disks[2]->getStatus();
	else if (disks[3]) disks[3]->getStatus();
	if (lock[1].exchange(1)) {
		IC.SendEOI(IRQ_ATA_DISK1);
		return;
	}
	rupt_proc(Task_Hdd_Serv, IRQ_ATA_DISK1);
	IC.SendEOI(IRQ_ATA_DISK1);
}

void R_HDD_INIT() {
	IC[IRQ_ATA_DISK0].setRange(mglb(Handint_HDD_Entry), SegCo32);
	register_interrupt_handler(IRQ_ATA_DISK0, Handint_HDD);
	IC[IRQ_ATA_DISK1].setRange(mglb(Handint_HDD1_Entry), SegCo32);
	register_interrupt_handler(IRQ_ATA_DISK1, Handint_HDD1);
	
	for0(i, MAX_DRIVES) {
		Harddisk_PATA probe(i);
		byte dev_type = probe.ProbeDevice();
		if (dev_type == 1) {
			disks[i] = new (hdd_buf + i * byteof(**disks)) Harddisk_PATA(i);
		} else if (dev_type == 2) {
			disks[i] = new (hdd_buf + i * byteof(**disks)) xCD_ATAPI(i);
		} else {
			disks[i] = nullptr;
		}
	}
	if (disks[0] || disks[1]) (disks[0] ? disks[0] : disks[1])->setInterrupt(NULL);
	if (disks[2] || disks[3]) (disks[2] ? disks[2] : disks[3])->setInterrupt(NULL); // Enable secondary channel interrupt
	if (!single_sector) single_sector = new char[0x1000];
	register_pata_storage_nodes();
}

void Handint_HDD()// HDD Master
{
	if (disks[0]) disks[0]->getStatus();
	else if (disks[1]) disks[1]->getStatus();
	if (lock[0].exchange(1)) {
		IC.SendEOI(IRQ_ATA_DISK0);
		plogwarn(">>> Handint_HDD");
		return;
	}
	rupt_proc(Task_Hdd_Serv, IRQ_ATA_DISK0);
	IC.SendEOI(IRQ_ATA_DISK0); // Acknowledge interrupt
}

//
typedef HD_Info hd_info_t[4];// 0:0 0:1 1:0 1:1
static hd_info_t* hd_info = nullptr;
static bool hd_info_valid[4] = { 0 };

#define	STATUS_BSY	0x80
#define	STATUS_DRQ	0x08
#define	HD_TIMEOUT		10000	/* in millisec */
#define	HD_WAITFOR_TIMEOUT	(HD_TIMEOUT / 1000)

static bool waitfor(Harddisk_PATA* hdd, stduint mask, stduint val, stduint timeout_second)// return seccess
{
	int t = syscall(syscall_t::TIME, 0);
	while (((syscall(syscall_t::TIME, 0) - t)) < timeout_second)
		if ((hdd->getStatus() & mask) == val)
			return 1;
	return 0;
}
static bool hd_cmd_wait(Harddisk_PATA* hdd) {
	return waitfor(hdd, STATUS_BSY, 0, HD_WAITFOR_TIMEOUT);
}

static bool hd_int_wait() {
	CommMsg msg;
	sysrecv(INTRUPT, (&msg), 0);
	return true;
}
static void hd_rw_foreback_0() { lock[0] = 0; }
static void hd_rw_foreback_1() { lock[1] = 0; }

static bool hd_read_dma_once(Harddisk_PATA& hd, stduint BlockIden, void* Dest) {
	if (hd.Block_Size != 512 || hd.getHigID() >= numsof(pata_dma_channels) || !Dest) return false;
	auto& state = pata_dma_channels[hd.getHigID()];
	if (!state.ready || !state.prdt || !state.bounce) return false;
	if (!hd.fn_int_wait || !hd.fn_lup_wait) return false;

	MemSet(state.prdt, 0, sizeof(*state.prdt));
	MemSet(state.bounce, 0, 512);
	state.prdt[0].phys_base = uint32(stduint(state.bounce));
	state.prdt[0].byte_count = 512;
	state.prdt[0].flags = 0x8000u;

	outpb(state.bmide_base + BMIDE_REG_CMD, 0);
	outpb(state.bmide_base + BMIDE_REG_STATUS, BMIDE_STATUS_IRQ | BMIDE_STATUS_ERROR);
	outpd(state.bmide_base + BMIDE_REG_PRDT, uint32(stduint(state.prdt)));

	HdiskCommand cmd = {};
	cmd.feature = 0;
	cmd.count = 1;
	for0(i, 3) cmd.LBA[i] = (BlockIden >> (i * 8));
	cmd.device = MAKE_DEVICE_REG(1, hd.getLowID(), (BlockIden >> 24) & 0xF);
	cmd.command = ATA_READ_DMA;
	asserv(hd.fn_feedback)();
	if (!hd.Hdisk_OUT(&cmd)) return false;

	outpb(state.bmide_base + BMIDE_REG_CMD, BMIDE_CMD_READ | BMIDE_CMD_START);

	const bool use_loop_fallback = !hd.fn_int_wait();
	if (use_loop_fallback) {
		if (!PataWaitRetry(&hd, hd.fn_lup_wait, STATUS_BSY, 0)) {
			outpb(state.bmide_base + BMIDE_REG_CMD, 0);
			return false;
		}
	}
	else if (!hd.fn_lup_wait(&hd, STATUS_BSY, 0, HD_TIMEOUT / 1000)) {
		outpb(state.bmide_base + BMIDE_REG_CMD, 0);
		return false;
	}

	outpb(state.bmide_base + BMIDE_REG_CMD, 0);
	const byte bmide_status = innpb(state.bmide_base + BMIDE_REG_STATUS);
	outpb(state.bmide_base + BMIDE_REG_STATUS, bmide_status & (BMIDE_STATUS_IRQ | BMIDE_STATUS_ERROR));
	if (bmide_status & BMIDE_STATUS_ERROR) return false;
	if (!use_loop_fallback && !(bmide_status & BMIDE_STATUS_IRQ)) return false;

	MemCopyN(Dest, state.bounce, 512);
	return true;
}

static bool hd_write_dma_once(Harddisk_PATA& hd, stduint BlockIden, const void* Sors) {
	if (hd.Block_Size != 512 || hd.getHigID() >= numsof(pata_dma_channels) || !Sors) return false;
	auto& state = pata_dma_channels[hd.getHigID()];
	if (!state.ready || !state.prdt || !state.bounce) return false;
	if (!hd.fn_int_wait || !hd.fn_lup_wait) return false;

	MemSet(state.prdt, 0, sizeof(*state.prdt));
	MemCopyN(state.bounce, Sors, 512);
	state.prdt[0].phys_base = uint32(stduint(state.bounce));
	state.prdt[0].byte_count = 512;
	state.prdt[0].flags = 0x8000u;

	outpb(state.bmide_base + BMIDE_REG_CMD, 0);
	outpb(state.bmide_base + BMIDE_REG_STATUS, BMIDE_STATUS_IRQ | BMIDE_STATUS_ERROR);
	outpd(state.bmide_base + BMIDE_REG_PRDT, uint32(stduint(state.prdt)));

	HdiskCommand cmd = {};
	cmd.feature = 0;
	cmd.count = 1;
	for0(i, 3) cmd.LBA[i] = (BlockIden >> (i * 8));
	cmd.device = MAKE_DEVICE_REG(1, hd.getLowID(), (BlockIden >> 24) & 0xF);
	cmd.command = ATA_WRITE_DMA;
	asserv(hd.fn_feedback)();
	if (!hd.Hdisk_OUT(&cmd)) return false;

	outpb(state.bmide_base + BMIDE_REG_CMD, BMIDE_CMD_START);

	const bool use_loop_fallback = !hd.fn_int_wait();
	if (use_loop_fallback) {
		if (!PataWaitRetry(&hd, hd.fn_lup_wait, STATUS_BSY, 0)) {
			outpb(state.bmide_base + BMIDE_REG_CMD, 0);
			return false;
		}
	}
	else if (!hd.fn_lup_wait(&hd, STATUS_BSY, 0, HD_TIMEOUT / 1000)) {
		outpb(state.bmide_base + BMIDE_REG_CMD, 0);
		return false;
	}

	outpb(state.bmide_base + BMIDE_REG_CMD, 0);
	const byte bmide_status = innpb(state.bmide_base + BMIDE_REG_STATUS);
	outpb(state.bmide_base + BMIDE_REG_STATUS, bmide_status & (BMIDE_STATUS_IRQ | BMIDE_STATUS_ERROR));
	if (bmide_status & BMIDE_STATUS_ERROR) return false;
	if (!use_loop_fallback && !(bmide_status & BMIDE_STATUS_IRQ)) return false;
	return true;
}

static bool hd_read_prefer_dma(byte disk_id, stduint BlockIden, void* Dest) {
	if (disk_id >= numsof(disks) || !disks[disk_id] || !Dest) return false;
	auto& hd = *disks[disk_id];
	static bool logged_fallback[numsof(disks)] = {};
	if (hd.Block_Size == 512) {
		if (hd_read_dma_once(hd, BlockIden, Dest)) {
			// static bool logged_dma[numsof(disks)] = {};
			// if (!logged_dma[disk_id]) {
			// 	logged_dma[disk_id] = true;
			// 	ploginfo("[PATA-DMA] live dma-read on ide%u:%u",
			// 		(stduint)hd.getHigID(), (stduint)hd.getLowID());
			// }
			return true;
		}
		if (!logged_fallback[disk_id]) {
			logged_fallback[disk_id] = true;
			plogwarn("[PATA-DMA] fallback to PIO for ide%u:%u reads",
				(stduint)hd.getHigID(), (stduint)hd.getLowID());
		}
	}
	return hd.Read(BlockIden, Dest);
}

static bool hd_write_prefer_dma(byte disk_id, stduint BlockIden, const void* Sors) {
	if (disk_id >= numsof(disks) || !disks[disk_id] || !Sors) return false;
	auto& hd = *disks[disk_id];
	static bool logged_fallback[numsof(disks)] = {};
	if (hd.Block_Size == 512) {
		if (hd_write_dma_once(hd, BlockIden, Sors)) {
			// static bool logged_dma[numsof(disks)] = {};
			// if (!logged_dma[disk_id]) {
			// 	logged_dma[disk_id] = true;
			// 	ploginfo("[PATA-DMA] live dma-write on ide%u:%u",
			// 		(stduint)hd.getHigID(), (stduint)hd.getLowID());
			// }
			return true;
		}
		if (!logged_fallback[disk_id]) {
			logged_fallback[disk_id] = true;
			plogwarn("[PATA-DMA] fallback to PIO for ide%u:%u writes",
				(stduint)hd.getHigID(), (stduint)hd.getLowID());
		}
	}
	return hd.Write(BlockIden, Sors);
}

static void probe_pata_dma_read_once() {
	static bool probed = false;
	if (probed) return;
	probed = true;

	if (!disks[0] || disks[0]->Block_Size != 512) {
		// ploginfo("[PATA-DMA] skip dma-read probe: ide0:0 unavailable");
		return;
	}
	if (!single_sector) {
		// ploginfo("[PATA-DMA] skip dma-read probe: no sector buffer");
		return;
	}

	const bool ok = hd_read_dma_once(*disks[0], 0, single_sector);
	if (!ok) {
		plogwarn("[PATA-DMA] dma-read probe ide0:0 lba0 failed");
		return;
	}
	// ploginfo("[PATA-DMA] dma-read probe ide0:0 lba0 ok sig=%02X%02X",
	// 	(unsigned)single_sector[511], (unsigned)single_sector[510]);
	// ploginfo("[PATA-DMA] dma-read probe bytes=%02X %02X %02X %02X %02X %02X %02X %02X",
	// 	(unsigned)single_sector[0], (unsigned)single_sector[1],
	// 	(unsigned)single_sector[2], (unsigned)single_sector[3],
	// 	(unsigned)single_sector[4], (unsigned)single_sector[5],
	// 	(unsigned)single_sector[6], (unsigned)single_sector[7]);
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
	byte low_id = hd.getLowID();
	HdiskCommand cmd = {};
	cmd.command = (hd.Block_Size == 2048) ? ATAPI_CMD_IDENTIFY : ATA_IDENTIFY;
	cmd.device = MAKE_DEVICE_REG(0, low_id, 0);
	lock[hd.getHigID()] = 0;
	hd.Hdisk_OUT(&cmd);
	hd_int_wait();
	word io_base = hd.getHigID() == 0 ? PORT_IDE_CommandBlock_0 : PORT_IDE_CommandBlock_1;
	IN_wn(io_base + REG_DATA, (word*)single_sector, 512); // IDENTIFY payload is always 512 bytes
	print_identify_info((uint16*)single_sector, hd);
	if (!hd_info_valid[hd.getHigID() * 2 + low_id]) {
		// if (_TEMP hd.getID() == 0x01) {
		// DiscPartition dpart(hd, NR_PRIM_PER_DRIVE * low_id);
		HD_Info& hdi = (*hd_info)[hd.getHigID() * 2 + low_id];
		if (hd.Block_Size == 512) { // Skip MBR mounting for ATAPI CD-ROMs
			DiscPartition::Partition(hd, hdi, (byte*)single_sector, NR_PRIM_PER_DRIVE * low_id);
		}
		// if (1) print_hdinfo(hd);
		hd_info_valid[hd.getHigID() * 2 + low_id] = true;
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
		slice.sys_id = 0;
		return slice;
	}
	HD_Info& hdinfo = (*hd_info)[getHigID() * 2 + getLowID()];
	return GetPartitionSlice(hdinfo, dev);
}

struct Harddisk_PATA_Paged : public uni::Harddisk_PATA {
	Harddisk_PATA_Paged(byte _id = 0, HarddiskType type = HarddiskType::ATA) : Harddisk_PATA(_id, type) {}
	virtual bool Read(stduint BlockIden, void* Dest);
	virtual bool Write(stduint BlockIden, const void* Sors);
};
bool Harddisk_PATA_Paged::Read(stduint BlockIden, void* Dest) {
	if (Taskman::CurrentPID() == Task_Hdd_Serv) {
		if (!disks[getID()]) return false;
		return disks[getID()]->Read(BlockIden, Dest);
	}
	stduint to_args[2];
	to_args[0] = getID();
	to_args[1] = BlockIden;
	syssend(Task_Hdd_Serv, sliceof(to_args), _IMM(FiledevMsg::READ));
	// Receive ACK before data transfer
	stduint ack;
	sysrecv(Task_Hdd_Serv, &ack, sizeof(ack));
	if (!ack) return false;
	sysrecv(Task_Hdd_Serv, Dest, Block_Size);
	return true;
}

bool Harddisk_PATA_Paged::Write(stduint BlockIden, const void* Sors) {
	if (Taskman::CurrentPID() == Task_Hdd_Serv) {
		if (!disks[getID()]) return false;
		return disks[getID()]->Write(BlockIden, Sors);
	}
	stduint to_args[2];
	to_args[0] = getID();
	to_args[1] = BlockIden;
	syssend(Task_Hdd_Serv, sliceof(to_args), _IMM(FiledevMsg::WRITE));
	// Receive ACK before data transfer
	stduint ack;
	sysrecv(Task_Hdd_Serv, &ack, sizeof(ack));
	if (!ack) return false;
	syssend(Task_Hdd_Serv, Sors, Block_Size);
	return true;
}

//// ---- ---- SERVICE ---- ---- ////
static stduint args[4];
Harddisk_PATA_Paged* paged_disks[MAX_DRIVES];

static void log_read_disk() {
	static stduint last_sec = 0;
	if (args[1] != last_sec + 1) {
		plogwarn("PATA%u Read %u -> %p", args[0], args[1], single_sector);
	}
	last_sec = args[1];
}

int fat_time = 0;
void serv_dev_hd_loop()
{
	if (_IMM(&bda->hdisk_number) != 0x475) {
		plogerro("Invalid BIOS_DataArea");
		while (1);
	}
	hd_info = new HD_Info[1][4];

	for0a(i, disks) {
		if (disks[i]) {
			disks[i]->fn_cmd_wait = hd_cmd_wait;
			disks[i]->fn_int_wait = hd_int_wait;
			disks[i]->fn_lup_wait = waitfor;
			disks[i]->fn_feedback = (disks[i]->getHigID() == 0) ? hd_rw_foreback_0 : hd_rw_foreback_1;
			disks[i]->react_type = Harddisk_PATA::ReactType::Rupt;
		}
	}
	// Initialize the paged proxy disks
	for0a(i, paged_disks) {
		paged_disks[i] = new Harddisk_PATA_Paged(i);
		if (disks[i]) {
			paged_disks[i]->Block_Size = disks[i]->Block_Size;
		}
	}
	ensure_pata_pci_bus_master_once();
	probe_pata_bus_master_capability_once();
	initialize_pata_dma_channels_once();
	probe_pata_dma_read_once();
	
	// Console.OutFormat("[Hrddisk] detect %u disks\n\r", bda->hdisk_number);
	stduint sig_type = 0, sig_src;
	String lab_fat;
	String lab;
	while (true) {
		switch ((FiledevMsg)sig_type)
		{
		case FiledevMsg::TEST:// (no-feedback)
			for0(i, MAX_DRIVES) {
				if (disks[i]) {
					hd_open(*disks[i]);
					stduint total_units = 0;
					if (disks[i]->Block_Size == 2048 && paged_disks[i]) {
						total_units = paged_disks[i]->getSlice(0).length;
					} else {
						total_units = disks[i]->getUnits();
					}
					ploginfo("[Hrddisk] Detect %s on IDE%u:%u : %u MB",
						(disks[i]->Block_Size == 2048) ? "CD-ROM" : "Disk",
						i / 2, i % 2,
						_IMM(total_units * disks[i]->Block_Size) >> 20);
				}
			}

			for0(i, MAX_DRIVES) {
				if (!disks[i] || disks[i]->Block_Size != 2048) continue;
				lab = String::newFormat("/mnt/ide%u.0", i);
				ploginfo("[Hrddisk] probe cdrom%u whole-disk", i);
				if (auto fs = Filesys::Mount(*paged_disks[i], 0, lab.reference())) {
					ploginfo("[Hrddisk] mount %s on %s", fs->name, lab.reference());
				} else {
					ploginfo("[Hrddisk] no filesystem recognized on cdrom%u", i);
				}
			}

			for0(i, MAX_DRIVES) {
				if (!disks[i] || disks[i]->Block_Size != 512) continue;
				HD_Info& hdinfo = (*hd_info)[i];
				for (unsigned part_dev = 1; part_dev <= hdinfo.part_count; part_dev++) {
					uni::PartitionSlice slice = GetPartitionSlice(hdinfo, part_dev);
					byte sys_id = slice.sys_id;
					if (sys_id == 0x00) continue; // empty/unpartitioned, skip
					else if (sys_id == FILESYS_EXT) continue;
					lab = String::newFormat("/mnt/ide%u.%u", i, part_dev);
					ploginfo("[Hrddisk] probe disk%u part%u: %x", i, part_dev, sys_id);
					if (auto fs = Filesys::Mount(*paged_disks[i], part_dev, lab.reference())) {
						if (!StrCompare(fs->name, "fat")) {
							fat_time++;
						}
					}
				}
			}
			break;
		case FiledevMsg::RUPT:// (usercall-forbidden, no feedback)
			break;
		case FiledevMsg::CLOSE:// [diskno]
			ploginfo("[Hrddisk] close %u", (unsigned)(byte)args[0]);
			hd_close(*disks[(byte)args[0]]);// assert msg.data.length == 0
			break;
		case FiledevMsg::READ:// [diskno, lba]
		{
			// ploginfo("[Hrddisk] device %u: read %u", args[0], args[1]);
			// log_read_disk();

			stduint ack = hd_read_prefer_dma((byte)args[0], args[1], single_sector) ? 1 : 0;
			if (sig_src) syssend(sig_src, &ack, sizeof(ack));
			if (ack && sig_src) syssend(sig_src, single_sector, disks[args[0]]->Block_Size);
			break;
		}
		case FiledevMsg::WRITE:// [diskno, lba]
		{
			// ploginfo("[Hrddisk] device %u: write %u", (unsigned)(byte)args[0], slice.address);
			stduint ack = (args[0] < numsof(disks) && disks[args[0]]) ? 1 : 0;
			if (sig_src) syssend(sig_src, &ack, sizeof(ack));
			if (ack && sig_src) {
				sysrecv(sig_src, single_sector, disks[args[0]]->Block_Size);
				ack = hd_write_prefer_dma((byte)args[0], args[1], single_sector) ? 1 : 0;
			}
			break;
		}
		// OPEN: not support for fixed sockets

		default:
			plogerro("Bad TYPE in %s %s", __FILE__, __FUNCIDEN__);
			break;
		}
		sysrecv(ANYPROC, sliceof(args), &sig_type, &sig_src);
	}
}


#endif
