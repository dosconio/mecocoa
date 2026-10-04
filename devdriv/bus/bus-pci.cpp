// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] Device Management
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"
#include <cpp/Device/Bus/ISA.hpp>
#include <cpp/Device/Bus/PCI.hpp>

#if (_MCCA & 0xFF00) == 0x8600
#include "../../include/devsman-storage.hpp"
#include "c/proctrl/IAx86_64.ext.h"
#include "c/proctrl/IAx86_64.msr.h"

namespace Devs {
	enum class PciDriverKind : uint8 {
		Unknown = 0,
		Scsi,
		Pata,
		Ahci,
		Nvme,
		PciBridge,
		Xhci,
		E1000,
		Piix4Acpi,
		Lance,
	};

	struct PciClassNameEntry {
		uint8 class_base;
		uint8 class_sub;
		uint8 class_if;
		const char* name;
	};

	struct PciDeviceNameEntry {
		uint16 vendor_id;
		uint16 device_id;
		const char* product_name;
	};

	constexpr uint8 MatchAnyClassIf = 0xFF;

	constexpr PciClassNameEntry pci_class_name_table[] = {
		{0x01u, 0x00u, MatchAnyClassIf, "SCSI storage controller"},
		{0x01u, 0x01u, 0x8Au, "ISA Compatibility IDE controller"},
		{0x01u, 0x01u, MatchAnyClassIf, "IDE interface"},
		{0x01u, 0x06u, 0x01u, "AHCI SATA controller"},
		{0x01u, 0x06u, MatchAnyClassIf, "SATA controller"},
		{0x01, 0x08u, 0x02u, "NVM controller"},
		{0x02u, 0x00u, MatchAnyClassIf, "Ethernet controller"},
		{0x03u, 0x00u, MatchAnyClassIf, "VGA compatible controller"},
		{0x06u, 0x00u, MatchAnyClassIf, "Host bridge"},
		{0x06u, 0x01u, MatchAnyClassIf, "ISA bridge"},
		{0x06u, 0x04u, MatchAnyClassIf, "PCI bridge"},
		{0x06u, 0x80u, MatchAnyClassIf, "Bridge device"},
		{0x08u, 0x80u, MatchAnyClassIf, "System peripheral"},
	};

	constexpr PciDeviceNameEntry pci_device_name_table[] = {
		{0x8086u, 0x1237u, "82441FX PMC host bridge"},// PCI and Memory Controller
		{0x8086u, 0x7190u, "440BX/ZX/DX - 82443BX/ZX/DX Host bridge"},
		{0x8086u, 0x7000u, "82371SB PIIX3 ISA bridge"},
		{0x8086u, 0x7110u, "82371AB PIIX4 ISA bridge"},
		{0x8086u, 0x7111u, "82371AB PIIX4 IDE"},
		{0x8086u, 0x7113u, "82371AB/EB/MB PIIX4 ACPI"},

		// ETH
		{0x8086u, 0x100Eu, "82540EM Gigabit Ethernet Controller"},
		{0x8086u, 0x100Fu, "82545EM Gigabit Ethernet Controller"},
		{0x8086u, 0x1010u, "82546EB Gigabit Ethernet Controller"},
		{0x8086u, 0x10D3u, "82574L Gigabit Network Connection"},
		{0x1022u, 0x2000u, "PCnet-PCI II Ethernet Controller"},

		{0x104Bu, 0x1040u, "MultiMaster SCSI host adapter"},
		{0x15ADu, 0x0405u, "SVGA II Adapter"},
		{0x15ADu, 0x0740u, "virtual machine communication interface"},
		{0x15ADu, 0x0790u, "PCI bridge"},
		{0x1234u, 0x1111u, "VGA adapter"},
	};


	DeviceNode* pci_root = nullptr;
	DeviceNode* primary_pci_bus = nullptr;
	DeviceNode* pci_bus_nodes[256]{};
	bool pci_devices_attached = false;

}

