// ASCII g++ TAB4 LF
// ModuTitle: Disk - AHCI
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#if _MCCA == 0x8632
#include <cpp/Device/Bus/PCI.hpp>
#include <c/storage/harddisk.h>
#include <c/format/filesys.h>

namespace {
	uni::PCI pci;
	static constexpr uint8 IRQ_AHCI = IRQ_RTC + 2;
	bool ahci_irq_wait(uni::AHCI_Port_Base* port_base, uint32 slot_mask);

	struct AHCI_Controller {
		DeviceNode* node = nullptr;
		DeviceNode* storage_node = nullptr;
		DeviceNode* cdrom_node = nullptr;
		volatile AHCI_MEM* abar = nullptr;
		uint8 irq_line = 0xFF;
		uint8 irq_pin = 0;
		int active_port = -1;
		int active_atapi_port = -1;
		byte* cmd_list = nullptr;
		byte* rx_fis = nullptr;
		byte* cmd_table = nullptr;
		byte* identify_buf = nullptr;
		byte* sector_buf = nullptr;
		byte* cd_cmd_list = nullptr;
		byte* cd_rx_fis = nullptr;
		byte* cd_cmd_table = nullptr;
		byte* cd_identify_buf = nullptr;
		byte* cd_capacity_buf = nullptr;
		byte* cd_sector_buf = nullptr;
		uni::Harddisk_SATA_AHCI disk = {};
		uni::CDROM_ATAPI_AHCI cdrom = {};
		volatile uint32 irq_global_is = 0;
		volatile uint32 irq_port_is = 0;
		volatile byte irq_waiting = 0;
		volatile byte irq_seen = 0;

		bool Bind(DeviceNode* ahci_node) {
			if (!ahci_node) return false;
			auto* mmio = Devsman::FindResource(ahci_node, DeviceResourceType::PciBarMmio, 5);
			if (!mmio) return false;
			node = ahci_node;
			abar = reinterpret_cast<volatile AHCI_MEM*>(stduint(mmio->start));
			if (auto* irq = Devsman::FindResource(ahci_node, DeviceResourceType::IrqLine, 0)) {
				irq_line = uint8(irq->start);
				irq_pin = uint8(irq->extra);
			}
			return abar != nullptr;
		}

		bool AllocateBuffers() {
			if (!cmd_list) cmd_list = (byte*)mempool.allocate(1024, 10);
			if (!rx_fis) rx_fis = (byte*)mempool.allocate(256, 8);
			if (!cmd_table) cmd_table = (byte*)mempool.allocate(256, 7);
			if (!identify_buf) identify_buf = (byte*)mempool.allocate(512, 9);
			if (!sector_buf) sector_buf = (byte*)mempool.allocate(512, 9);
			return cmd_list && rx_fis && cmd_table && identify_buf && sector_buf;
		}

		bool AllocateCdromBuffers() {
			if (!cd_cmd_list) cd_cmd_list = (byte*)mempool.allocate(1024, 10);
			if (!cd_rx_fis) cd_rx_fis = (byte*)mempool.allocate(256, 8);
			if (!cd_cmd_table) cd_cmd_table = (byte*)mempool.allocate(256, 7);
			if (!cd_identify_buf) cd_identify_buf = (byte*)mempool.allocate(512, 9);
			if (!cd_capacity_buf) cd_capacity_buf = (byte*)mempool.allocate(512, 9);
			if (!cd_sector_buf) cd_sector_buf = (byte*)mempool.allocate(2048, 11);
			return cd_cmd_list && cd_rx_fis && cd_cmd_table &&
				cd_identify_buf && cd_capacity_buf && cd_sector_buf;
		}

