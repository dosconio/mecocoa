// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] Device Management
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <cpp/Device/Bus/PCI.hpp>
#if (_MCCA & 0xFF00) == 0x8600
#include "c/proctrl/IAx86_64.ext.h"
#include "c/proctrl/IAx86_64.msr.h"
#endif

extern RMOD_LIST __init_rmod_ento[], __init_rmod_endo[];

#define mfence() _ASM volatile ("mfence":::"memory")

namespace {
	class DeviceTree final : public uni::Nchain {
	public:
		DeviceTree() : uni::Nchain(true) {
			extn_field = sizeof(DeviceNode) - sizeof(Nnode);
		}

		DeviceNode* NewNode() {
			return reinterpret_cast<DeviceNode*>(New());
		}

		void SetRoot(DeviceNode* node) {
			root_node = node ? &node->link : nullptr;
		}
	};

	DeviceTree device_tree;
	DeviceNode* device_root = nullptr;
	
	#if (_MCCA & 0xFF00) == 0x8600
	DeviceNode* pci_root = nullptr;
	DeviceNode* primary_pci_bus = nullptr;
	DeviceNode* bus_roots[8]{};
	DeviceNode* pci_bus_nodes[256]{};
	bool pci_devices_attached = false;

	void init_node(DeviceNode* node, DeviceNodeType node_type, DeviceBusType bus_type, const char* name) {
		MemSet(node, 0, sizeof(DeviceNode));
		node->link.addr = const_cast<char*>(name);
		node->fields.node_type = static_cast<uint16>(node_type);
		node->fields.bus_type = static_cast<uint16>(bus_type);
		node->fields.resource_capacity = DeviceNodeInlineResourceCapacity;
		node->fields.resources = node->inline_resources;
	}

	stduint bus_root_index(DeviceBusType bus_type) {
		return static_cast<stduint>(bus_type);
	}

	const char* default_bus_root_name(DeviceBusType bus_type) {
		switch (bus_type) {
		case DeviceBusType::PCI:      return "pci-root";
		case DeviceBusType::USB:      return "usb-root";
		case DeviceBusType::Platform: return "platform-root";
		case DeviceBusType::I2C:      return "i2c-root";
		case DeviceBusType::SPI:      return "spi-root";
		case DeviceBusType::Serio:    return "serio-root";
		case DeviceBusType::Virtio:   return "virtio-root";
		default:                      return "bus-root";
		}
	}

	void append_child(DeviceNode* parent, DeviceNode* child) {
		Nnode* child_link = &child->link;
		child_link->next = nullptr;
		child_link->subf = nullptr;
		child_link->pare = &parent->link;
		child_link->left = nullptr;
		if (!parent->link.subf) {
			parent->link.subf = child_link;
			return;
		}
		Nnode* last = parent->link.subf;
		while (last->next) last = last->next;
		last->next = child_link;
		child_link->left = last;
	}

	bool detach_child(DeviceNode* parent, DeviceNode* child) {
		if (!parent || !child || !parent->link.subf) return false;
		Nnode* prev = nullptr;
		for (Nnode* crt = parent->link.subf; crt; crt = crt->next) {
			if (crt != &child->link) {
				prev = crt;
				continue;
			}
			if (prev) prev->next = crt->next;
			else parent->link.subf = crt->next;
			if (crt->next) {
				auto* next_node = reinterpret_cast<DeviceNode*>(crt->next);
				next_node->link.left = prev;
			}
			crt->next = nullptr;
			crt->left = nullptr;
			crt->pare = nullptr;
			return true;
		}
		return false;
	}

	const char* heap_name(const char* fmt, stduint a0, stduint a1 = 0, stduint a2 = 0, stduint a3 = 0) {
		return StrHeap(String::newFormat(fmt, a0, a1, a2, a3).reference());
	}

	DeviceNode* create_bus_root(DeviceBusType bus_type, const char* name = nullptr) {
		const stduint idx = bus_root_index(bus_type);
		if (bus_roots[idx]) return bus_roots[idx];
		auto* root = device_tree.NewNode();
		init_node(root, bus_type == DeviceBusType::PCI ? DeviceNodeType::PCI_Root : DeviceNodeType::BusRoot,
			bus_type, name ? name : default_bus_root_name(bus_type));
		append_child(device_root, root);
		bus_roots[idx] = root;
		if (bus_type == DeviceBusType::PCI) pci_root = root;
		return root;
	}

	DeviceNode* get_bus_root(DeviceBusType bus_type) {
		return bus_roots[bus_root_index(bus_type)];
	}

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