namespace Devs {
	void reparent_secondary_buses_under_bridges();
	DeviceNode* attach_isa_bus_if_bridge(DeviceNode* pci_node);
	void initialize_device_tree();
	bool set_driver_probe_state(DeviceNode* node, DriverBindingState state, int32 result);
	Devsman::DriverStartRoutine find_driver_starter(const char* driver_name);
	void append_child(DeviceNode* parent, DeviceNode* child);
	void init_node(DeviceNode* node, DeviceNodeType node_type, DeviceBusType bus_type, const char* name);
	const char* heap_name(const char* fmt, stduint a0, stduint a1 = 0, stduint a2 = 0, stduint a3 = 0);// release?
	const DriverOpsEntry* find_driver_ops(const char* driver_name, const DriverOpsEntry* table, stduint count);
	bool append_resource(DeviceNode* node, DeviceResourceType type, uint16 flags, uint32 index, uint64 start, uint64 length, uint64 extra);
	bool is_e1000_device(const DeviceNode* node);
	bool is_lance_pci_device(const DeviceNode* node);
	bool set_driver_binding(DeviceNode* node, const char* driver_name);

	extern DeviceTree device_tree;
}

//
namespace Devs {
	DeviceNode* create_pci_bus_node(uint16 segment, uint8 bus) {
		auto* bus_node = device_tree.NewNode();
		init_node(bus_node, DeviceNodeType::PciBus, DeviceBusType::PCI,
			heap_name("pci-bus@%04x:%02x", segment, bus));
		bus_node->fields.pci_segment = segment;
		bus_node->fields.pci_bus = bus;
		append_child(pci_root, bus_node);
		pci_bus_nodes[bus] = bus_node;
		return bus_node;
	}

	DeviceNode* ensure_pci_bus_node(uint16 segment, uint8 bus) {
		if (pci_bus_nodes[bus]) return pci_bus_nodes[bus];
		return create_pci_bus_node(segment, bus);
	}
}

// probe
namespace Devs {

	void probe_and_attach_pci_devices() {
		if (pci_devices_attached) return;
		uni::PCI pci;
		if (!PCI_Init(pci)) {
			plogwarn("[DEVSMAN] No devices on PCI or PCI init failed.");
			return;
		}
		Devsman::AttachPCIDevices(pci);
	}

	bool probe_xhci_device(DeviceNode* node) {
		if (!node) return false;
		const auto* mmio = Devsman::FindResource(node, DeviceResourceType::PciBarMmio, 0);
		if (!mmio) return false;
		const auto* irq = Devsman::FindResource(node, DeviceResourceType::IrqLine, 0);
		node->fields.binding.probe_result = irq ? 0 : 1;
		return true;
	}

	bool probe_ahci_device(DeviceNode* node) {
		if (!node) return false;
		const auto* abar = Devsman::FindResource(node, DeviceResourceType::PciBarMmio, 5);
		if (!abar) {
			plogwarn("[DEVSMAN] AHCI %s missing BAR5 ABAR resource",
				node->link.addr ? node->link.addr : "(unnamed)");
			return false;
		}
		const auto* irq = Devsman::FindResource(node, DeviceResourceType::IrqLine, 0);
		node->fields.binding.probe_result = irq ? 0 : 1;
		ploginfo("[DEVSMAN] AHCI %s ABAR=%[64H]%s",
			node->link.addr ? node->link.addr : "(unnamed)",
			abar->start,
			irq ? "" : " irq=none");
		return true;
	}

	bool probe_nvme_device(DeviceNode* node) {
		if (!node) return false;
		const auto* bar0 = Devsman::FindResource(node, DeviceResourceType::PciBarMmio, 0);
		if (!bar0) {
			plogwarn("[DEVSMAN] NVMe %s missing BAR0 resource",
				node->link.addr ? node->link.addr : "(unnamed)");
			return false;
		}
		const auto* irq = Devsman::FindResource(node, DeviceResourceType::IrqLine, 0);
		node->fields.binding.probe_result = irq ? 0 : 1;
		ploginfo("[DEVSMAN] NVMe %s BAR0=%[64H]%s",
			node->link.addr ? node->link.addr : "(unnamed)",
			bar0->start,
			irq ? "" : " irq=none");
		return true;
	}

	bool probe_scsi_device(DeviceNode* node) {
		if (!node) return false;
		const auto* io_bar = Devsman::FindResource(node, DeviceResourceType::PciBarIo, 0);
		const auto* mmio_bar = Devsman::FindResource(node, DeviceResourceType::PciBarMmio, 0);
		if (!io_bar && !mmio_bar) {
			plogwarn("[DEVSMAN] SCSI %s missing BAR0 resource",
				node->link.addr ? node->link.addr : "(unnamed)");
			return false;
		}
		const auto* irq = Devsman::FindResource(node, DeviceResourceType::IrqLine, 0);
		node->fields.binding.probe_result = irq ? 0 : 1;
		if (io_bar) {
			ploginfo("[DEVSMAN] SCSI %s IO=%[64H]%s",
				node->link.addr ? node->link.addr : "(unnamed)",
				io_bar->start,
				irq ? "" : " irq=none");
		}
		else {
			ploginfo("[DEVSMAN] SCSI %s MMIO=%[64H]%s",
				node->link.addr ? node->link.addr : "(unnamed)",
				mmio_bar->start,
				irq ? "" : " irq=none");
		}
		return true;
	}

