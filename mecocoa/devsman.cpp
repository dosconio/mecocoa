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
			extn_field = sizeof(DevExt);
		}

		DeviceNode* NewNode() {
			return reinterpret_cast<DeviceNode*>(New());
		}

		void SetRoot(DeviceNode* node) {
			root_node = node ? &node->link : nullptr;
		}
	};

	#if (_MCCA & 0xFF00) == 0x8600

	DeviceTree device_tree;
	DeviceNode* device_root = nullptr;
	DeviceNode* pci_root = nullptr;
	DeviceNode* primary_pci_bus = nullptr;
	DeviceNode* pci_bus_nodes[256]{};
	bool pci_devices_attached = false;

	void init_node(DeviceNode* node, DeviceNodeType node_type, DeviceBusType bus_type, const char* name) {
		MemSet(node, 0, sizeof(Nnode) + sizeof(DevExt));
		node->link.addr = const_cast<char*>(name);
		node->fields.node_type = static_cast<uint16>(node_type);
		node->fields.bus_type = static_cast<uint16>(bus_type);
	}

	void append_child(DeviceNode* parent, DeviceNode* child) {
		Nnode* child_link = &child->link;
		child_link->next = nullptr;
		child_link->subf = nullptr;
		if (!parent->link.subf) {
			parent->link.subf = child_link;
			child_link->pare = &parent->link;
			return;
		}
		Nnode* last = parent->link.subf;
		while (last->next) last = last->next;
		last->next = child_link;
		child_link->left = last;
	}

	const char* heap_name(const char* fmt, stduint a0, stduint a1 = 0, stduint a2 = 0, stduint a3 = 0) {
		return StrHeap(String::newFormat(fmt, a0, a1, a2, a3).reference());
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
		append_child(bus_node, dev_node);
		ploginfo("[DEVSMAN] PCI node: %s vend=%[16H] dev=%[16H] class=%[8H].%[8H].%[8H]",
			dev_node->link.addr,
			dev_node->fields.vendor_id,
			dev_node->fields.device_id,
			dev_node->fields.class_base,
			dev_node->fields.class_sub,
			dev_node->fields.class_if);
	}

	void initialize_device_tree() {
		if (device_root) return;

		device_root = device_tree.NewNode();
		init_node(device_root, DeviceNodeType::SystemRoot, DeviceBusType::None, "system-root");
		device_tree.SetRoot(device_root);

		pci_root = device_tree.NewNode();
		init_node(pci_root, DeviceNodeType::PCI_Root, DeviceBusType::PCI, "pci-root");
		append_child(device_root, pci_root);

		primary_pci_bus = create_pci_bus_node(0, 0);

		ploginfo("[DEVSMAN] Device tree root ready");
		ploginfo("[DEVSMAN] Node: %s", device_root->link.addr);
		ploginfo("[DEVSMAN] Node: %s", pci_root->link.addr);
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

#if (_MCCA & 0xFF00) == 0x8600
bool Devsman::AttachPCIDevices(uni::PCI& pci) {
	initialize_device_tree();
	if (pci_devices_attached) {
		plogwarn("[DEVSMAN] PCI devices already attached");
		return true;
	}
	const uni::PCI::Device* pci_devices = pci.devices.data();
	for0(i, pci.num_device) {
		attach_pci_device_node(pci_devices[i]);
	}
	pci_devices_attached = true;
	ploginfo("[DEVSMAN] Attached %d PCI device nodes", pci.num_device);
	return true;
}

DeviceNode* Devsman::Root() {
	return device_root;
}

DeviceNode* Devsman::PCI_Root() {
	return pci_root;
}

DeviceNode* Devsman::PrimaryPciBus() {
	return primary_pci_bus;
}
#endif