	uint8 read_pci_revision_id(const uni::PCI::Device& dev) {
		return byte(uni::PCI::read_config_register(dev, 0x08));
	}

	uint8 read_pci_interrupt_line(const uni::PCI::Device& dev) {
		return byte(uni::PCI::read_config_register(dev, 0x3C));
	}

	uint8 read_pci_interrupt_pin(const uni::PCI::Device& dev) {
		return byte(uni::PCI::read_config_register(dev, 0x3C) >> 8);
	}

	bool append_resource(DeviceNode* node, DeviceResourceType type, uint16 flags,
		uint32 index, uint64 start, uint64 length, uint64 extra) {
		if (!node->fields.resources || node->fields.resource_count >= node->fields.resource_capacity) {
			return false;
		}
		auto& res = node->fields.resources[node->fields.resource_count++];
		res.type = static_cast<uint16>(type);
		res.flags = flags;
		res.index = index;
		res.start = start;
		res.length = length;
		res.extra = extra;
		return true;
	}

	DeviceNode* append_plain_device(DeviceNode* parent, DeviceNodeType node_type, DeviceBusType bus_type, const char* name) {
		auto* node = device_tree.NewNode();
		init_node(node, node_type, bus_type, name);
		append_child(parent, node);
		return node;
	}

	DeviceNode* find_named_child(DeviceNode* parent, DeviceNodeType node_type, const char* name) {
		if (!parent || !name) return nullptr;
		for (auto* crt = reinterpret_cast<DeviceNode*>(parent->link.subf); crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
			if (crt->fields.node_type != static_cast<uint16>(node_type)) continue;
			if (crt->link.addr && StrCompare(crt->link.addr, name) == 0) return crt;
		}
		return nullptr;
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
		append_resource(node, DeviceResourceType::IrqLine, DeviceResourceFlag_None,
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
				append_resource(node, DeviceResourceType::PciBarIo, DeviceResourceFlag_None,
					resource_index, base, 0, 0);
				continue;
			}

			uint16 flags = DeviceResourceFlag_None;
			if (bar_low & 0x8u) flags |= DeviceResourceFlag_Prefetchable;

			const uint8 mem_type = uint8((bar_low >> 1) & 0x3u);
			uint64 base = uint64(bar_low & ~0xFu);
			if (mem_type == 0x2u && bar_index + 1 < bar_count) {
				const uint32 bar_high = uni::PCI::read_config_register(dev, addr + 4);
				base |= uint64(bar_high) << 32;
				flags |= DeviceResourceFlag_Bar64;
				++bar_index;
			}
			if (!base) continue;
			append_resource(node, DeviceResourceType::PciBarMmio, flags,
				resource_index, base, 0, 0);
		}
	}

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
		append_pci_bar_resources(dev_node, dev);
		append_pci_irq_resource(dev_node, dev);
		append_pci_bridge_bus_range_resource(dev_node, dev);
		append_child(bus_node, dev_node);
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

	void initialize_device_tree() {
		if (device_root) return;

		device_root = device_tree.NewNode();
		init_node(device_root, DeviceNodeType::SystemRoot, DeviceBusType::None, "system-root");
		device_tree.SetRoot(device_root);
		create_bus_root(DeviceBusType::PCI);
		create_bus_root(DeviceBusType::USB);
		create_bus_root(DeviceBusType::Platform);
		create_bus_root(DeviceBusType::I2C);
		create_bus_root(DeviceBusType::SPI);
		create_bus_root(DeviceBusType::Serio);
		create_bus_root(DeviceBusType::Virtio);
		primary_pci_bus = create_pci_bus_node(0, 0);

		ploginfo("[DEVSMAN] Device tree root ready");
		ploginfo("[DEVSMAN] Node: %s", device_root->link.addr);
		for (stduint i = 1; i < byteof(bus_roots) / byteof(bus_roots[0]); ++i) {
			if (bus_roots[i]) ploginfo("[DEVSMAN] Node: %s", bus_roots[i]->link.addr);
		}
		ploginfo("[DEVSMAN] Node: %s", primary_pci_bus->link.addr);
	}

	void probe_and_attach_pci_devices() {
		if (pci_devices_attached) return;
		uni::PCI pci;
		if (!PCI_Init(pci)) {
			plogwarn("[DEVSMAN] No devices on PCI or PCI init failed.");
			return;
		}
		Devsman::AttachPCIDevices(pci);
	}

	DeviceNode* find_pci_device_by_class_in_subtree(DeviceNode* node, uint8 class_base, uint8 class_sub, uint8 class_if) {
		for (auto* crt = node; crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
			if (crt->fields.node_type == static_cast<uint16>(DeviceNodeType::PciDevice) &&
				crt->fields.class_base == class_base &&
				crt->fields.class_sub == class_sub &&
				crt->fields.class_if == class_if) {
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

	const DeviceResource* find_resource(const DeviceNode* node, DeviceResourceType type, uint32 index) {
		if (!node || !node->fields.resources) return nullptr;
		for0(i, node->fields.resource_count) {
			const auto& res = node->fields.resources[i];
			if (res.type != static_cast<uint16>(type)) continue;
			if (type == DeviceResourceType::IrqLine || type == DeviceResourceType::PciBridgeBusRange) {
				return &res;
			}
			if (res.index == index) return &res;
		}
		return nullptr;
	}

	void reparent_secondary_buses_under_bridges() {
		if (!pci_root) return;
		for (Nnode* bus_link = pci_root->link.subf; bus_link; bus_link = bus_link->next) {
			auto* bus_node = reinterpret_cast<DeviceNode*>(bus_link);
			if (bus_node->fields.node_type != static_cast<uint16>(DeviceNodeType::PciBus)) continue;
			for (Nnode* dev_link = bus_node->link.subf; dev_link; dev_link = dev_link->next) {
				auto* dev_node = reinterpret_cast<DeviceNode*>(dev_link);
				const auto* range = find_resource(dev_node, DeviceResourceType::PciBridgeBusRange, 0);
				if (!range) continue;
				const uint8 secondary_bus = uint8(range->length);
				if (!secondary_bus) continue;
				DeviceNode* secondary_bus_node = pci_bus_nodes[secondary_bus];
				if (!secondary_bus_node || secondary_bus_node == bus_node) continue;
				if (secondary_bus_node->link.pare == &dev_node->link) continue;
				if (!detach_child(pci_root, secondary_bus_node)) continue;
				append_child(dev_node, secondary_bus_node);
				ploginfo("[DEVSMAN] Reparented %s under %s",
					secondary_bus_node->link.addr, dev_node->link.addr);
			}
		}
	}
	#endif
}

bool Devsman::Initialize() {
	#if (_MCCA & 0xFF00) == 0x8600
	initialize_device_tree();
	probe_and_attach_pci_devices();
	#endif

	#if (_MCCA & 0xFF00) == 0x8600 &&       (_MCCA == 0x8632)
	unsigned a, b, c, d;
	_IO_CPUID(1, 0, &a, &b, &c, &d);
	bool support_apic = (cast<cpuid_1_0_edx>(d).apic);
	bool support_x2apic = support_apic && (cast<cpuid_1_0_ecx>(c).x2apic);
	ploginfo("[DEVSMAN] IC Using: %s", support_apic ? support_x2apic ?
		"x2APIC" : "APIC" : "PIC");

	// IC Init
	IC.Reset(SegCo32, 0x80000000);
	IC.Initialize(support_apic + support_x2apic);

	// APIC
	if (support_apic + support_x2apic) {
		uint32_t val;
		val = IC.ReadLAPIC(0x20); // LAPIC ID
		ploginfo("[DEVSMAN] LAPIC ID:%010x", val);
		val = IC.ReadLAPIC(0x30); // LAPIC Version
		uint32_t version = val;
		uint32_t max_lvt = ((val >> 16) & 0xFF) + 1;
		uint32_t suppress_eoi = (val >> 24) & 0x1;
		ploginfo("[DEVSMAN] LAPIC (%s) Version:%d, Max LVT:%d %s",
			((version & 0xFF) >= 0x10 && (version & 0xFF) <= 0x15) ? "Integrated" :
			(version & 0xFF) < 0x10 ? "82489DX Discrete" : "Unknown",
			version, max_lvt,
			suppress_eoi ? "(Suppress EOI)" : "");
		//
		ploginfo("[DEVSMAN] LAPIC TPR %[x], PPR %[x]", IC.ReadLAPIC(0x80), IC.ReadLAPIC(0xA0));
		// keep APIC-IO ID
		//
		val = IC.IO_Read32(1);// VER
		ploginfo("[DEVSMAN] IOAPIC Version:%d, RTEs:%d", val & 0xFF, (val >> 16) + 1);
		// RCBA (Root Complex Base Address)
		outpd(0xcf8, 0x8000f8f0);
		uint32_t rcba_raw = innpd(0xcfc);
		if (rcba_raw != 0xFFFFFFFF) {
			val = rcba_raw & 0xFFFFC000u;
			ploginfo("[DEVSMAN] RCBA Address %[32H]", val);
			ploginfo("[DEVSMAN] OIC  Address %[32H]", val + 0x31FEu);
			// Enable IOAPIC via OIC (Output Interrupt Control)
			// Note: This region must be mapped in page tables if paging is enabled
			volatile uint32* oic = (volatile uint32*)(val + 0x31FEu);
			val = (*oic & 0xffffff00) | 0x100;
			mfence();
			*oic = val;
			mfence();
		} else {
			ploginfo("[DEVSMAN] RCBA not supported, assuming IOAPIC enabled by BIOS");
		}
		//
		// Reset all 24 IOAPIC pins to masked state
		for (int i = 0; i < 24; i++) {
			IC.IO_Writ64(0x10 + i * 2, 0x10000); // Bit 16: Masked
		}
		// Mapping ISA IRQs (0-15) with redundancy for PIT
		for (int i = 0; i < 16; i++) {
			uint8_t vector = (i < 8) ? (IRQ_PIT + i) : (IRQ_RTC + (i - 8));
			uint64_t rte = (uint64_t)vector; // Fixed, Physical (ID 0), Edge, Active-High, Unmasked

			if (i == 0) {
				// Route PIT to both Pin 0 and Pin 2 for maximum compatibility
				IC.IO_Writ64(0x10 + 0 * 2, rte);
				IC.IO_Writ64(0x10 + 2 * 2, rte);
			} else if (i == 2) {
				// IRQ 2 is cascade, usually no device
			} else {
				IC.IO_Writ64(0x10 + i * 2, rte);
			}
		}
	}



	for (auto func = __init_rmod_ento; func < __init_rmod_endo; func++) {
		ploginfo("Loading %s", func->name);
		(func->init)();
	}

	#endif
	return true;
}

DeviceNode* Devsman::Root() {
	return device_root;
}

#if (_MCCA & 0xFF00) == 0x8600
bool Devsman::AttachPCIDevices(uni::PCI& pci) {
	initialize_device_tree();
	if (pci_devices_attached) {
		plogwarn("[DEVSMAN] PCI devices already attached");
		return true;
	}
	for0(i, pci.num_device) {
		attach_pci_device_node(pci.devices[i]);
	}
	reparent_secondary_buses_under_bridges();
	pci_devices_attached = true;
	ploginfo("[DEVSMAN] Attached %d PCI device nodes", pci.num_device);
	return true;
}



DeviceNode* Devsman::RegisterPlatformDevice(const char* name) {
	initialize_device_tree();
	auto* root = get_bus_root(DeviceBusType::Platform);
	if (!root || !name) return nullptr;
	if (auto* node = find_named_child(root, DeviceNodeType::PlatformDevice, name)) return node;
	return append_plain_device(root, DeviceNodeType::PlatformDevice, DeviceBusType::Platform, StrHeap(name));
}

DeviceNode* Devsman::RegisterSerioController(const char* name) {
	initialize_device_tree();
	auto* root = get_bus_root(DeviceBusType::Serio);
	if (!root || !name) return nullptr;
	if (auto* node = find_named_child(root, DeviceNodeType::SerioController, name)) return node;
	return append_plain_device(root, DeviceNodeType::SerioController, DeviceBusType::Serio, StrHeap(name));
}

DeviceNode* Devsman::RegisterSerioDevice(DeviceNode* parent, const char* name) {
	initialize_device_tree();
	if (!parent || !name) return nullptr;
	if (auto* node = find_named_child(parent, DeviceNodeType::SerioDevice, name)) return node;
	return append_plain_device(parent, DeviceNodeType::SerioDevice, DeviceBusType::Serio, StrHeap(name));
}

bool Devsman::AddIoPortResource(DeviceNode* node, uint32 index, uint64 base, uint64 length) {
	if (!node) return false;
	if (find_resource(node, DeviceResourceType::IoPortRange, index)) return true;
	return append_resource(node, DeviceResourceType::IoPortRange, DeviceResourceFlag_None, index, base, length, 0);
}

bool Devsman::AddIrqResource(DeviceNode* node, uint64 vector, uint64 pin) {
	if (!node) return false;
	if (find_resource(node, DeviceResourceType::IrqLine, 0)) return true;
	return append_resource(node, DeviceResourceType::IrqLine, DeviceResourceFlag_None, 0, vector, 1, pin);
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

const DeviceResource* Devsman::FindResource(const DeviceNode* node, DeviceResourceType type, uint32 index) {
	return find_resource(node, type, index);
}
#endif