	bool probe_pata_device(DeviceNode* node);
	bool probe_ahci_device(DeviceNode* node);
	bool probe_nvme_device(DeviceNode* node);
	bool probe_scsi_device(DeviceNode* node);
	bool probe_video_vmware_device(DeviceNode* node);
	bool probe_video_bochs_device(DeviceNode* node);
	bool probe_e1000_device(DeviceNode* node);
	bool probe_piix4_acpi_device(DeviceNode* node);
	bool probe_lance_device(DeviceNode* node);
	constexpr DriverOpsEntry pci_driver_ops_table[] = {
		{"xhci", probe_xhci_device},
		{"pata", probe_pata_device},
		{"ahci", probe_ahci_device},
		{"nvme", probe_nvme_device},
		{"scsi", probe_scsi_device},
		{"video-vmware", probe_video_vmware_device},
		{"video-bochs", probe_video_bochs_device},
		{"e1000", probe_e1000_device},
		{"piix4-acpi", probe_piix4_acpi_device},
		{"lance", probe_lance_device},
	};

	void probe_pci_device(DeviceNode* node) {
		if (!node) return;
		if (node->fields.binding.state != static_cast<uint32>(DriverBindingState::Matched)) return;
		const auto* ops = find_driver_ops(node->fields.binding.driver_name,
			pci_driver_ops_table, numsof(pci_driver_ops_table));
		if (!ops || !ops->probe) return;
		const bool ok = ops->probe(node);
		set_driver_probe_state(node, ok ? DriverBindingState::Probed : DriverBindingState::Failed, ok ? 0 : -1);
		ploginfo("[DEVSMAN] Probe %s -> %s", node->link.addr ? node->link.addr : "(unnamed)", ok ? "ok" : "failed");
	}
}

// isXXX
namespace Devs {
	bool is_bochs_video_device(const DeviceNode* node) {
		if (!node) return false;
		// QEMU/Bochs VGA vendor ID = 0x1234, device ID = 0x1111
		return node->fields.vendor_id == 0x1234 &&
			node->fields.device_id == 0x1111;
	}

	bool is_vmware_video_device(const DeviceNode* node) {
		if (!node) return false;
		return node->fields.vendor_id == 0x15AD &&
			node->fields.device_id == 0x0405;
	}

	//{TEMP}
	bool is_active_boot_gpu(const uni::PCI::Device& dev) {
		if (dev.class_code.base != 0x03u) return false;
		if (!sys_framebuffer.physical_range.address) return false;

		const uint8 header_type = dev.header_type & 0x7Fu;
		const uint8 bar_count = header_type == 0x01 ? 2 : 6;
		for (uint8 bar_index = 0; bar_index < bar_count; ++bar_index) {
			const uint8 addr = 0x10 + bar_index * 4;
			const uint32 bar_low = uni::PCI::read_config_register(dev, addr);
			if (!bar_low) continue;
			if (bar_low & 0x1u) continue; // Skip I/O BARs

			const uint8 mem_type = uint8((bar_low >> 1) & 0x3u);
			uint64 base = uint64(bar_low & ~0xFu);
			if (mem_type == 0x2u && bar_index + 1 < bar_count) {
				const uint32 bar_high = uni::PCI::read_config_register(dev, addr + 4);
				base |= uint64(bar_high) << 32;
				bar_index++;
			}

			// Check if active framebuffer address is within this BAR window
			if (base && sys_framebuffer.physical_range.address >= base &&
				sys_framebuffer.physical_range.address < base + 0x100000000ull) {
				return true;
			}
		}
		return false;
	}
}

namespace Devs {
	uint8 read_pci_revision_id(const uni::PCI::Device& dev) {
		return byte(uni::PCI::read_config_register(dev, 0x08));
	}

	uint8 read_pci_interrupt_pin(const uni::PCI::Device& dev) {
		return byte(uni::PCI::read_config_register(dev, 0x3C) >> 8);
	}