		void DumpSummary() const {
			if (!node || !abar) return;
			// ploginfo("[AHCI] ctlr %02x:%02x.%u ABAR=%p IRQ=%u pin=%u",
			// 	(unsigned)node->fields.pci_bus,
			// 	(unsigned)node->fields.pci_device,
			// 	(unsigned)node->fields.pci_function,
			// 	(void*)abar,
			// 	(unsigned)irq_line,
			// 	(unsigned)irq_pin);
			// ploginfo("[AHCI] cap=%[32H] ghc=%[32H] is=%[32H] pi=%[32H] vs=%[32H]",
			// 	abar->cap, abar->ghc, abar->is, abar->pi, abar->vs);
		}

		void DumpPorts() const {
			if (!abar) return;
			const uint32 implemented = abar->pi;
			for0(port, 32) {
				if (((implemented >> port) & 1u) == 0) continue;
				const volatile AHCI_PORT& regs = abar->ports[port];
				if (regs.sig == 0xFFFFFFFFu && regs.ssts == 0) continue;
				// ploginfo("[AHCI] port%u sig=%[32H] ssts=%[32H] cmd=%[32H] tfd=%[32H]",
				// 	(unsigned)port, regs.sig, regs.ssts, regs.cmd, regs.tfd);
			}
		}

		void SelectPorts() {
			active_port = uni::Harddisk_SATA_AHCI::SelectFirstPort(abar);
			active_atapi_port = -1;
			for0(port, 32) {
				if (uni::CDROM_ATAPI_AHCI::IsCandidatePort(abar, port)) {
					active_atapi_port = port;
					break;
				}
			}
		}

		void SetupDisk() {
			disk.Bind(abar, active_port);
			if (cmd_list && rx_fis && cmd_table) {
				disk.SetWorkspace(cmd_list, rx_fis, cmd_table);
			}
			disk.fn_irq_wait = nullptr;
			if (irq_line == 10) {
				disk.fn_irq_wait = ahci_irq_wait;
				abar->ghc |= AHCI_GHC_IE | AHCI_GHC_AE;
				abar->ports[active_port].ie = 0xFFFFFFFFu;
			}
		}

		void SetupCdrom() {
			cdrom.Bind(abar, active_atapi_port);
			if (cd_cmd_list && cd_rx_fis && cd_cmd_table) {
				cdrom.SetWorkspace(cd_cmd_list, cd_rx_fis, cd_cmd_table);
			}
			cdrom.fn_irq_wait = nullptr;
			if (irq_line == 10) {
				cdrom.fn_irq_wait = ahci_irq_wait;
				abar->ghc |= AHCI_GHC_IE | AHCI_GHC_AE;
				abar->ports[active_atapi_port].ie = 0xFFFFFFFFu;
			}
		}

		void RegisterStorageNode() {
			if (!node || active_port < 0) return;
			String name = String::newFormat("ahci-disk@%u", (stduint)active_port);
			storage_node = Devsman::RegisterStorageDevice(node, name.reference(),
				DeviceBusType::PCI, "ahci-disk", &disk);
		}

		void RegisterCdromNode() {
			if (!node || active_atapi_port < 0) return;
			String name = String::newFormat("ahci-cdrom@%u", (stduint)active_atapi_port);
			cdrom_node = Devsman::RegisterStorageDevice(node, name.reference(),
				DeviceBusType::PCI, "ahci-cdrom", &cdrom);
		}

		bool PrepareProbeBuffers() {
			if (!AllocateBuffers()) {
				plogwarn("[AHCI] port%d buffer allocation failed", active_port);
				return false;
			}
			disk.SetWorkspace(cmd_list, rx_fis, cmd_table);
			MemSet(identify_buf, 0, 512);
			MemSet(sector_buf, 0, 512);
			return true;
		}

		bool PrepareCdromProbeBuffers() {
			if (!AllocateCdromBuffers()) {
				plogwarn("[AHCI] atapi port%d buffer allocation failed", active_atapi_port);
				return false;
			}
			cdrom.SetWorkspace(cd_cmd_list, cd_rx_fis, cd_cmd_table);
			MemSet(cd_identify_buf, 0, 512);
			MemSet(cd_capacity_buf, 0, 512);
			MemSet(cd_sector_buf, 0, 2048);
			return true;
		}

