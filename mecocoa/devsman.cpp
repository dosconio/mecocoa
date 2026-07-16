// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] Device Management
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <cpp/Device/Bus/ISA.hpp>
#include <cpp/Device/Bus/PCI.hpp>
#include <c/storage/AHCI.h>
#if _MCCA == 0x8664
#include <cpp/Device/USB/xHCI/xHCI.hpp>
#endif
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
	DeviceNode* legacy_isa_bus = nullptr;
	DeviceNode* bus_roots[9]{};
	DeviceNode* pci_bus_nodes[256]{};
	bool pci_devices_attached = false;
	constexpr uint8 MatchAnyClassIf = 0xFF;

	struct PciDriverMatchEntry {
		uint8 class_base;
		uint8 class_sub;
		uint8 class_if;
		const char* driver_name;
	};

	struct NamedDriverMatchEntry {
		const char* node_name;
		const char* driver_name;
	};

	struct DriverOpsEntry {
		const char* driver_name;
		bool (*probe)(DeviceNode*);
	};

	struct DriverStartHookEntry {
		const char* driver_name;
		Devsman::DriverStartRoutine starter;
	};

	constexpr PciDriverMatchEntry pci_driver_match_table[] = {
		{0x06u, 0x04u, MatchAnyClassIf, "pci-bridge"},
		{0x0Cu, 0x03u, 0x30u, "xhci"},
		{AHCI_PCI_CLASS_BASE, AHCI_PCI_CLASS_SUB, AHCI_PCI_CLASS_IF, "ahci"},
		{0x01u, 0x01u, MatchAnyClassIf, "pata"},
		{0x03u, 0x00u, MatchAnyClassIf, "video-vmware"},
		{0x03u, 0x00u, MatchAnyClassIf, "video-bochs"},
	};

	constexpr NamedDriverMatchEntry platform_driver_match_table[] = {
		{"uart@com1", "uart-8250"},
		{"rtc@cmos", "rtc-cmos"},
	};

	constexpr NamedDriverMatchEntry serio_controller_match_table[] = {
		{"i8042", "i8042"},
	};

	constexpr NamedDriverMatchEntry serio_device_match_table[] = {
		{"ps2kbd", "ps2-keyboard"},
		{"ps2mouse", "ps2-mouse"},
	};

	bool is_x86_legacy_isa_platform_name(const char* name) {
		if (!name) return false;
		return StrCompare(name, "pic@8259-master") == 0 ||
			StrCompare(name, "pic@8259-slave") == 0 ||
			StrCompare(name, "rtc@cmos") == 0 ||
			StrCompare(name, "uart@com1") == 0 ||
			StrCompare(name, "fdc@0") == 0;
	}

	bool is_x86_legacy_isa_serio_name(const char* name) {
		if (!name) return false;
		return StrCompare(name, "i8042") == 0;
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

	bool probe_video_bochs_device(DeviceNode* node) {
		return node != nullptr;
	}

	bool probe_video_vmware_device(DeviceNode* node) {
		return node != nullptr;
	}

	constexpr DriverOpsEntry pci_driver_ops_table[] = {
		{"xhci", probe_xhci_device},
		{"ahci", probe_ahci_device},
		{"video-vmware", probe_video_vmware_device},
		{"video-bochs", probe_video_bochs_device},
	};

	DriverStartHookEntry driver_start_hooks[32]{};
	stduint driver_start_hook_count = 0;

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
		case DeviceBusType::ISA:      return "isa-root";
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
		// Preserve existing children when reparenting a populated subtree.
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

	void release_device_node_payload(pureptr_t inp) {
		if (!inp) return;
		auto* node = reinterpret_cast<DeviceNode*>(inp);
		if (node->fields.text_manufacturer) {
			auto* text = const_cast<char*>(node->fields.text_manufacturer);
			mfree(text);
		}
		if (node->fields.text_product) {
			auto* text = const_cast<char*>(node->fields.text_product);
			mfree(text);
		}
		if (node->fields.text_serial) {
			auto* text = const_cast<char*>(node->fields.text_serial);
			mfree(text);
		}
		node->fields.text_manufacturer = nullptr;
		node->fields.text_product = nullptr;
		node->fields.text_serial = nullptr;
		NnodeHeapFreeSimple(inp);
	}

	void release_device_subtree(DeviceNode* node) {
		if (!node) return;
		NnodesRelease(&node->link, release_device_node_payload);
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

	DeviceNode* find_child_by_type(DeviceNode* parent, DeviceNodeType node_type) {
		if (!parent) return nullptr;
		for (auto* crt = reinterpret_cast<DeviceNode*>(parent->link.subf); crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
			if (crt->fields.node_type == static_cast<uint16>(node_type)) return crt;
		}
		return nullptr;
	}

	DeviceNode* resolve_default_platform_parent(const char* name) {
		if (legacy_isa_bus && is_x86_legacy_isa_platform_name(name)) return legacy_isa_bus;
		return get_bus_root(DeviceBusType::Platform);
	}

	DeviceNode* resolve_default_serio_parent(const char* name) {
		if (legacy_isa_bus && is_x86_legacy_isa_serio_name(name)) return legacy_isa_bus;
		return get_bus_root(DeviceBusType::Serio);
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

	bool set_driver_binding(DeviceNode* node, const char* driver_name) {
		if (!node || !driver_name) return false;
		if (node->fields.binding.driver_name &&
			StrCompare(node->fields.binding.driver_name, driver_name) == 0) {
			node->fields.binding.state = static_cast<uint32>(DriverBindingState::Matched);
			node->fields.binding.probe_result = 0;
			return false;
		}
		node->fields.binding.driver_name = driver_name;
		node->fields.binding.state = static_cast<uint32>(DriverBindingState::Matched);
		node->fields.binding.probe_result = 0;
		node->fields.binding.driver_data = nullptr;
		// ploginfo("[DEVSMAN] Bind %s -> %s", node->link.addr ? node->link.addr : "(unnamed)", driver_name);
		return true;
	}

	void set_driver_started(DeviceNode* node, const char* driver_name, void* driver_data = nullptr) {
		if (!node || !driver_name) return;
		node->fields.binding.driver_name = driver_name;
		node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
		node->fields.binding.probe_result = 0;
		node->fields.binding.driver_data = driver_data;
	}

	bool set_driver_probe_state(DeviceNode* node, DriverBindingState state, int32 result) {
		if (!node || !node->fields.binding.driver_name) return false;
		node->fields.binding.state = static_cast<uint32>(state);
		node->fields.binding.probe_result = result;
		return true;
	}

	Devsman::DriverStartRoutine find_driver_starter(const char* driver_name);

	bool try_start_platform_driver(DeviceNode* node, const char* driver_name, void* driver_data) {
		if (!node || !driver_name) return false;
		auto starter = find_driver_starter(driver_name);
		if (!starter) return false;
		set_driver_binding(node, driver_name);
		node->fields.binding.driver_data = driver_data;
		const bool ok = starter(node);
		set_driver_probe_state(node, ok ? DriverBindingState::Started : DriverBindingState::Failed, ok ? 0 : -1);
		return true;
	}

	const PciDriverMatchEntry* match_pci_driver(const DeviceNode* node) {
		if (!node) return nullptr;
		for (const auto& entry : pci_driver_match_table) {
			if (node->fields.class_base != entry.class_base) continue;
			if (node->fields.class_sub != entry.class_sub) continue;
			if (entry.class_if != MatchAnyClassIf && node->fields.class_if != entry.class_if) continue;
			return &entry;
		}
		return nullptr;
	}

	const NamedDriverMatchEntry* match_named_driver(const DeviceNode* node,
		const NamedDriverMatchEntry* table, stduint count) {
		if (!node || !node->link.addr || !table) return nullptr;
		for0(i, count) {
			if (StrCompare(node->link.addr, table[i].node_name) == 0) return &table[i];
		}
		return nullptr;
	}

	const DriverOpsEntry* find_driver_ops(const char* driver_name,
		const DriverOpsEntry* table, stduint count) {
		if (!driver_name || !table) return nullptr;
		for0(i, count) {
			if (StrCompare(driver_name, table[i].driver_name) == 0) return &table[i];
		}
		return nullptr;
	}

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

	Devsman::DriverStartRoutine find_driver_starter(const char* driver_name) {
		if (!driver_name) return nullptr;
		for0(i, driver_start_hook_count) {
			if (!driver_start_hooks[i].driver_name || !driver_start_hooks[i].starter) continue;
			if (StrCompare(driver_name, driver_start_hooks[i].driver_name) == 0) {
				return driver_start_hooks[i].starter;
			}
		}
		return nullptr;
	}

	void bind_pci_device(DeviceNode* node) {
		if (!node) return;
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
		if (const auto* entry = match_pci_driver(node)) {
			if (StrCompare(entry->driver_name, "video-vmware") == 0 && !is_vmware_video_device(node)) {
				return;
			}
			if (StrCompare(entry->driver_name, "video-bochs") == 0 && !is_bochs_video_device(node)) {
				return;
			}
			set_driver_binding(node, entry->driver_name);
		}
	}

	void bind_platform_device(DeviceNode* node) {
		if (const auto* entry = match_named_driver(node,
			platform_driver_match_table, numsof(platform_driver_match_table))) {
			set_driver_binding(node, entry->driver_name);
		}
	}

	void bind_serio_controller(DeviceNode* node) {
		if (const auto* entry = match_named_driver(node,
			serio_controller_match_table, numsof(serio_controller_match_table))) {
			set_driver_binding(node, entry->driver_name);
		}
	}

	void bind_serio_device(DeviceNode* node) {
		if (const auto* entry = match_named_driver(node,
			serio_device_match_table, numsof(serio_device_match_table))) {
			set_driver_binding(node, entry->driver_name);
		}
	}

	void bind_device_node(DeviceNode* node) {
		if (!node) return;
		switch (DeviceNodeType(node->fields.node_type)) {
		case DeviceNodeType::PciDevice:
			bind_pci_device(node);
			break;
		case DeviceNodeType::PlatformDevice:
			bind_platform_device(node);
			break;
		case DeviceNodeType::SerioController:
			bind_serio_controller(node);
			break;
		case DeviceNodeType::SerioDevice:
			bind_serio_device(node);
			break;
		default:
			break;
		}
	}

	void bind_known_drivers_subtree(DeviceNode* node) {
		for (auto* crt = node; crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
			bind_device_node(crt);
			if (crt->link.subf) {
				bind_known_drivers_subtree(reinterpret_cast<DeviceNode*>(crt->link.subf));
			}
		}
	}

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

	void probe_device_node(DeviceNode* node) {
		if (!node) return;
		switch (DeviceNodeType(node->fields.node_type)) {
		case DeviceNodeType::PciDevice:
			probe_pci_device(node);
			break;
		default:
			break;
		}
	}

	void probe_known_drivers_subtree(DeviceNode* node) {
		for (auto* crt = node; crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
			probe_device_node(crt);
			if (crt->link.subf) {
				probe_known_drivers_subtree(reinterpret_cast<DeviceNode*>(crt->link.subf));
			}
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

	void start_device_node(DeviceNode* node) {
		if (!node) return;
		switch (DeviceNodeType(node->fields.node_type)) {
		case DeviceNodeType::PciDevice:
			start_pci_device(node);
			break;
		default:
			break;
		}
	}

	void start_known_drivers_subtree(DeviceNode* node) {
		for (auto* crt = node; crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
			start_device_node(crt);
			if (crt->link.subf) {
				start_known_drivers_subtree(reinterpret_cast<DeviceNode*>(crt->link.subf));
			}
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

	uint64 read_pci_mmio_bar_length(const uni::PCI::Device& dev, uint8 bar_index, uint32 bar_low, uint8 bar_count) {
		const uint8 addr = 0x10 + bar_index * 4;
		const uint8 mem_type = uint8((bar_low >> 1) & 0x3u);
		if (mem_type == 0x2u && bar_index + 1 >= bar_count) return 0;

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
				append_resource(node, DeviceResourceType::PciBarIo, DeviceResourceFlag_None,
					resource_index, base, 0, 0);
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
			append_resource(node, DeviceResourceType::PciBarMmio, flags,
				resource_index, base, length, 0);
			map_pci_mmio_resource(base, length);
			if (mem_type == 0x2u && bar_index + 1 < bar_count) {
				++bar_index;
			}
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

	DeviceNode* ensure_isa_bus_node(DeviceNode* pci_bridge_node) {
		if (!pci_bridge_node) return nullptr;
		if (auto* node = find_child_by_type(pci_bridge_node, DeviceNodeType::IsaBus)) {
			return node;
		}
		auto* isa_node = device_tree.NewNode();
		init_node(isa_node, DeviceNodeType::IsaBus, DeviceBusType::ISA,
			heap_name("isa@%04x:%02x:%02x.%x",
				pci_bridge_node->fields.pci_segment,
				pci_bridge_node->fields.pci_bus,
				pci_bridge_node->fields.pci_device,
				pci_bridge_node->fields.pci_function));
		append_child(pci_bridge_node, isa_node);
		return isa_node;
	}

	DeviceNode* attach_isa_bus_if_bridge(DeviceNode* pci_node) {
		if (!pci_node) return nullptr;
		const auto kind = uni::ISA::ClassifyBridge(pci_node->fields.vendor_id, pci_node->fields.device_id,
			pci_node->fields.class_base, pci_node->fields.class_sub);
		if (kind == uni::ISA::BridgeKind::Unknown) return nullptr;
		if (!pci_node->fields.text_product) {
			pci_node->fields.text_product = StrHeap(uni::ISA::BridgeKindName(kind));
		}
		if (!pci_node->fields.text_manufacturer && pci_node->fields.vendor_id == 0x8086u) {
			pci_node->fields.text_manufacturer = StrHeap("Intel");
		}
		auto* isa_node = ensure_isa_bus_node(pci_node);
		if (!legacy_isa_bus) legacy_isa_bus = isa_node;
		ploginfo("[DEVSMAN] ISA bridge %s on %s -> %s",
			uni::ISA::BridgeKindName(kind),
			pci_node->link.addr ? pci_node->link.addr : "(unnamed)",
			isa_node->link.addr ? isa_node->link.addr : "(unnamed)");
		return isa_node;
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
		if constexpr (0) {
			ploginfo("[DEVSMAN] Node: %s", device_root->link.addr);
			for (stduint i = 1; i < byteof(bus_roots) / byteof(bus_roots[0]); ++i) {
				if (bus_roots[i]) ploginfo("[DEVSMAN] Node: %s", bus_roots[i]->link.addr);
			}
			ploginfo("[DEVSMAN] Node: %s", primary_pci_bus->link.addr);
		}
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

	void register_legacy_x86_platform_nodes(bool support_apic) {
		auto* legacy_bus = legacy_isa_bus ? legacy_isa_bus : get_bus_root(DeviceBusType::Platform);
		if (auto* pic_master = Devsman::RegisterPlatformDevice(legacy_bus, "pic@8259-master", "pic-8259")) {
			Devsman::AddIoPortResource(pic_master, 0, 0x20, 2);
		}
		if (auto* pic_slave = Devsman::RegisterPlatformDevice(legacy_bus, "pic@8259-slave", "pic-8259")) {
			Devsman::AddIoPortResource(pic_slave, 0, 0xA0, 2);
		}
		if (support_apic) {
			Devsman::RegisterPlatformDevice("lapic@0", "lapic");
			Devsman::RegisterPlatformDevice("ioapic@0", "ioapic");
		}
		if (auto* fdc = Devsman::RegisterPlatformDevice(legacy_bus, "fdc@0")) {
			Devsman::AddIoPortResource(fdc, 0, 0x3F0, 8);
			Devsman::AddIrqResource(fdc, IRQ_PIT + 6);
			Devsman::RegisterStorageDevice(fdc, "floppy@0", DeviceBusType::ISA);
		}
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

	void program_isa_irq_routes() {
		uint16_t enabled_irq_mask = 0xFFFF;
		bool mirror_irq0_to_pin2 = true;
		#if _MCCA == 0x8664
		// x64 UEFI can open the ISA IRQ set, but mirroring IRQ0 onto pin2
		// currently triggers the legacy cascade path bug.
		mirror_irq0_to_pin2 = false;
		#endif
		// Reset all 24 IOAPIC pins to masked state first.
		for (int i = 0; i < 24; i++) {
			IC.IO_Writ64(0x10 + i * 2, 0x10000); // Bit 16: Masked
		}
		// Then remap ISA IRQs (0-15) to the kernel vectors and clear the mask bit.
		for (int i = 0; i < 16; i++) {
			if ((enabled_irq_mask & (1u << i)) == 0) continue;
			const uint8_t vector = (i < 8) ? (IRQ_PIT + i) : (IRQ_RTC + (i - 8));
			const uint64_t rte = (uint64_t)vector; // Fixed, Physical, Edge, Active-High, Unmasked
			if (i == 0) {
				// Route PIT to pin 0. Some platforms also mirror it to pin 2 for legacy
				// compatibility, but x64 UEFI currently keeps pin 2 masked while we
				// localize the transition-stack/legacy-IRQ entry issue.
				IC.IO_Writ64(0x10 + 0 * 2, rte);
				if (mirror_irq0_to_pin2) {
					IC.IO_Writ64(0x10 + 2 * 2, rte);
				}
			}
			else if (i != 2) {
				IC.IO_Writ64(0x10 + i * 2, rte);
			}
		}
	}

	#if _MCCA == 0x8664
	struct USBNodeClassInfo {
		uint8 class_base;
		uint8 class_sub;
		uint8 class_if;
		const char* driver_name;
		const char* kind_name;
	};

	struct USBDeviceNodeInfo {
		const char* driver_name;
		const char* kind_name;
	};

	DeviceNode* ensure_xhci_root_hub_node(uni::device::SpaceUSB3::HostController& xhc);

	USBDeviceNodeInfo classify_usb_device(const uni::device::SpaceUSB3::DeviceUSB3& dev) {
		if (dev.DeviceClass() == 0x09u) {
			return {"usb-hub", "hub"};
		}
		return {"usb-device", "device"};
	}

	USBNodeClassInfo classify_usb_interface(const uni::device::SpaceUSB::InterfaceDescriptor& if_desc) {
		if (if_desc.interface_class == 3 && if_desc.interface_sub_class == 1) {
			if (if_desc.interface_protocol == 1) {
				return {if_desc.interface_class, if_desc.interface_sub_class, if_desc.interface_protocol,
					"usb-hid-keyboard", "keyboard"};
			}
			if (if_desc.interface_protocol == 2) {
				return {if_desc.interface_class, if_desc.interface_sub_class, if_desc.interface_protocol,
					"usb-hid-mouse", "mouse"};
			}
		}
		return {if_desc.interface_class, if_desc.interface_sub_class, if_desc.interface_protocol,
			"usb-interface", "interface"};
	}

	DeviceNode* find_device_node_by_driver_data(DeviceNode* parent, uint16 node_type, void* driver_data) {
		if (!parent || !driver_data) return nullptr;
		for (auto* node = reinterpret_cast<DeviceNode*>(parent->link.subf); node; node = reinterpret_cast<DeviceNode*>(node->link.next)) {
			if (!node) continue;
			if (node->fields.node_type == node_type &&
				node->fields.binding.driver_data == driver_data) {
				return node;
			}
			if (auto* found = find_device_node_by_driver_data(node, node_type, driver_data)) {
				return found;
			}
		}
		return nullptr;
	}

	DeviceNode* find_usb_device_node_by_driver_data(DeviceNode* parent, void* driver_data) {
		return find_device_node_by_driver_data(parent, uint16(DeviceNodeType::UsbDevice), driver_data);
	}

	DeviceNode* find_pci_device_node_by_driver_data(DeviceNode* parent, void* driver_data) {
		return find_device_node_by_driver_data(parent, uint16(DeviceNodeType::PciDevice), driver_data);
	}

	void ensure_usb_hub_downstream_ports(DeviceNode* usb_dev_node, uint8 num_ports) {
		if (!usb_dev_node || num_ports == 0) return;
		for (uint8 downstream_port = 1; downstream_port <= num_ports; ++downstream_port) {
			auto usb_port_name = String::newFormat("usb-port@%u", (stduint)downstream_port);
			Devsman::RegisterUSBPort(usb_dev_node, usb_port_name.reference(), downstream_port);
		}
	}

	void ensure_usb_hub_downstream_ports_for_device(uni::device::SpaceUSB3::HostController& xhc,
		uni::device::SpaceUSB3::DeviceUSB3& dev) {
		if (dev.DeviceClass() != 0x09u) return;
		auto* usb_root_hub_node = ensure_xhci_root_hub_node(xhc);
		if (!usb_root_hub_node) return;
		auto* usb_hub_node = find_usb_device_node_by_driver_data(usb_root_hub_node, &dev);
		ensure_usb_hub_downstream_ports(usb_hub_node, dev.HubNumPorts());
	}

	void register_single_usb_device_for_xhci(DeviceNode* usb_parent_node,
		uint8 port_num, uni::device::SpaceUSB3::DeviceUSB3& dev) {
		if (!usb_parent_node || !dev.IsInitialized()) return;
		const auto slot_id = dev.SlotID();
		auto dev_info = classify_usb_device(dev);
		auto usb_port_name = String::newFormat("usb-port@%u", (stduint)port_num);
		auto* usb_port_node = Devsman::RegisterUSBPort(usb_parent_node, usb_port_name.reference(), port_num);
		auto usb_dev_name = String::newFormat("usb-dev@port%u.slot%u", (stduint)port_num, (stduint)slot_id);
		auto* usb_dev_node = Devsman::RegisterUSBDevice(usb_port_node, usb_dev_name.reference(),
			dev.VendorID(), dev.ProductID(),
			dev.ManufacturerString(), dev.ProductString(), dev.SerialString(),
			dev.DeviceClass(), dev.DeviceSubClass(), dev.DeviceProtocol(),
			port_num, slot_id,
			dev_info.driver_name, &dev);
		if (dev.DeviceClass() == 0x09u) {
			ensure_usb_hub_downstream_ports(usb_dev_node, dev.HubNumPorts());
		}
		auto* desc = dev.Buffer();
		if (!usb_dev_node || !desc) return;
		const auto* conf_desc = uni::device::SpaceUSB::DescriptorDynamicCast<uni::device::SpaceUSB::ConfigurationDescriptor>(desc);
		if (!conf_desc || conf_desc->total_length < conf_desc->length) return;
		const auto* p = desc + conf_desc->length;
		const auto* end = desc + conf_desc->total_length;
		while (p + 2 <= end) {
			const uint8 len = p[0];
			if (len == 0 || p + len > end) break;
			if (auto* if_desc = uni::device::SpaceUSB::DescriptorDynamicCast<uni::device::SpaceUSB::InterfaceDescriptor>(p)) {
				auto info = classify_usb_interface(*if_desc);
				auto usb_if_name = String::newFormat("usb-if@%u", (stduint)if_desc->interface_number);
				auto* usb_if_node = Devsman::RegisterUSBInterface(usb_dev_node, usb_if_name.reference(),
					info.class_base, info.class_sub, info.class_if,
					info.driver_name, &dev);
				const auto* q = p + len;
				uint32 ep_index = 0;
				while (q + 2 <= end) {
					const uint8 qlen = q[0];
					if (qlen == 0 || q + qlen > end) break;
					if (uni::device::SpaceUSB::DescriptorDynamicCast<uni::device::SpaceUSB::InterfaceDescriptor>(q)) {
						break;
					}
					if (auto* ep_desc = uni::device::SpaceUSB::DescriptorDynamicCast<uni::device::SpaceUSB::EndpointDescriptor>(q)) {
						Devsman::AddUSBEndpointResource(usb_if_node, ep_index++,
							ep_desc->endpoint_address.data,
							ep_desc->attributes.bits.transfer_type,
							ep_desc->max_packet_size,
							ep_desc->interval);
					}
					q += qlen;
				}
			}
			p += len;
		}
	}

	DeviceNode* ensure_xhci_root_hub_node(uni::device::SpaceUSB3::HostController& xhc) {
		auto* root = Devsman::Root();
		if (!root) return nullptr;
		auto* xhc_node = find_pci_device_node_by_driver_data(root, &xhc);
		if (!xhc_node) return nullptr;
		auto usb_bus_name = String::newFormat("usb-bus@%04x:%02x:%02x.%x", 0,
			(stduint)xhc_node->fields.pci_bus,
			(stduint)xhc_node->fields.pci_device,
			(stduint)xhc_node->fields.pci_function);
		auto* usb_bus_node = Devsman::RegisterUSBBus(usb_bus_name.reference(), "xhci", &xhc);
		return Devsman::RegisterUSBRootHub(usb_bus_node, "usb-root-hub@0", 0x09u, 0x00u, 0x03u, "usb-root-hub", &xhc);
	}

	void on_xhci_complete_configuration(uni::device::SpaceUSB3::HostController& xhc, uint8 port_id, uint8, uni::device::SpaceUSB3::DeviceUSB3& dev) {
		auto* usb_root_hub_node = ensure_xhci_root_hub_node(xhc);
		if (!usb_root_hub_node) return;
		if (dev.ParentHubSlotID() != 0) {
			auto* parent_hub_dev = xhc.GetDeviceManager()->FindBySlot(dev.ParentHubSlotID());
			auto* parent_hub_node = find_usb_device_node_by_driver_data(usb_root_hub_node, parent_hub_dev);
			register_single_usb_device_for_xhci(parent_hub_node, dev.UpstreamPortNum(), dev);
			return;
		}
		register_single_usb_device_for_xhci(usb_root_hub_node, port_id, dev);
	}

	void on_usb_hub_descriptor_complete(uni::device::SpaceUSB::DeviceUSB& base_dev) {
		auto& dev = static_cast<uni::device::SpaceUSB3::DeviceUSB3&>(base_dev);
		auto* xhc = dev.Controller();
		if (!xhc) return;
		ensure_usb_hub_downstream_ports_for_device(*xhc, dev);
	}

	void on_usb_hub_port_status(uni::device::SpaceUSB::DeviceUSB& base_dev,
		uint8 downstream_port, uint16 status, uint16 change) {
		(void)change;
		auto& dev = static_cast<uni::device::SpaceUSB3::DeviceUSB3&>(base_dev);
		if ((status & 0x0001u) == 0) return;
	}

	void on_xhci_device_disconnect(uni::device::SpaceUSB3::HostController& xhc, uint8 port_id, uint8 slot_id) {
		auto* usb_root_node = ensure_xhci_root_hub_node(xhc);
		if (!usb_root_node) return;
		auto* dev = xhc.GetDeviceManager()->FindBySlot(slot_id);
		DeviceNode* usb_parent_node = usb_root_node;
		uint8 upstream_port_num = port_id;
		if (dev && dev->ParentHubSlotID() != 0) {
			auto* parent_hub_dev = xhc.GetDeviceManager()->FindBySlot(dev->ParentHubSlotID());
			usb_parent_node = find_usb_device_node_by_driver_data(usb_root_node, parent_hub_dev);
			upstream_port_num = dev->UpstreamPortNum();
		}
		auto usb_port_name = String::newFormat("usb-port@%u", (stduint)upstream_port_num);
		auto* usb_port_node = Devsman::RegisterUSBPort(usb_parent_node, usb_port_name.reference(), upstream_port_num);
		auto usb_dev_name = String::newFormat("usb-dev@port%u.slot%u", (stduint)upstream_port_num, (stduint)slot_id);
		if (Devsman::RemoveUSBDevice(usb_port_node, usb_dev_name.reference())) {
			if (dev && dev->ParentHubSlotID() != 0) {
				ploginfo("USB device detached from xHC root-port=%u hub-slot=%u downstream-port=%u child-slot=%u",
					(stduint)port_id,
					(stduint)dev->ParentHubSlotID(),
					(stduint)upstream_port_num,
					(stduint)slot_id);
			} else {
				ploginfo("USB device detached from xHC root-port=%u slot=%u",
					(stduint)port_id, (stduint)slot_id);
			}
		}
	}
	#endif
	#endif
}

bool Devsman::Initialize() {
	#if (_MCCA & 0xFF00) == 0x8600
	initialize_device_tree();
	probe_and_attach_pci_devices();
	#endif

	#if _MCCA == 0x8664
	new (&IC) InterruptControl(mglb(mem.allocate(256 * sizeof(gate_t))));
	IC.Reset(SegCo64, 0xFFFFFFFFC0000000ull);
	
	unsigned a = 0, b = 0, c = 0, d = 0;
	_IO_CPUID(1, 0, &a, &b, &c, &d);
	bool support_apic = cast<cpuid_1_0_edx>(d).apic;
	// CPUID.x2apic is a capability bit only; actual mode is determined by IA32_APIC_BASE.EXTD (bit10).
	// In UEFI, the firmware controls APIC mode at ExitBootServices time.
	// Forcing x2APIC when UEFI left EXTD=0 would switch the LAPIC but leave the IOAPIC RTEs in
	// xAPIC format, causing SendEOI (WRMSR 0x80B) to not clear the IOAPIC ISR → deadlock.
	bool cpuid_x2apic = support_apic && (cast<cpuid_1_0_ecx>(c).x2apic);
	bool hw_x2apic    = support_apic && ((getMSR(x86MSR::APIC_BASE) >> 10) & 1); // EXTD bit
	bool support_x2apic = cpuid_x2apic && hw_x2apic; // only use x2APIC if HW is already in x2APIC mode
	ploginfo("[DEVSMAN] IC Using: %s (CPUID x2apic=%d HW EXTD=%d)", support_apic ? support_x2apic ?
		"x2APIC" : "APIC" : "PIC", (int)cpuid_x2apic, (int)hw_x2apic);
	IC.Initialize(support_apic + support_x2apic);
	if (support_apic + support_x2apic) {
		program_isa_irq_routes();
	}

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
		program_isa_irq_routes();
	}

	register_legacy_x86_platform_nodes(support_apic + support_x2apic);



	#endif
	
	#if (_MCCA & 0xFF00) == 0x8600
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

void Devsman::BindKnownDrivers() {
	#if (_MCCA & 0xFF00) == 0x8600
	if (!device_root) return;
	bind_known_drivers_subtree(device_root);
	#endif
}

void Devsman::ProbeKnownDrivers() {
	#if (_MCCA & 0xFF00) == 0x8600
	if (!device_root) return;
	probe_known_drivers_subtree(device_root);
	#endif
}

void Devsman::StartKnownDrivers() {
	#if (_MCCA & 0xFF00) == 0x8600
	if (!device_root) return;
	start_known_drivers_subtree(device_root);
	#endif
}

bool Devsman::RegisterDriverStarter(const char* driver_name, DriverStartRoutine starter) {
	#if (_MCCA & 0xFF00) == 0x8600
	if (!driver_name || !starter) return false;
	for0(i, driver_start_hook_count) {
		if (driver_start_hooks[i].driver_name &&
			StrCompare(driver_start_hooks[i].driver_name, driver_name) == 0) {
			driver_start_hooks[i].starter = starter;
			return true;
		}
	}
	if (driver_start_hook_count >= numsof(driver_start_hooks)) return false;
	driver_start_hooks[driver_start_hook_count++] = { driver_name, starter };
	return true;
	#else
	return false;
	#endif
}

void Devsman::RegisterXHCIDeviceTreeHook() {
	#if _MCCA == 0x8664
	uni::device::SpaceUSB3::g_configuration_complete_hook = on_xhci_complete_configuration;
	uni::device::SpaceUSB3::g_device_disconnect_hook = on_xhci_device_disconnect;
	uni::device::SpaceUSB::g_hub_descriptor_complete_hook = on_usb_hub_descriptor_complete;
	uni::device::SpaceUSB::g_hub_port_status_hook = on_usb_hub_port_status;
	#endif
}



DeviceNode* Devsman::RegisterPlatformDevice(const char* name, const char* driver_name, void* driver_data) {
	initialize_device_tree();
	auto* root = resolve_default_platform_parent(name);
	if (!root || !name) return nullptr;
	if (driver_name) return RegisterPlatformDevice(root, name, driver_name, driver_data);
	const auto root_bus_type = DeviceBusType(root->fields.bus_type);
	const auto node_bus_type = root_bus_type == DeviceBusType::ISA ? DeviceBusType::ISA : DeviceBusType::Platform;
	if (auto* node = find_named_child(root, DeviceNodeType::PlatformDevice, name)) {
		node->fields.bus_type = static_cast<uint16>(node_bus_type);
		bind_device_node(node);
		return node;
	}
	auto* node = append_plain_device(root, DeviceNodeType::PlatformDevice, node_bus_type, StrHeap(name));
	bind_device_node(node);
	return node;
}

DeviceNode* Devsman::RegisterPlatformDevice(DeviceNode* parent, const char* name,
	const char* driver_name, void* driver_data) {
	initialize_device_tree();
	if (!parent || !name) return nullptr;
	const auto parent_bus_type = DeviceBusType(parent->fields.bus_type);
	const auto node_bus_type = parent_bus_type == DeviceBusType::ISA ? DeviceBusType::ISA : DeviceBusType::Platform;
	if (auto* node = find_named_child(parent, DeviceNodeType::PlatformDevice, name)) {
		if (driver_name) {
			if (!try_start_platform_driver(node, driver_name, driver_data)) {
				set_driver_started(node, driver_name, driver_data);
			}
		}
		else bind_device_node(node);
		return node;
	}
	auto* node = append_plain_device(parent, DeviceNodeType::PlatformDevice, node_bus_type, StrHeap(name));
	if (driver_name) {
		if (!try_start_platform_driver(node, driver_name, driver_data)) {
			set_driver_started(node, driver_name, driver_data);
		}
	}
	else bind_device_node(node);
	return node;
}

DeviceNode* Devsman::RegisterStorageDevice(DeviceNode* parent, const char* name,
	DeviceBusType bus_type, const char* driver_name, void* driver_data) {
	initialize_device_tree();
	if (!parent || !name) return nullptr;
	if (auto* node = find_named_child(parent, DeviceNodeType::StorageDevice, name)) {
		if (driver_name) set_driver_started(node, driver_name, driver_data);
		return node;
	}
	auto* node = append_plain_device(parent, DeviceNodeType::StorageDevice, bus_type, StrHeap(name));
	if (driver_name) set_driver_started(node, driver_name, driver_data);
	return node;
}

DeviceNode* Devsman::RegisterUSBBus(const char* name, const char* driver_name, void* driver_data) {
	initialize_device_tree();
	auto* root = get_bus_root(DeviceBusType::USB);
	if (!root || !name) return nullptr;
	if (auto* node = find_named_child(root, DeviceNodeType::UsbBus, name)) {
		if (driver_name) {
			set_driver_binding(node, driver_name);
			node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
			node->fields.binding.driver_data = driver_data;
		}
		return node;
	}
	auto* node = append_plain_device(root, DeviceNodeType::UsbBus, DeviceBusType::USB, StrHeap(name));
	if (driver_name) {
		set_driver_binding(node, driver_name);
		node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
		node->fields.binding.driver_data = driver_data;
	}
	return node;
}

DeviceNode* Devsman::RegisterUSBRootHub(DeviceNode* parent, const char* name,
	uint8 class_base, uint8 class_sub, uint8 class_if,
	const char* driver_name, void* driver_data) {
	initialize_device_tree();
	if (!parent || !name) return nullptr;
	if (auto* node = find_named_child(parent, DeviceNodeType::UsbRootHub, name)) {
		node->fields.class_base = class_base;
		node->fields.class_sub = class_sub;
		node->fields.class_if = class_if;
		node->fields.dev_class = (uint16(class_base) << 8) | class_sub;
		if (driver_name) {
			set_driver_binding(node, driver_name);
			node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
			node->fields.binding.driver_data = driver_data;
		}
		return node;
	}
	auto* node = append_plain_device(parent, DeviceNodeType::UsbRootHub, DeviceBusType::USB, StrHeap(name));
	node->fields.class_base = class_base;
	node->fields.class_sub = class_sub;
	node->fields.class_if = class_if;
	node->fields.dev_class = (uint16(class_base) << 8) | class_sub;
	if (driver_name) {
		set_driver_binding(node, driver_name);
		node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
		node->fields.binding.driver_data = driver_data;
	}
	return node;
}

DeviceNode* Devsman::RegisterUSBPort(DeviceNode* parent, const char* name, uint8 port_num) {
	initialize_device_tree();
	if (!parent || !name) return nullptr;
	if (auto* node = find_named_child(parent, DeviceNodeType::UsbPort, name)) {
		if (!find_resource(node, DeviceResourceType::UsbLocation, 0)) {
			append_resource(node, DeviceResourceType::UsbLocation, DeviceResourceFlag_None, 0, port_num, 1, 0);
		}
		return node;
	}
	auto* node = append_plain_device(parent, DeviceNodeType::UsbPort, DeviceBusType::USB, StrHeap(name));
	append_resource(node, DeviceResourceType::UsbLocation, DeviceResourceFlag_None, 0, port_num, 1, 0);
	return node;
}

DeviceNode* Devsman::RegisterUSBDevice(DeviceNode* parent, const char* name,
	uint16 vendor_id, uint16 product_id,
	const char* text_manufacturer, const char* text_product, const char* text_serial,
	uint8 class_base, uint8 class_sub, uint8 class_if,
	uint8 port_num, uint8 slot_id,
	const char* driver_name, void* driver_data) {
	initialize_device_tree();
	if (!parent || !name) return nullptr;
	if (auto* node = find_named_child(parent, DeviceNodeType::UsbDevice, name)) {
		node->fields.vendor_id = vendor_id;
		node->fields.device_id = product_id;
		node->fields.text_manufacturer = text_manufacturer ? StrHeap(text_manufacturer) : nullptr;
		node->fields.text_product = text_product ? StrHeap(text_product) : nullptr;
		node->fields.text_serial = text_serial ? StrHeap(text_serial) : nullptr;
		node->fields.class_base = class_base;
		node->fields.class_sub = class_sub;
		node->fields.class_if = class_if;
		node->fields.dev_class = (uint16(class_base) << 8) | class_sub;
		if (!find_resource(node, DeviceResourceType::UsbLocation, 0)) {
			append_resource(node, DeviceResourceType::UsbLocation, DeviceResourceFlag_None, 0, port_num, 1, slot_id);
		}
		if (driver_name) {
			set_driver_binding(node, driver_name);
			node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
			node->fields.binding.driver_data = driver_data;
		}
		return node;
	}
	auto* node = append_plain_device(parent, DeviceNodeType::UsbDevice, DeviceBusType::USB, StrHeap(name));
	node->fields.vendor_id = vendor_id;
	node->fields.device_id = product_id;
	node->fields.text_manufacturer = text_manufacturer ? StrHeap(text_manufacturer) : nullptr;
	node->fields.text_product = text_product ? StrHeap(text_product) : nullptr;
	node->fields.text_serial = text_serial ? StrHeap(text_serial) : nullptr;
	node->fields.class_base = class_base;
	node->fields.class_sub = class_sub;
	node->fields.class_if = class_if;
	node->fields.dev_class = (uint16(class_base) << 8) | class_sub;
	append_resource(node, DeviceResourceType::UsbLocation, DeviceResourceFlag_None, 0, port_num, 1, slot_id);
	if (driver_name) {
		set_driver_binding(node, driver_name);
		node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
		node->fields.binding.driver_data = driver_data;
	}
	return node;
}

DeviceNode* Devsman::RegisterUSBInterface(DeviceNode* parent, const char* name,
	uint8 class_base, uint8 class_sub, uint8 class_if,
	const char* driver_name, void* driver_data) {
	initialize_device_tree();
	if (!parent || !name) return nullptr;
	if (auto* node = find_named_child(parent, DeviceNodeType::UsbInterface, name)) {
		node->fields.class_base = class_base;
		node->fields.class_sub = class_sub;
		node->fields.class_if = class_if;
		node->fields.dev_class = (uint16(class_base) << 8) | class_sub;
		if (driver_name) {
			set_driver_binding(node, driver_name);
			node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
			node->fields.binding.driver_data = driver_data;
		}
		return node;
	}
	auto* node = append_plain_device(parent, DeviceNodeType::UsbInterface, DeviceBusType::USB, StrHeap(name));
	node->fields.class_base = class_base;
	node->fields.class_sub = class_sub;
	node->fields.class_if = class_if;
	node->fields.dev_class = (uint16(class_base) << 8) | class_sub;
	if (driver_name) {
		set_driver_binding(node, driver_name);
		node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
		node->fields.binding.driver_data = driver_data;
	}
	return node;
}

bool Devsman::AddUSBEndpointResource(DeviceNode* node, uint32 index,
	uint8 endpoint_addr, uint8 transfer_type, uint16 max_packet_size, uint8 interval) {
	if (!node) return false;
	if (find_resource(node, DeviceResourceType::UsbEndpoint, index)) return true;
	return append_resource(node, DeviceResourceType::UsbEndpoint, DeviceResourceFlag_None,
		index, endpoint_addr, max_packet_size, uint64(transfer_type) | (uint64(interval) << 8));
}

bool Devsman::RemoveUSBDevice(DeviceNode* parent, const char* name) {
	initialize_device_tree();
	if (!parent || !name) return false;
	auto* node = find_named_child(parent, DeviceNodeType::UsbDevice, name);
	if (!node) return false;
	if (!detach_child(parent, node)) return false;
	release_device_subtree(node);
	return true;
}

DeviceNode* Devsman::RegisterSerioController(const char* name) {
	initialize_device_tree();
	auto* root = resolve_default_serio_parent(name);
	if (!root || !name) return nullptr;
	return RegisterSerioController(root, name);
}

DeviceNode* Devsman::RegisterSerioController(DeviceNode* parent, const char* name) {
	initialize_device_tree();
	if (!parent || !name) return nullptr;
	if (auto* node = find_named_child(parent, DeviceNodeType::SerioController, name)) {
		bind_device_node(node);
		return node;
	}
	const auto parent_bus_type = DeviceBusType(parent->fields.bus_type);
	const auto node_bus_type = parent_bus_type == DeviceBusType::ISA ? DeviceBusType::ISA : DeviceBusType::Serio;
	auto* node = append_plain_device(parent, DeviceNodeType::SerioController, node_bus_type, StrHeap(name));
	bind_device_node(node);
	return node;
}

DeviceNode* Devsman::RegisterSerioDevice(DeviceNode* parent, const char* name) {
	initialize_device_tree();
	if (!parent || !name) return nullptr;
	if (auto* node = find_named_child(parent, DeviceNodeType::SerioDevice, name)) {
		bind_device_node(node);
		return node;
	}
	auto* node = append_plain_device(parent, DeviceNodeType::SerioDevice, DeviceBusType::Serio, StrHeap(name));
	bind_device_node(node);
	return node;
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