	uint8 read_pci_interrupt_line(const uni::PCI::Device& dev) {
		return byte(uni::PCI::read_config_register(dev, 0x3C));
	}

	PciDriverKind match_pci_driver(const DeviceNode* node) {
		if (!node) return PciDriverKind::Unknown;
		if (node->fields.class_base == 0x01u && node->fields.class_sub == 0x00u) {
			return PciDriverKind::Scsi;
		}
		if (node->fields.class_base == 0x01u && node->fields.class_sub == 0x01u) {
			return PciDriverKind::Pata;
		}
		if (node->fields.class_base == AHCI_PCI_CLASS_BASE &&
			node->fields.class_sub == AHCI_PCI_CLASS_SUB &&
			node->fields.class_if == AHCI_PCI_CLASS_IF) {
			return PciDriverKind::Ahci;
		}
		if (node->fields.class_base == NVME_PCI_CLASS_BASE &&
			node->fields.class_sub == NVME_PCI_CLASS_SUB &&
			node->fields.class_if == NVME_PCI_CLASS_IF) {
			return PciDriverKind::Nvme;
		}
		if (node->fields.class_base == 0x06u && node->fields.class_sub == 0x04u) {
			return PciDriverKind::PciBridge;
		}
		if (node->fields.class_base == 0x0Cu &&
			node->fields.class_sub == 0x03u &&
			node->fields.class_if == 0x30u) {
			return PciDriverKind::Xhci;
		}
		if (is_e1000_device(node)) {
			return PciDriverKind::E1000;
		}
		if (node->fields.vendor_id == 0x8086u &&
			node->fields.device_id == 0x7113u) {
			return PciDriverKind::Piix4Acpi;
		}
		if (is_lance_pci_device(node)) {
			return PciDriverKind::Lance;
		}
		return PciDriverKind::Unknown;
	}

	void bind_pci_device(DeviceNode* node) {
		if (!node) return;
		if (node->fields.class_base == 0x06u && node->fields.class_sub == 0x00u) {
			set_driver_binding(node, "host-bridge");
			return;
		}
		if (uni::ISA::IsBridgeDevice(node->fields.vendor_id, node->fields.device_id,
			node->fields.class_base, node->fields.class_sub)) {
			set_driver_binding(node, "isa-bridge");
			return;
		}
		if (node->fields.class_base == 0x03u && node->fields.class_sub == 0x00u) {
			if (is_vmware_video_device(node)) {
				set_driver_binding(node, "video-vmware");
				return;
			}
			if (is_bochs_video_device(node)) {
				set_driver_binding(node, "video-bochs");
			}
			return;
		}
		switch (match_pci_driver(node)) {
		case PciDriverKind::Scsi:
			set_driver_binding(node, "scsi");
			return;
		case PciDriverKind::Pata:
			set_driver_binding(node, "pata");
			return;
		case PciDriverKind::Ahci:
			set_driver_binding(node, "ahci");
			return;
		case PciDriverKind::Nvme:
			set_driver_binding(node, "nvme");
			return;
		case PciDriverKind::PciBridge:
			set_driver_binding(node, "pci-bridge");
			return;
		case PciDriverKind::Xhci:
			set_driver_binding(node, "xhci");
			return;
		case PciDriverKind::E1000:
			set_driver_binding(node, "e1000");
			return;
		case PciDriverKind::Piix4Acpi:
			set_driver_binding(node, "piix4-acpi");
			return;
		case PciDriverKind::Lance:
			set_driver_binding(node, "lance");
			return;
		default:
			return;
		}
	}

	const char* pci_resource_type_name(uint16 type) {
		switch (static_cast<DeviceResourceType>(type)) {
		case DeviceResourceType::PciBarMmio:
			return "BAR-MMIO";
		case DeviceResourceType::PciBarIo:
			return "BAR-IO";
		case DeviceResourceType::IoPortRange:
			return "IOPORT";
		case DeviceResourceType::IrqLine:
			return "IRQ";
		case DeviceResourceType::PciBridgeBusRange:
			return "BUS-RANGE";
		case DeviceResourceType::UsbLocation:
			return "USB-LOC";
		case DeviceResourceType::UsbEndpoint:
			return "USB-EP";
		case DeviceResourceType::DmaChannel:
			return "DMA";
		default:
			return "Unknown";
		}
	}