		bool IdentifyCdrom() {
			if (active_atapi_port < 0 || !PrepareCdromProbeBuffers()) return false;
			if (!cdrom.IdentifyPacket(cd_identify_buf)) {
				auto& port = abar->ports[active_atapi_port];
				plogwarn("[AHCI] atapi port%d IDENTIFY PACKET failed is=%[32H] tfd=%[32H]",
					active_atapi_port, port.is, port.tfd);
				return false;
			}
			return true;
		}

		bool ReadCdromCapacity() {
			if (active_atapi_port < 0) return false;
			if (!cdrom.ReadCapacity(cd_capacity_buf)) {
				auto& port = abar->ports[active_atapi_port];
				plogwarn("[AHCI] atapi port%d READ CAPACITY failed is=%[32H] tfd=%[32H]",
					active_atapi_port, port.is, port.tfd);
				return false;
			}
			cdrom.UpdateCapacity(cd_capacity_buf);
			// ploginfo("[AHCI] atapi port%d blocks=%u block_size=%u",
			// 	active_atapi_port, cdrom.getUnits(), cdrom.Block_Size);
			return cdrom.getUnits() != 0;
		}

		void MountCdromWholeDisk() {
			if (active_atapi_port < 0) return;
			String lab = String::newFormat("/mnt/ahci%u.0", (stduint)active_atapi_port);
			// ploginfo("[AHCI] probe atapi port%u whole-disk", (stduint)active_atapi_port);
			if (auto fs = Filesys::Mount(cdrom, 0, lab.reference())) {
				ploginfo("[AHCI] mount %s on %s", fs->name, lab.reference());
			}
			else {
				plogwarn("[AHCI] no filesystem recognized on atapi port%u",
					(stduint)active_atapi_port);
			}
		}

		bool IdentifyActivePort() {
			if (active_port < 0 || !PrepareProbeBuffers()) return false;
			if (!disk.Identify(identify_buf)) {
				auto& port = abar->ports[active_port];
				plogwarn("[AHCI] port%d IDENTIFY failed is=%[32H] tfd=%[32H]",
					active_port, port.is, port.tfd);
				return false;
			}
			return true;
		}

		bool ReadSector(uint64 lba, byte* data_buf) {
			if (active_port < 0 || !data_buf) return false;
			if (!disk.ReadSectors(lba, data_buf, 1)) {
				auto& port = abar->ports[active_port];
				plogwarn("[AHCI] port%d READ LBA=%[64H] failed is=%[32H] tfd=%[32H]",
					active_port, lba, port.is, port.tfd);
				return false;
			}
			return true;
		}

		bool ReadSector0() {
			return ReadSector(0, sector_buf);
		}

		void DumpIdentify() const {
			// char model[41] = {};
			// disk.GetModel(model, identify_buf);
			// const uint64 sectors = disk.total_sectors;
			// const uint64 mib = (sectors * 512ull) >> 20;
			// ploginfo("[AHCI] IDENTIFY port%d model=\"%s\" sectors=%[64H] size=%[64H]MiB",
			// 	active_port, model, sectors, mib);
		}

		void DumpSector0() const {
			// ploginfo("[AHCI] LBA0 %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
			// 	(unsigned)sector_buf[0], (unsigned)sector_buf[1],
			// 	(unsigned)sector_buf[2], (unsigned)sector_buf[3],
			// 	(unsigned)sector_buf[4], (unsigned)sector_buf[5],
			// 	(unsigned)sector_buf[6], (unsigned)sector_buf[7],
			// 	(unsigned)sector_buf[8], (unsigned)sector_buf[9],
			// 	(unsigned)sector_buf[10], (unsigned)sector_buf[11],
			// 	(unsigned)sector_buf[12], (unsigned)sector_buf[13],
			// 	(unsigned)sector_buf[14], (unsigned)sector_buf[15]);
			// ploginfo("[AHCI] LBA0 signature=%02X%02X",
			// 	(unsigned)sector_buf[511], (unsigned)sector_buf[510]);
		}