	void log_pci_resources(const DeviceNode* node) {
		for0(i, node->fields.resource_count) {
			const auto& res = node->fields.resources[i];
			switch (static_cast<DeviceResourceType>(res.type)) {
			case DeviceResourceType::PciBarMmio:
				ploginfo("[DEVSMAN]   %s[%u] base=%[64H] flags=%[16H]",
					pci_resource_type_name(res.type), res.index, res.start, res.flags);
				break;
			case DeviceResourceType::PciBarIo:
				ploginfo("[DEVSMAN]   %s[%u] base=%[64H]",
					pci_resource_type_name(res.type), res.index, res.start);
				break;
			case DeviceResourceType::IoPortRange:
				ploginfo("[DEVSMAN]   %s[%u] base=%[64H] len=%[64H]",
					pci_resource_type_name(res.type), res.index, res.start, res.length);
				break;
			case DeviceResourceType::IrqLine:
				ploginfo("[DEVSMAN]   %s line=%u pin=%u",
					pci_resource_type_name(res.type), (unsigned)res.start, (unsigned)res.extra);
				break;
			case DeviceResourceType::PciBridgeBusRange:
				ploginfo("[DEVSMAN]   %s primary=%u secondary=%u subordinate=%u",
					pci_resource_type_name(res.type),
					(unsigned)res.start,
					(unsigned)res.length,
					(unsigned)res.extra);
				break;
			case DeviceResourceType::UsbLocation:
				ploginfo("[DEVSMAN]   %s port=%u slot=%u",
					pci_resource_type_name(res.type),
					(unsigned)res.start,
					(unsigned)res.extra);
				break;
			case DeviceResourceType::UsbEndpoint:
				ploginfo("[DEVSMAN]   %s[%u] addr=%u type=%u mps=%u interval=%u",
					pci_resource_type_name(res.type),
					(unsigned)res.index,
					(unsigned)res.start,
					(unsigned)(res.extra & 0xFFu),
					(unsigned)res.length,
					(unsigned)((res.extra >> 8) & 0xFFu));
				break;
			default:
				ploginfo("[DEVSMAN]   %s[%u] start=%[64H] len=%[64H] extra=%[64H] flags=%[16H]",
					pci_resource_type_name(res.type), res.index, res.start, res.length, res.extra, res.flags);
				break;
			}
		}
	}

	void append_pci_irq_resource(DeviceNode* node, const uni::PCI::Device& dev) {
		const uint8 irq_line = read_pci_interrupt_line(dev);
		const uint8 irq_pin = read_pci_interrupt_pin(dev);
		if (irq_line == 0xFF || irq_pin == 0) return;
		append_resource(node, DeviceResourceType::IrqLine,
			DeviceResourceFlag_IrqLevel | DeviceResourceFlag_IrqActiveLow | DeviceResourceFlag_IrqShareable,
			0, irq_line, 1, irq_pin);
	}

	void append_pci_bridge_bus_range_resource(DeviceNode* node, const uni::PCI::Device& dev) {
		if (!(dev.class_code.base == 0x06u && dev.class_code.sub == 0x04u)) return;
		const uint32 bus_numbers = uni::PCI::read_bus_numbers(dev.bus, dev.device, dev.function);
		const uint8 primary_bus = byte(bus_numbers);
		const uint8 secondary_bus = byte(bus_numbers >> 8);
		const uint8 subordinate_bus = byte(bus_numbers >> 16);
		append_resource(node, DeviceResourceType::PciBridgeBusRange, DeviceResourceFlag_None,
			0, primary_bus, secondary_bus, subordinate_bus);
	}

	uint64 read_pci_io_bar_length(const uni::PCI::Device& dev, uint8 bar_index, uint32 bar_low) {
		const uint8 addr = 0x10 + bar_index * 4;
		const uint16 command = uint16(uni::PCI::read_config_register(dev, 0x04) & 0xFFFFu);
		uni::PCI::write_config_register(dev, 0x04, command & uint16(~0x0001u));
		uni::PCI::write_config_register(dev, addr, 0xFFFFFFFFu);
		const uint32 size_low = uni::PCI::read_config_register(dev, addr);
		uni::PCI::write_config_register(dev, addr, bar_low);
		uni::PCI::write_config_register(dev, 0x04, command);
		const uint32 size_mask = size_low & ~0x3u;
		if (!size_mask) return 0;
		return uint64(~size_mask) + 1;
	}

	uint64 read_pci_mmio_bar_length(const uni::PCI::Device& dev, uint8 bar_index, uint32 bar_low, uint8 bar_count) {
		const uint8 addr = 0x10 + bar_index * 4;
		const uint8 mem_type = uint8((bar_low >> 1) & 0x3u);
		if (mem_type == 0x2u && bar_index + 1 >= bar_count) return 0;

		//{TEMP}
		// Protect the active boot GPU from write probing
		if (is_active_boot_gpu(dev)) {
			uint64 base = uint64(bar_low & ~0xFu);
			if (mem_type == 0x2u) {
				const uint32 bar_high = uni::PCI::read_config_register(dev, addr + 4);
				base |= uint64(bar_high) << 32;
			}
			if (base && sys_framebuffer.physical_range.address >= base &&
				sys_framebuffer.physical_range.address < base + 0x100000000ull) {
				return sys_framebuffer.physical_range.length;
			}
			// Skip size probing for other BARs on active GPU to prevent hardware lockup,
			// but return a safe default size (2MB) to ensure virtual page mapping is created.
			return 2 * 1024 * 1024;
		}

		if (mem_type == 0x2u) {
			const uint32 bar_high = uni::PCI::read_config_register(dev, addr + 4);
			uni::PCI::write_config_register(dev, addr, 0xFFFFFFFFu);
			uni::PCI::write_config_register(dev, addr + 4, 0xFFFFFFFFu);
			const uint32 size_low = uni::PCI::read_config_register(dev, addr);
			const uint32 size_high = uni::PCI::read_config_register(dev, addr + 4);
			uni::PCI::write_config_register(dev, addr, bar_low);
			uni::PCI::write_config_register(dev, addr + 4, bar_high);
			const uint64 size_mask = (uint64(size_high) << 32) | (size_low & ~0xFu);
			if (!size_mask) return 0;
			return (~size_mask) + 1;
		}

		uni::PCI::write_config_register(dev, addr, 0xFFFFFFFFu);
		const uint32 size_low = uni::PCI::read_config_register(dev, addr);
		uni::PCI::write_config_register(dev, addr, bar_low);
		const uint32 size_mask = size_low & ~0xFu;
		if (!size_mask) return 0;
		return uint64(~size_mask) + 1;
	}

	void map_pci_mmio_resource(uint64 base, uint64 length) {
		if (!base || !length) return;
		const stduint page_base = stduint(base) & ~0xFFFu;
		const stduint page_end = stduint((base + length + 0xFFFu) & ~0xFFFu);
		if (page_end <= page_base) return;
		kernel_paging.Map(
			page_base,
			page_base,
			page_end - page_base,
			PAGESIZE_4KB,
			PGPROP_present | PGPROP_writable
		);
	}



	void append_pci_bar_resources(DeviceNode* node, const uni::PCI::Device& dev) {
		const uint8 header_type = dev.header_type & 0x7Fu;
		const uint8 bar_count = header_type == 0x01 ? 2 : 6;
		for (uint8 bar_index = 0; bar_index < bar_count; ++bar_index) {
			const uint8 resource_index = bar_index;
			const uint8 addr = 0x10 + bar_index * 4;
			const uint32 bar_low = uni::PCI::read_config_register(dev, addr);
			if (!bar_low) continue;
			if (bar_low & 0x1u) {
				const uint64 base = uint64(bar_low & ~0x3u);
				if (!base) continue;
				const uint64 length = read_pci_io_bar_length(dev, resource_index, bar_low);
				append_resource(node, DeviceResourceType::PciBarIo, DeviceResourceFlag_None,
					resource_index, base, length, 0);
				continue;
			}

			uint16 flags = DeviceResourceFlag_None;
			if (bar_low & 0x8u) flags |= DeviceResourceFlag_Prefetchable;

			const uint8 mem_type = uint8((bar_low >> 1) & 0x3u);
			uint64 base = uint64(bar_low & ~0xFu);
			uint64 length = 0;
			if (mem_type == 0x2u && bar_index + 1 < bar_count) {
				const uint32 bar_high = uni::PCI::read_config_register(dev, addr + 4);
				base |= uint64(bar_high) << 32;
				flags |= DeviceResourceFlag_Bar64;
			}
			if (!base) continue;
			length = read_pci_mmio_bar_length(dev, resource_index, bar_low, bar_count);
			if (is_active_boot_gpu(dev)) {
				flags |= DeviceResourceFlag_SizeEstimated;
			}
			append_resource(node, DeviceResourceType::PciBarMmio, flags,
				resource_index, base, length, 0);
			map_pci_mmio_resource(base, length);
			if (mem_type == 0x2u && bar_index + 1 < bar_count) {
				++bar_index;
			}
		}
	}