		void UpdatePortDiskIdentify() {
			disk.UpdateIdentity(identify_buf);
		}

		void ParsePartitions() {
			if (disk.hd_info_valid) return;
			DiscPartition::Partition(disk, disk.hd_info, sector_buf, 0);
			disk.hd_info_valid = true;
		}

		void DumpPartitions() const {
			// const auto& hdinfo = disk.hd_info;
			// auto end_lba = [](const uni::PartitionSlice& slice) -> stduint {
			// 	return slice.length ? slice.address + slice.length - 1 : slice.address;
			// };
			// ploginfo("[AHCI] whole 0..%u", end_lba(hdinfo.whole_disk));
			// ploginfo("[AHCI] parts %u (+%u overflow)", hdinfo.part_count, hdinfo.part_overflow);
			// for0(i, hdinfo.part_count) {
			// 	auto part = hdinfo.parts[i];
			// 	ploginfo("[AHCI] part%u %u..%u type=%[8H]",
			// 		i + 1, part.address, end_lba(part), (unsigned)part.sys_id);
			// }
		}

		void MountPartitions() {
			auto& hdinfo = disk.hd_info;
			for (stduint part_dev = 1; part_dev <= hdinfo.part_count; ++part_dev) {
				uni::PartitionSlice slice = GetPartitionSlice(hdinfo, part_dev);
				byte sys_id = slice.sys_id;
				if (sys_id == 0x00) continue;
				if (sys_id == FILESYS_EXT) continue;

				// /mnt/sataA.B
				String lab = String::newFormat("/mnt/ahci%u.%u", (stduint)active_port, part_dev);
				// ploginfo("[AHCI] probe port%u part%u: %x",
				// 	(stduint)active_port, part_dev, (unsigned)sys_id);
				if (auto fs = Filesys::Mount(disk, part_dev, lab.reference())) {
					ploginfo("[AHCI] mount %s on %s", fs->name, lab.reference());
				}
			}
		}
	};

	AHCI_Controller g_ahci_controller;

	bool ahci_irq_wait(uni::AHCI_Port_Base* port_base, uint32 slot_mask) {
		(void)slot_mask;
		if (!port_base || !g_ahci_controller.abar ||
			(port_base != static_cast<uni::AHCI_Port_Base*>(&g_ahci_controller.disk) &&
			 port_base != static_cast<uni::AHCI_Port_Base*>(&g_ahci_controller.cdrom))) return false;
		if (port_base->port_index < 0 || port_base->port_index >= 32) return false;
		auto& port = g_ahci_controller.abar->ports[port_base->port_index];
		g_ahci_controller.irq_seen = 0;
		g_ahci_controller.irq_waiting = 1;
		for (stduint spin = 0; spin < 5000000; ++spin) {
			if (g_ahci_controller.irq_seen) {
				g_ahci_controller.irq_waiting = 0;
				if (port.is & AHCI_PxIS_TFES) return false;
				return true;
			}
			if ((port.ci & 1u) == 0) {
				g_ahci_controller.irq_waiting = 0;
				if (port.is & AHCI_PxIS_TFES) return false;
				return true;
			}
		}
		g_ahci_controller.irq_waiting = 0;
		return true;
	}