	const PciDeviceNameEntry* find_pci_device_name_entry(uint16 vendor_id, uint16 device_id);
	void attach_pci_device_node(const uni::PCI::Device& dev) {
		DeviceNode* bus_node = ensure_pci_bus_node(0, dev.bus);
		auto* dev_node = device_tree.NewNode();
		init_node(dev_node, DeviceNodeType::PciDevice, DeviceBusType::PCI,
			heap_name("pci@%04x:%02x:%02x.%x", 0, dev.bus, dev.device, dev.function));
		dev_node->fields.vendor_id = uni::PCI::read_vendor_id(dev);
		dev_node->fields.device_id = uni::PCI::read_device_id(dev.bus, dev.device, dev.function);
		dev_node->fields.revision = read_pci_revision_id(dev);
		dev_node->fields.class_base = dev.class_code.base;
		dev_node->fields.class_sub = dev.class_code.sub;
		dev_node->fields.class_if = dev.class_code.interface;
		dev_node->fields.dev_class = (uint16(dev.class_code.base) << 8) | dev.class_code.sub;
		dev_node->fields.pci_segment = 0;
		dev_node->fields.pci_bus = dev.bus;
		dev_node->fields.pci_device = dev.device;
		dev_node->fields.pci_function = dev.function;
		if (const char* vendor_name = Devsman::LookupPciVendorName(dev_node->fields.vendor_id)) {
			dev_node->fields.text_manufacturer = StrHeap(vendor_name);
		}
		if (const auto* entry = find_pci_device_name_entry(dev_node->fields.vendor_id, dev_node->fields.device_id)) {
			dev_node->fields.text_product = StrHeap(entry->product_name);
		}
		append_pci_bar_resources(dev_node, dev);
		append_pci_irq_resource(dev_node, dev);
		append_pci_bridge_bus_range_resource(dev_node, dev);
		append_child(bus_node, dev_node);
		if constexpr (0) {
			ploginfo("[DEVSMAN] PCI node: %s vend=%[16H] dev=%[16H] class=%[8H].%[8H].%[8H] res=%u",
				dev_node->link.addr,
				dev_node->fields.vendor_id,
				dev_node->fields.device_id,
				dev_node->fields.class_base,
				dev_node->fields.class_sub,
				dev_node->fields.class_if,
				dev_node->fields.resource_count);
			log_pci_resources(dev_node);
		}
	}

	void start_pci_device(DeviceNode* node) {
		if (!node) return;
		if (node->fields.binding.state != static_cast<uint32>(DriverBindingState::Probed)) return;
		auto starter = find_driver_starter(node->fields.binding.driver_name);
		if (!starter) return;
		const bool ok = starter(node);
		set_driver_probe_state(node, ok ? DriverBindingState::Started : DriverBindingState::Failed, ok ? 0 : -1);
		ploginfo("[DEVSMAN] Start %s -> %s", node->link.addr ? node->link.addr : "(unnamed)", ok ? "ok" : "failed");
	}

}

// find
namespace Devs {
	DeviceNode* find_pci_device_by_class_in_subtree(DeviceNode* node, uint8 class_base, uint8 class_sub, uint8 class_if) {
		for (auto* crt = node; crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
			if (crt->fields.node_type == static_cast<uint16>(DeviceNodeType::PciDevice) &&
				crt->fields.class_base == class_base &&
				crt->fields.class_sub == class_sub &&
				(class_if == MatchAnyClassIf || crt->fields.class_if == class_if)) {
				return crt;
			}
			if (crt->link.subf) {
				if (auto* found = find_pci_device_by_class_in_subtree(reinterpret_cast<DeviceNode*>(crt->link.subf),
					class_base, class_sub, class_if)) {
					return found;
				}
			}
		}
		return nullptr;
	}

	DeviceNode* find_pci_device_by_class(uint8 class_base, uint8 class_sub, uint8 class_if) {
		if (!pci_root || !pci_root->link.subf) return nullptr;
		return find_pci_device_by_class_in_subtree(reinterpret_cast<DeviceNode*>(pci_root->link.subf),
			class_base, class_sub, class_if);
	}