	void Handint_AHCI() {
		if (g_ahci_controller.abar) {
			g_ahci_controller.irq_global_is = g_ahci_controller.abar->is;
			g_ahci_controller.irq_port_is = 0;
			if (g_ahci_controller.active_port >= 0) {
				auto& port = g_ahci_controller.abar->ports[g_ahci_controller.active_port];
				g_ahci_controller.irq_port_is |= port.is;
				port.is = 0xFFFFFFFFu;
			}
			if (g_ahci_controller.active_atapi_port >= 0) {
				auto& port = g_ahci_controller.abar->ports[g_ahci_controller.active_atapi_port];
				g_ahci_controller.irq_port_is |= port.is;
				port.is = 0xFFFFFFFFu;
			}
			g_ahci_controller.abar->is = g_ahci_controller.irq_global_is;
			if (g_ahci_controller.irq_waiting) {
				g_ahci_controller.irq_seen = 1;
			}
		}
		IC.SendEOI(IRQ_AHCI);
	}

	_ESYM_C void Handint_AHCI_Entry();

	static uni::PCI::Device make_pci_device(const DeviceNode& node) {
		uni::PCI::Device dev{};
		dev.bus = node.fields.pci_bus;
		dev.device = node.fields.pci_device;
		dev.function = node.fields.pci_function;
		dev.header_type = pci.read_header_type(dev.bus, dev.device, dev.function);
		dev.class_code.base = node.fields.class_base;
		dev.class_code.sub = node.fields.class_sub;
		dev.class_code.interface = node.fields.class_if;
		return dev;
	}
}

static bool start_ahci_driver(DeviceNode* ahci_node) {
	if (!ahci_node) return false;
	auto dev = make_pci_device(*ahci_node);
	pci.enable_MMIO(dev);
	if (!g_ahci_controller.Bind(ahci_node)) {
		plogwarn("[AHCI] Failed to bind controller %s",
			ahci_node->link.addr ? ahci_node->link.addr : "(unnamed)");
		return false;
	}
	g_ahci_controller.DumpSummary();
	g_ahci_controller.DumpPorts();
	g_ahci_controller.SelectPorts();
	if (g_ahci_controller.active_port >= 0) {
		g_ahci_controller.SetupDisk();
		g_ahci_controller.RegisterStorageNode();
		if (g_ahci_controller.irq_line != 10) {
			plogwarn("[AHCI] IRQ line %u not wired for interrupt completion, keep polling",
				(unsigned)g_ahci_controller.irq_line);
		}
		// ploginfo("[AHCI] select port%d", g_ahci_controller.active_port);
		if (g_ahci_controller.IdentifyActivePort()) {
			g_ahci_controller.UpdatePortDiskIdentify();
			g_ahci_controller.DumpIdentify();
			if (g_ahci_controller.ReadSector0()) {
				g_ahci_controller.DumpSector0();
				g_ahci_controller.ParsePartitions();
				g_ahci_controller.DumpPartitions();
				g_ahci_controller.MountPartitions();
			}
		}
	}
	else {
		plogwarn("[AHCI] No active ATA port found.");
	}
	if (g_ahci_controller.active_atapi_port >= 0) {
		g_ahci_controller.SetupCdrom();
		g_ahci_controller.RegisterCdromNode();
		// ploginfo("[AHCI] select atapi port%d", g_ahci_controller.active_atapi_port);
		if (g_ahci_controller.IdentifyCdrom() &&
			g_ahci_controller.ReadCdromCapacity()) {
			g_ahci_controller.MountCdromWholeDisk();
		}
	}
	ahci_node->fields.binding.driver_data = &g_ahci_controller;
	return true;
}

_ESYM_C void R_AHCI_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_AHCI{
	.init = R_AHCI_INIT,
	.name = "AHCI",
};

void R_AHCI_INIT() {
	if (!PCI_Init(pci)) {
		plogwarn("[AHCI] No devices on PCI or PCI init failed.");
	}
	IC[IRQ_AHCI].setRange(mglb(Handint_AHCI_Entry), SegCo32);
	register_interrupt_handler(IRQ_AHCI, Handint_AHCI);
	Devsman::RegisterDriverStarter("ahci", start_ahci_driver);
	Devsman::StartKnownDrivers();
}

#endif