	DeviceNode* find_pci_device_by_vendor_device_in_subtree(DeviceNode* node, uint16 vendor_id, uint16 device_id) {
		for (auto* crt = node; crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
			if (crt->fields.node_type == static_cast<uint16>(DeviceNodeType::PciDevice) &&
				crt->fields.vendor_id == vendor_id &&
				crt->fields.device_id == device_id) {
				return crt;
			}
			if (crt->link.subf) {
				if (auto* found = find_pci_device_by_vendor_device_in_subtree(reinterpret_cast<DeviceNode*>(crt->link.subf),
					vendor_id, device_id)) {
					return found;
				}
			}
		}
		return nullptr;
	}

	DeviceNode* find_pci_device_by_vendor_device(uint16 vendor_id, uint16 device_id) {
		if (!pci_root || !pci_root->link.subf) return nullptr;
		return find_pci_device_by_vendor_device_in_subtree(reinterpret_cast<DeviceNode*>(pci_root->link.subf),
			vendor_id, device_id);
	}

	const PciDeviceNameEntry* find_pci_device_name_entry(uint16 vendor_id, uint16 device_id) {
		for (const auto& entry : pci_device_name_table) {
			if (entry.vendor_id != vendor_id) continue;
			if (entry.device_id != device_id) continue;
			return &entry;
		}
		return nullptr;
	}




}

using namespace Devs;


bool Devsman::AttachPCIDevices(uni::PCI& pci) {
	initialize_device_tree();
	if (pci_devices_attached) {
		plogwarn("[DEVSMAN] PCI devices already attached");
		return true;
	}
	for0(i, pci.num_device) {
		attach_pci_device_node(pci.devices[i]);
	}
	for (Nnode* bus_link = pci_root ? pci_root->link.subf : nullptr; bus_link; bus_link = bus_link->next) {
		auto* bus_node = reinterpret_cast<DeviceNode*>(bus_link);
		if (bus_node->fields.node_type != static_cast<uint16>(DeviceNodeType::PciBus)) continue;
		for (Nnode* dev_link = bus_node->link.subf; dev_link; dev_link = dev_link->next) {
			attach_isa_bus_if_bridge(reinterpret_cast<DeviceNode*>(dev_link));
		}
	}
	reparent_secondary_buses_under_bridges();
	pci_devices_attached = true;
	BindKnownDrivers();
	ProbeKnownDrivers();
	ploginfo("[DEVSMAN] Attached %d PCI device nodes", pci.num_device);
	return true;
}

DeviceNode* Devsman::PCI_Root() {
	return pci_root;
}

DeviceNode* Devsman::PrimaryPciBus() {
	return primary_pci_bus;
}







DeviceNode* Devsman::FindPCIDeviceByClass(uint8 class_base, uint8 class_sub, uint8 class_if) {
	return find_pci_device_by_class(class_base, class_sub, class_if);
}

DeviceNode* Devsman::FindPCIDeviceByVendorDevice(uint16 vendor_id, uint16 device_id) {
	return find_pci_device_by_vendor_device(vendor_id, device_id);
}

const char* Devsman::LookupPciClassName(uint8 class_base, uint8 class_sub, uint8 class_if) {
	for (const auto& entry : pci_class_name_table) {
		if (entry.class_base != class_base) continue;
		if (entry.class_sub != class_sub) continue;
		if (entry.class_if != MatchAnyClassIf && entry.class_if != class_if) continue;
		return entry.name;
	}
	return nullptr;
}

const char* Devsman::LookupPciDeviceName(uint16 vendor_id, uint16 device_id, uint8 class_base, uint8 class_sub) {
	if (class_base == 0x06u && class_sub == 0x01u) {
		const auto kind = uni::ISA::ClassifyBridge(vendor_id, device_id, class_base, class_sub);
		if (kind != uni::ISA::BridgeKind::Unknown) {
			return uni::ISA::BridgeKindName(kind);
		}
	}
	if (const auto* entry = find_pci_device_name_entry(vendor_id, device_id)) {
		return entry->product_name;
	}
	return nullptr;
}

const char* Devsman::LookupPciVendorName(uint16 vendor_id) {
	switch (vendor_id) {
	case 0x104Bu: return "BusLogic";
	case 0x1234u: return "QEMU/Bochs";
	case 0x15ADu: return "VMware";
	case 0x8086u: return "Intel";
	default: return nullptr;
	}
}

#endif






