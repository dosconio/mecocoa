// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] Device Management
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <cpp/Device/Bus/ISA.hpp>
#include <cpp/Device/Bus/PCI.hpp>
#include <c/storage/AHCI.h>
#include <c/storage/NVMe.h>
#include <cpp/System/Audiosys/AudioManager.hpp>
#if _MCCA == 0x8664
#include <cpp/Device/USB/xHCI/xHCI.hpp>
#endif
#if (_MCCA & 0xFF00) == 0x8600
#include "../include/devsman-storage.hpp"
#include "c/proctrl/IAx86_64.ext.h"
#include "c/proctrl/IAx86_64.msr.h"
#endif

extern RMOD_LIST __init_rmod_ento[], __init_rmod_endo[];

#if (_MCCA & 0xFFFF) == 0x2032
// ER_RMOD bounds of mcca.sct, exported by stm32h743.S
extern "C" char* RMOD_BoundBase();
extern "C" char* RMOD_BoundLimit();
#endif

#define mfence() _ASM volatile ("mfence":::"memory")

namespace Devs {

	DeviceTree device_tree;
	DeviceNode* device_root = nullptr;
	
	#if (_MCCA & 0xFF00) == 0x8600

	extern DeviceNode* pci_root;
	extern DeviceNode* primary_pci_bus;
	extern DeviceNode* pci_bus_nodes[256];
	extern bool pci_devices_attached;

	DeviceNode* legacy_isa_bus = nullptr;
	DeviceNode* bus_roots[9]{};
	

	struct NamedDriverMatchEntry {
		const char* node_name;
		const char* driver_name;
	};



	struct DriverStartHookEntry {
		const char* driver_name;
		Devsman::DriverStartRoutine starter;
	};



	constexpr NamedDriverMatchEntry platform_driver_match_table[] = {
		{"uart@com1", "uart-8250"},
		{"uart@com2", "uart-8250"},
		{"uart@com3", "uart-8250"},
		{"uart@com4", "uart-8250"},
		{"rtc@cmos", "rtc-cmos"},
		{"floppy@flc0", "flopdisk"},
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
			StrCompare(name, "uart@com2") == 0 ||
			StrCompare(name, "uart@com3") == 0 ||
			StrCompare(name, "uart@com4") == 0 ||
			StrCompare(name, "sound-blaster") == 0 ||
			StrCompare(name, "floppy@flc0") == 0;
	}

	bool is_x86_legacy_isa_serio_name(const char* name) {
		if (!name) return false;
		return StrCompare(name, "i8042") == 0;
	}



	bool probe_video_bochs_device(DeviceNode* node) {
		if (!node) return false;
		return Devsman::AddIoPortResource(node, 0, 0x01CE, 3);
	}

	bool probe_video_vmware_device(DeviceNode* node) {
		return node != nullptr;
	}

	bool is_e1000_device(const DeviceNode* node) {
		if (!node) return false;
		if (DeviceNodeType(node->fields.node_type) != DeviceNodeType::PciDevice) return false;
		if (node->fields.vendor_id != 0x8086u) return false;
		switch (node->fields.device_id) {
		case 0x100Eu:
		case 0x100Fu:
		case 0x1010u:
		case 0x10D3u:
			return true;
		default:
			return false;
		}
	}

	bool is_lance_pci_device(const DeviceNode* node) {
		if (!node) return false;
		if (DeviceNodeType(node->fields.node_type) != DeviceNodeType::PciDevice) return false;
		return node->fields.vendor_id == 0x1022u && node->fields.device_id == 0x2000u;
	}

	const DeviceResource* find_pci_io_bar(const DeviceNode* node) {
		if (!node) return nullptr;
		for (uint32 bar_index = 0; bar_index < 6; ++bar_index) {
			if (const auto* io = Devsman::FindResource(node, DeviceResourceType::PciBarIo, bar_index)) {
				return io;
			}
		}
		return nullptr;
	}

	bool probe_e1000_device(DeviceNode* node) {
		if (!node || !is_e1000_device(node)) return false;
		const auto* mmio = Devsman::FindResource(node, DeviceResourceType::PciBarMmio, 0);
		const auto* io = find_pci_io_bar(node);
		if (!mmio && !io) {
			plogwarn("[DEVSMAN] E1000 %s missing BAR resource",
				node->link.addr ? node->link.addr : "(unnamed)");
			return false;
		}
		const auto* irq = Devsman::FindResource(node, DeviceResourceType::IrqLine, 0);
		node->fields.binding.probe_result = irq ? 0 : 1;
		if (mmio) {
			ploginfo("[DEVSMAN] E1000 %s MMIO=%[64H] len=%[64H]%s",
				node->link.addr ? node->link.addr : "(unnamed)",
				mmio->start, mmio->length,
				irq ? "" : " irq=none");
		}
		else {
			ploginfo("[DEVSMAN] E1000 %s IO=%[64H]%s",
				node->link.addr ? node->link.addr : "(unnamed)",
				io->start,
				irq ? "" : " irq=none");
		}
		return true;
	}

	bool probe_lance_device(DeviceNode* node) {
		if (!node || !is_lance_pci_device(node)) return false;
		const auto* io = find_pci_io_bar(node);
		if (!io) {
			plogwarn("[DEVSMAN] LANCE %s missing I/O BAR resource",
				node->link.addr ? node->link.addr : "(unnamed)");
			return false;
		}
		const auto* irq = Devsman::FindResource(node, DeviceResourceType::IrqLine, 0);
		if (!irq) {
			plogwarn("[DEVSMAN] LANCE %s missing IRQ resource",
				node->link.addr ? node->link.addr : "(unnamed)");
			return false;
		}
		node->fields.binding.probe_result = 0;
		ploginfo("[DEVSMAN] LANCE %s I/O BAR%u=%[64H] len=%[64H] irq=%[64H]",
			node->link.addr ? node->link.addr : "(unnamed)",
			io->index, io->start, io->length, irq->start);
		return true;
	}

	bool probe_pata_device(DeviceNode* node) {
		if (!node) return false;
		const auto* bmide = Devsman::FindResource(node, DeviceResourceType::PciBarIo, 4);
		const auto* irq = Devsman::FindResource(node, DeviceResourceType::IrqLine, 0);
		node->fields.binding.probe_result = 0;
		if (bmide) {
			ploginfo("[DEVSMAN] PATA %s BMIDE=%[64H]%s",
				node->link.addr ? node->link.addr : "(unnamed)",
				bmide->start,
				irq ? "" : " irq=none");
		}
		else {
			ploginfo("[DEVSMAN] PATA %s (legacy IO ports)%s",
				node->link.addr ? node->link.addr : "(unnamed)",
				irq ? "" : " irq=none");
		}
		return true;
	}



	DriverStartHookEntry driver_start_hooks[32]{};
	stduint driver_start_hook_count = 0;

	void attach_builtin_node_ops(DeviceNode* node, void* driver_data);
	void probe_and_attach_pci_devices();
	DeviceNode* create_pci_bus_node(uint16 segment, uint8 bus);
	void start_pci_device(DeviceNode* node);
	void probe_pci_device(DeviceNode* node);
	void bind_pci_device(DeviceNode* node);



	uni::StorageTrait* storage_trait_from_node(DeviceNode* node) {
		if (!node) return nullptr;
		if (DeviceNodeType(node->fields.node_type) != DeviceNodeType::StorageDevice) return nullptr;
		return static_cast<uni::StorageTrait*>(node->fields.binding.driver_data);
	}

	stdsint storage_transfer(DeviceNode* node, void* raw_buf, stduint count, stduint idx, bool is_write) {
		auto* storage = storage_trait_from_node(node);
		if (!storage || !raw_buf || count == 0) return 0;
		const stduint block_size = storage->Block_Size ? storage->Block_Size : 1;
		const uint64 total_bytes = uint64(storage->getUnits()) * uint64(block_size);
		if (uint64(idx) >= total_bytes) return 0;
		uint64 remaining = minof(uint64(count), total_bytes - uint64(idx));
		byte* buf = static_cast<byte*>(raw_buf);
		byte* bounce = nullptr;
		stduint processed = 0;

		while (remaining) {
			const uint64 current_off = uint64(idx) + processed;
			const stduint block_id = stduint(current_off / block_size);
			const stduint block_off = stduint(current_off % block_size);
			const stduint chunk = stduint(minof(remaining, uint64(block_size - block_off)));
			const bool whole_block = block_off == 0 && chunk == block_size;
			byte* target_buf = buf + processed;

			if (whole_block) {
				const bool ok = is_write
					? storage->Write(block_id, target_buf)
					: storage->Read(block_id, target_buf);
				if (!ok) return processed ? (stdsint)processed : -1;
			}
			else {
				if (!bounce) {
					bounce = new byte[block_size];
					if (!bounce) return processed ? (stdsint)processed : -1;
				}
				if (!storage->Read(block_id, bounce)) {
					delete[] bounce;
					return processed ? (stdsint)processed : -1;
				}
				if (is_write) {
					MemCopyN(bounce + block_off, target_buf, chunk);
					if (!storage->Write(block_id, bounce)) {
						delete[] bounce;
						return processed ? (stdsint)processed : -1;
					}
				}
				else {
					MemCopyN(target_buf, bounce + block_off, chunk);
				}
			}

			processed += chunk;
			remaining -= chunk;
		}

		if (bounce) delete[] bounce;
		return processed;
	}

	stdsint storage_read(DeviceNode* node, void* buf, stduint count, stduint idx, stduint flags) {
		(void)flags;
		return storage_transfer(node, buf, count, idx, false);
	}

	stdsint storage_send(DeviceNode* node, const void* buf, stduint count, stduint idx, stduint flags) {
		(void)flags;
		return storage_transfer(node, const_cast<void*>(buf), count, idx, true);
	}

	stdsint storage_ctrl(DeviceNode* node, stduint cmd, void* args, stduint flags) {
		(void)flags;
		auto* storage = storage_trait_from_node(node);
		if (!storage) return -1;
		switch (DeviceCtrlCommand(cmd)) {
		case DeviceCtrlCommand::GetBlockSize:
			if (!args) return -1;
			*static_cast<stduint*>(args) = storage->Block_Size;
			return 0;
		case DeviceCtrlCommand::GetUnitCount:
			if (!args) return -1;
			*static_cast<stduint*>(args) = storage->getUnits();
			return 0;
		case DeviceCtrlCommand::GetByteSize:
			if (!args) return -1;
			*static_cast<uint64*>(args) = uint64(storage->Block_Size) * uint64(storage->getUnits());
			return 0;
		case DeviceCtrlCommand::GetBackingObject:
			if (!args) return -1;
			*static_cast<void**>(args) = storage;
			return 0;
		default:
			return -1;
		}
	}

	const DeviceNodeOps storage_device_ops_impl{
		.read = storage_read,
		.send = storage_send,
		.ctrl = storage_ctrl,
	};

	void attach_builtin_node_ops(DeviceNode* node, void* driver_data) {
		if (!node || !driver_data) return;
		if (DeviceNodeType(node->fields.node_type) == DeviceNodeType::StorageDevice) {
			node->fields.ops = &storage_device_ops_impl;
		}
	}

	void init_node(DeviceNode* node, DeviceNodeType node_type, DeviceBusType bus_type, const char* name) {
		MemSet(node, 0, sizeof(DeviceNode));
		node->link.addr = const_cast<char*>(name);
		node->fields.node_type = static_cast<uint16>(node_type);
		node->fields.bus_type = static_cast<uint16>(bus_type);
		node->fields.resource_capacity = DeviceNodeInlineResourceCapacity;
		node->fields.resources = node->inline_resources;
		node->fields.ops = nullptr;
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
		if (!parent->link.subf) {
			child_link->pare = &parent->link;
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
				if (prev) next_node->link.left = prev;
				else next_node->link.pare = &parent->link;
			}
			crt->next = nullptr;
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
		if (node->fields.dev_alias) {
			auto* text = const_cast<char*>(node->fields.dev_alias);
			mfree(text);
		}
		node->fields.text_manufacturer = nullptr;
		node->fields.text_product = nullptr;
		node->fields.text_serial = nullptr;
		node->fields.dev_alias = nullptr;
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

	bool append_resource(DeviceNode* node, DeviceResourceType type, uint16 flags, uint32 index, uint64 start, uint64 length, uint64 extra) {
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

	DeviceNode* find_named_node_in_subtree(DeviceNode* node, DeviceNodeType node_type, const char* name) {
		for (auto* crt = node; crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
			if (crt->fields.node_type == static_cast<uint16>(node_type) &&
				crt->link.addr &&
				StrCompare(crt->link.addr, name) == 0) {
				return crt;
			}
			if (crt->link.subf) {
				if (auto* found = find_named_node_in_subtree(reinterpret_cast<DeviceNode*>(crt->link.subf), node_type, name)) {
					return found;
				}
			}
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
		attach_builtin_node_ops(node, driver_data);
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

	const NamedDriverMatchEntry* match_named_driver(const DeviceNode* node,
		const NamedDriverMatchEntry* table, stduint count) {
		if (!node || !node->link.addr || !table) return nullptr;
		for0(i, count) {
			if (StrCompare(node->link.addr, table[i].node_name) == 0) return &table[i];
		}
		return nullptr;
	}

	const DriverOpsEntry* find_driver_ops(const char* driver_name, const DriverOpsEntry* table, stduint count) {
		if (!driver_name || !table) return nullptr;
		for0(i, count) {
			if (StrCompare(driver_name, table[i].driver_name) == 0) return &table[i];
		}
		return nullptr;
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
		if (!pci_node->fields.text_manufacturer) {
			if (const char* vendor_name = Devsman::LookupPciVendorName(pci_node->fields.vendor_id)) {
				pci_node->fields.text_manufacturer = StrHeap(vendor_name);
			}
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
		if (auto* fdc = Devsman::RegisterPlatformDevice(legacy_bus, "floppy@flc0", "flopdisk")) {
			Devsman::AddIoPortResource(fdc, 0, 0x3F0, 8);
			Devsman::AddIrqResource(fdc, IRQ_PIT + 6);
			Devsman::AddDmaResource(fdc, 0, 2, 8);
			auto* floppy0 = Devsman::RegisterStorageDevice(fdc, "floppy@0", DeviceBusType::ISA);
			auto* floppy1 = Devsman::RegisterStorageDevice(fdc, "floppy@1", DeviceBusType::ISA);
			Devsman::RegisterDevAlias(floppy0, "fd0");
			Devsman::RegisterDevAlias(floppy1, "fd1");
		}
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


	#endif
}

using namespace Devs;

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
	
	#if (_MCCA & 0xFF00) == 0x8600 || (_MCCA & 0xFFFF) == 0x2032
	#if (_MCCA & 0xFFFF) == 0x2032
	// ARM: ER_RMOD of mcca.sct holds .init.rmod, its bounds come from the linker
	auto func = (RMOD_LIST*)RMOD_BoundBase();
	auto rmod_endo = (RMOD_LIST*)RMOD_BoundLimit();
	#else
	auto func = __init_rmod_ento;
	auto rmod_endo = __init_rmod_endo;
	#endif
	for (; func < rmod_endo; func++) {
		ploginfo("Loading %s", func->name);
		(func->init)();
	}
	#endif

	return true;
}

DeviceNode* Devsman::Root() {
	return device_root;
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



#if (_MCCA & 0xFF00) == 0x8600

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
		else attach_builtin_node_ops(node, node->fields.binding.driver_data);
		return node;
	}
	auto* node = append_plain_device(parent, DeviceNodeType::StorageDevice, bus_type, StrHeap(name));
	if (driver_name) set_driver_started(node, driver_name, driver_data);
	else attach_builtin_node_ops(node, node->fields.binding.driver_data);
	return node;
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

bool Devsman::AddDmaResource(DeviceNode* node, uint32 index, uint8 channel, uint8 width_bits) {
	if (!node || channel > 7 || (width_bits != 8 && width_bits != 16)) return false;
	if (find_resource(node, DeviceResourceType::DmaChannel, index)) return true;
	return append_resource(node, DeviceResourceType::DmaChannel,
		DeviceResourceFlag_None, index, channel, 1, width_bits);
}



DeviceNode* Devsman::FindNamedNode(DeviceNodeType node_type, const char* name) {
	if (!device_root || !name) return nullptr;
	return find_named_node_in_subtree(device_root, node_type, name);
}

#endif

namespace {
	bool IsValidDevAlias(const char* alias) {
		if (!alias || !alias[0] ||
			(alias[0] == '.' && (!alias[1] || (alias[1] == '.' && !alias[2])))) return false;
		stduint length = 0;
		for (const char* crt = alias; *crt; ++crt, ++length) {
			if (length + 1 >= DeviceAliasNameCapacity) return false;
			if (*crt == '/' || *crt == '\\') return false;
		}
		return StrCompare(alias, "tty") != 0 && StrCompare(alias, "pts") != 0;
	}

	DeviceNode* FindDevAliasInSubtree(DeviceNode* node, const char* alias) {
		for (auto* crt = node; crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
			if (crt->fields.dev_alias && StrCompare(crt->fields.dev_alias, alias) == 0) return crt;
			if (crt->link.subf) {
				if (auto* match = FindDevAliasInSubtree(
					reinterpret_cast<DeviceNode*>(crt->link.subf), alias)) return match;
			}
		}
		return nullptr;
	}

	bool DeviceSubtreeContains(DeviceNode* node, const DeviceNode* target) {
		for (auto* crt = node; crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
			if (crt == target) return true;
			if (crt->link.subf && DeviceSubtreeContains(
				reinterpret_cast<DeviceNode*>(crt->link.subf), target)) return true;
		}
		return false;
	}

	DeviceNode* FindBoundNodeInSubtree(DeviceNode* node, const char* driver_name) {
		for (auto* crt = node; crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
			if (crt->fields.binding.driver_name &&
				StrCompare(crt->fields.binding.driver_name, driver_name) == 0) return crt;
			if (crt->link.subf) {
				if (auto* match = FindBoundNodeInSubtree(
					reinterpret_cast<DeviceNode*>(crt->link.subf), driver_name)) return match;
			}
		}
		return nullptr;
	}
}

bool Devsman::RegisterDevAlias(DeviceNode* node, const char* alias) {
	if (!node || !device_root || !DeviceSubtreeContains(device_root, node) || !IsValidDevAlias(alias)) return false;
	if (node->fields.dev_alias) return StrCompare(node->fields.dev_alias, alias) == 0;
	if (FindByDevAlias(alias)) return false;
	node->fields.dev_alias = StrHeap(alias);
	return node->fields.dev_alias != nullptr;
}

const char* Devsman::GetDevAlias(const DeviceNode* node) {
	return node ? node->fields.dev_alias : nullptr;
}

DeviceNode* Devsman::FindByDevAlias(const char* alias) {
	return device_root && IsValidDevAlias(alias) ? FindDevAliasInSubtree(device_root, alias) : nullptr;
}

DeviceNode* Devsman::FindBoundNode(const char* driver_name) {
	return device_root && driver_name ? FindBoundNodeInSubtree(device_root, driver_name) : nullptr;
}

namespace {
	DeviceNode* FindOwnedNodeInSubtree(DeviceNode* node, stduint pid) {
		for (auto* crt = node; crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
			if (crt->fields.binding.owner_pid == pid) return crt;
			if (crt->link.subf) {
				if (auto* match = FindOwnedNodeInSubtree(
					reinterpret_cast<DeviceNode*>(crt->link.subf), pid)) return match;
			}
		}
		return nullptr;
	}
}

DeviceNode* Devsman::FindOwnedNode(stduint pid) {
	return device_root && pid ? FindOwnedNodeInSubtree(device_root, pid) : nullptr;
}

#if (_MCCA & 0xFF00) == 0x8600



const DeviceResource* Devsman::FindResource(const DeviceNode* node, DeviceResourceType type, uint32 index) {
	return find_resource(node, type, index);
}

#endif

bool Devsman::SetOps(DeviceNode* node, const DeviceNodeOps* ops) {
	if (!node) return false;
	node->fields.ops = ops;
	return true;
}

const DeviceNodeOps* Devsman::GetOps(const DeviceNode* node) {
	return node ? node->fields.ops : nullptr;
}

stdsint Devsman::Read(DeviceNode* node, void* buf, stduint count, stduint idx, stduint flags) {
	if (!node || !node->fields.ops || !node->fields.ops->read) return -1;
	return node->fields.ops->read(node, buf, count, idx, flags);
}

stdsint Devsman::Send(DeviceNode* node, const void* buf, stduint count, stduint idx, stduint flags) {
	if (!node || !node->fields.ops || !node->fields.ops->send) return -1;
	return node->fields.ops->send(node, buf, count, idx, flags);
}

stdsint Devsman::Ctrl(DeviceNode* node, stduint cmd, void* args, stduint flags) {
	if (!node || !node->fields.ops || !node->fields.ops->ctrl) return -1;
	return node->fields.ops->ctrl(node, cmd, args, flags);
}

#if (_MCCA & 0xFF00) == 0x8600

namespace {
	void RegisterStoragePartitionAlias(DeviceNode* parent, DeviceNode* partition, stduint part_dev) {
		const char* parent_alias = Devsman::GetDevAlias(parent);
		if (!parent_alias || !partition) return;
		const stduint length = StrLength(parent_alias);
		const bool needs_p = length && parent_alias[length - 1] >= '0' && parent_alias[length - 1] <= '9';
		String alias;
		alias.Format(needs_p ? "%sp%u" : "%s%u", parent_alias, part_dev);
		Devsman::RegisterDevAlias(partition, alias.reference());
	}
}

bool Devsman::AttachStorageOps(DeviceNode* node, uni::StorageTrait* storage) {
	if (!node || !storage) return false;
	if (DeviceNodeType(node->fields.node_type) != DeviceNodeType::StorageDevice) return false;
	node->fields.binding.driver_data = storage;
	attach_builtin_node_ops(node, storage);
	return true;
}

DeviceNode* Devsman::RegisterStoragePartition(DeviceNode* parent, const char* name,
	uni::StorageTrait& storage, stdsint part_dev, const char* driver_name) {
	initialize_device_tree();
	if (!parent || !name || part_dev <= 0) return nullptr;
	if (auto* node = find_named_child(parent, DeviceNodeType::StorageDevice, name)) {
		if (!node->fields.binding.driver_data) {
			auto* part = new uni::DiscPartition(storage, part_dev);
			set_driver_started(node, driver_name, part);
		}
		else {
			attach_builtin_node_ops(node, node->fields.binding.driver_data);
		}
		RegisterStoragePartitionAlias(parent, node, stduint(part_dev));
		return node;
	}
	auto bus_type = DeviceBusType(parent->fields.bus_type);
	if (bus_type == DeviceBusType::None) bus_type = DeviceBusType::Platform;
	auto* part = new uni::DiscPartition(storage, part_dev);
	auto* node = append_plain_device(parent, DeviceNodeType::StorageDevice, bus_type, StrHeap(name));
	set_driver_started(node, driver_name, part);
	RegisterStoragePartitionAlias(parent, node, stduint(part_dev));
	return node;
}




#endif

namespace {
	struct DevsmanServicePayload {
		alignas(stduint) byte data[64] = {};
	};
	#if (_MCCA & 0xFF00) == 0x8600
	static_assert(sizeof(StorageDriverInfo) <= sizeof(DevsmanServicePayload),
		"StorageDriverInfo exceeds Devsman receive buffer");
	#endif

	struct DriverProcessRecord {
		stduint pid = 0;
		uint32 restart_count = 0;
		bool restart_on_failure = true;
		char path[96] = {};
		char name[48] = {};

		bool operator==(const DriverProcessRecord& other) const {
			return StrCompare(path, other.path) == 0;
		}
	};

	struct PciRingDriverMatchEntry {
		uint16 vendor_id;
		uint16 device_id;
		const char* filename;
	};

	constexpr PciRingDriverMatchEntry pci_ring_driver_match_table[] = {
		{0x1234u, 0x1111u, "video-bochs.elf"},
		{0x8086u, 0x100Eu, "e1000.elf"},
		{0x8086u, 0x100Fu, "e1000.elf"},
		{0x8086u, 0x1010u, "e1000.elf"},
		{0x8086u, 0x10D3u, "e1000.elf"},
		{0x1022u, 0x2000u, "lance.elf"},
	};

	constexpr const char* always_load_ring_drivers[] = {
		"flopdisk.elf",
	};

	constexpr uint32 DriverRestartLimit = 0;
	uni::Vector<DriverProcessRecord> driver_processes;
	bool driver_directory_ready = false;

	void CopyDriverText(char* output, stduint capacity, const char* input) {
		if (!output || !capacity) return;
		stduint index = 0;
		if (input) {
			while (index + 1 < capacity && input[index]) {
				output[index] = input[index];
				index++;
			}
		}
		output[index] = '\0';
	}

	DriverProcessRecord* FindDriverProcess(stduint pid) {
		for0(i, driver_processes.Count()) {
			if (driver_processes[i].pid == pid) return &driver_processes[i];
		}
		return nullptr;
	}

	bool HasMatchingPciDevice(DeviceNode* node, const PciRingDriverMatchEntry& entry) {
		for (auto* crt = node; crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
			if (DeviceNodeType(crt->fields.node_type) == DeviceNodeType::PciDevice &&
				crt->fields.vendor_id == entry.vendor_id &&
				crt->fields.device_id == entry.device_id) {
				return true;
			}
			if (crt->link.subf && HasMatchingPciDevice(
				reinterpret_cast<DeviceNode*>(crt->link.subf), entry)) {
				return true;
			}
		}
		return false;
	}

	bool ShouldLoadRingDriver(const char* filename) {
		if (!filename) return false;
		for0(i, numsof(always_load_ring_drivers)) {
			if (StrCompare(filename, always_load_ring_drivers[i]) == 0) return true;
		}
		for0(i, numsof(pci_ring_driver_match_table)) {
			const auto& entry = pci_ring_driver_match_table[i];
			if (StrCompare(filename, entry.filename) == 0 &&
				HasMatchingPciDevice(Devsman::Root(), entry)) {
				return true;
			}
		}
		return false;
	}

	bool LaunchDriverProcess(DriverProcessRecord& record) {
		#if (_MCCA & 0xFF00) == 0x8600
		constexpr byte drv_ring = 1;
		#else
		constexpr byte drv_ring = RING_M;
		#endif
		ProcessBlock* task = Taskman::CreateFile(record.path, drv_ring, Task_Devsman);
		if (!task) {
			plogwarn("[Devsman] load driver failed: %s", record.path);
			return false;
		}
		char driver_task_name[48] = {};
		CopyDriverText(driver_task_name, sizeof(driver_task_name), record.name);
		for (stduint i = 0; driver_task_name[i]; ++i) {
			if (driver_task_name[i] == '.') {
				driver_task_name[i] = '\0';
				break;
			}
		}
		task->main_thread->name.reset(StrHeap(driver_task_name[0] ? driver_task_name : record.name));
		{
			auto focus_tty = task->focus_tty.Lock();
			*focus_tty = vttys[0];
			if (*focus_tty) {
				task->Open("/dev/tty", O_RDWR);
				task->Open("/dev/tty", O_RDWR);
				task->Open("/dev/tty", O_RDWR);
			}
		}
		Taskman::Append(task);
		if (auto* node = Devsman::FindBoundNode(driver_task_name))
			node->fields.binding.owner_pid = uint32(task->pid);
		else if (StrCompare(driver_task_name, "flopdisk") == 0)
			plogwarn("[Devsman] no device bound for flopdisk pid=%u", task->pid);
		Taskman::AppendThread(task->main_thread);
		record.pid = task->pid;
		ploginfo("[Devsman] load driver: %s pid=%u", record.path, task->pid);
		return true;
	}

	bool RegisterDriverProcess(const char* path, const char* name) {
		for0(i, driver_processes.Count()) {
			if (StrCompare(driver_processes[i].path, path) == 0) return true;
		}
		DriverProcessRecord record = {};
		CopyDriverText(record.path, sizeof(record.path), path);
		CopyDriverText(record.name, sizeof(record.name), name);
		driver_processes.Append(record);
		auto& stored = driver_processes[driver_processes.Count() - 1];
		if (LaunchDriverProcess(stored)) return true;
		driver_processes.Remove(driver_processes.Count() - 1);
		return false;
	}

	bool TryLoadDriverDirectory() {
		if (driver_directory_ready) return true;
		constexpr stduint drv_batch_count = 8;
		dirent_t entries[drv_batch_count];
		auto* current = Taskman::CurrentTB();
		if (!current || !current->parent_process) return false;
		const auto& vroot = Filesys::GetSystemVirtualRootPath();
		if (!vroot.getByteCount()) return false;

		String drv_dir_str = String::newFormat("%s/drvs", vroot.reference());
		const char* drv_dir = drv_dir_str.reference();
		stdsint fd = -1;
		struct {
			stduint flag;
			stduint tid;
			rostr usr_filepath;
		} open_msg = { O_RDONLY | O_DIRECTORY, current->getID(), drv_dir };
		syssend(Task_FileSys, &open_msg, sizeof(open_msg), _IMM(FilemanMsg::OPEN));
		sysrecv(Task_FileSys, &fd, sizeof(fd));
		if (fd < 0) return false;

		const stduint drv_dir_len = StrLength(drv_dir);
		struct {
			stduint fd;
			stduint pid;
		} close_msg = { (stduint)fd, current->parent_process->pid };
		for (;;) {
			MemSet(entries, 0, sizeof(entries));
			stduint enum_msg[4] = {
				(stduint)fd,
				_IMM(entries),
				drv_batch_count,
				current->getID()
			};
			syssend(Task_FileSys, enum_msg, byteof(enum_msg), _IMM(FilemanMsg::ENUMER));
			sysrecv(Task_FileSys, enum_msg, byteof(enum_msg[0]));
			const stdsint count = (stdsint)enum_msg[0];
			if (count <= 0) break;

			for (stdsint i = 0; i < count; ++i) {
				if (entries[i].is_dir || !entries[i].name[0] || entries[i].name[0] == '.') continue;
				if (!ShouldLoadRingDriver(entries[i].name)) continue;

				char path[96] = {};
				StrCopy(path, drv_dir);
				path[drv_dir_len] = '/';
				StrCopy(path + drv_dir_len + 1, entries[i].name);
				(void)RegisterDriverProcess(path, entries[i].name);
			}
		}

		stdsint close_ret = -1;
		syssend(Task_FileSys, &close_msg, sizeof(close_msg), _IMM(FilemanMsg::CLOSE));
		sysrecv(Task_FileSys, &close_ret, sizeof(close_ret));
		driver_directory_ready = true;
		return true;
	}

	stduint ReleaseDriverBindings(DeviceNode* node, stduint pid, stdsint exit_status) {
		stduint released = 0;
		for (auto* crt = node; crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
			if (crt->fields.binding.owner_pid == uint32(pid)) {
				crt->fields.binding.owner_pid = 0;
				crt->fields.binding.driver_data = nullptr;
				crt->fields.binding.state = static_cast<uint32>(
					exit_status == 0 ? DriverBindingState::Probed : DriverBindingState::Failed);
				crt->fields.binding.probe_result = int32(exit_status);
				released++;
			}
			if (crt->link.subf) {
				released += ReleaseDriverBindings(
					reinterpret_cast<DeviceNode*>(crt->link.subf), pid, exit_status);
			}
		}
		return released;
	}

	void DispatchDriverLifecycle(const TaskLifecycleEvent& event) {
		if (event.version != TaskLifecycleProtocolVersion ||
			TaskLifecycleEventKind(event.kind) != TaskLifecycleEventKind::Exited ||
			event.parent_pid != Task_Devsman) {
			plogwarn("[Devsman] invalid task lifecycle event");
			return;
		}
		auto* record = FindDriverProcess(event.pid);
		#if (_MCCA & 0xFF00) == 0x8600
		Powercall::StorageAdapter::DriverExited(event.pid);
		#endif
		const stduint released = ReleaseDriverBindings(Devsman::Root(), event.pid, event.exit_status);
		ploginfo("[Devsman] driver exited pid=%u status=%d devices=%u",
			event.pid, event.exit_status, released);
		if (!record) {
			plogwarn("[Devsman] driver exit has no process record pid=%u", event.pid);
			return;
		}
		record->pid = 0;
		if (!record->restart_on_failure || event.exit_status == 0) return;
		if (record->restart_count >= DriverRestartLimit) {
			plogwarn("[Devsman] driver restart limit reached: %s", record->path);
			return;
		}
		record->restart_count++;
		plogwarn("[Devsman] restarting driver: %s attempt=%u/%u",
			record->path, record->restart_count, DriverRestartLimit);
		(void)LaunchDriverProcess(*record);
	}

	void DispatchKernelDeviceEvent(const DeviceEvent& event) {
		if (event.version != DeviceEventProtocolVersion) {
			plogwarn("[Devsman] invalid device event version=%u", event.version);
			return;
		}
		switch (DeviceEventKind(event.kind)) {
		case DeviceEventKind::Removed:
		case DeviceEventKind::Fault:
		case DeviceEventKind::Shutdown:
			ploginfo("[Devsman] device event kind=%u handle=%u source=%u count=%u",
				event.kind, event.device_handle, event.source, event.count);
			break;
		case DeviceEventKind::ConsoleWake:
			Consman::DispatchDeferredWake();
			break;
		case DeviceEventKind::TimerExpired:
			Systimex::DispatchExpired();
			break;
		case DeviceEventKind::Interrupt:
			#if _MCCA == 0x8664 && defined(_UEFI)
			if (Devsman::ProcessXHCIEvent(event)) break;
			#endif
			plogwarn("[Devsman] unclaimed interrupt event handle=%u source=%u", event.device_handle, event.source);
			break;
		default:
			plogwarn("[Devsman] unknown device event kind=%u", event.kind);
			break;
		}
	}

	void DispatchDevsmanRequest(stduint type, stduint source) {
		switch (DevsmanMsg(type)) {
		case DevsmanMsg::TEST:
			break;
		case DevsmanMsg::BIND_DRIVERS:
			Devsman::BindKnownDrivers();
			break;
		case DevsmanMsg::PROBE_DRIVERS:
			Devsman::ProbeKnownDrivers();
			break;
		case DevsmanMsg::START_DRIVERS:
			Devsman::StartKnownDrivers();
			break;
		case DevsmanMsg::LOAD_DRIVER_DIRECTORY:
			(void)TryLoadDriverDirectory();
			break;
		default:
			plogwarn("[Devsman] unknown request type=%u source=%u", type, source);
			break;
		}
	}

	void RunDevsmanMessageLoop() {
		for (;;) {
			DevsmanServicePayload payload = {};
			stduint type = 0;
			stduint source = 0;
			if (sysrecv(ANYPROC, payload.data, sizeof(payload.data), &type, &source)) continue;

			if (type == _IMM(KernelMsg::TaskLifecycle)) {
				TaskLifecycleEvent event = {};
				MemCopyN(&event, payload.data, sizeof(event));
				DispatchDriverLifecycle(event);
			}
			else if (type == _IMM(KernelMsg::DeviceEvent)) {
				DeviceEvent event = {};
				MemCopyN(&event, payload.data, sizeof(event));
				DispatchKernelDeviceEvent(event);
			}
			#if (_MCCA & 0xFF00) == 0x8600
			else if (type == _IMM(StorageDriverMsg::Attach)) {
				StorageDriverInfo info = {};
				MemCopyN(&info, payload.data, sizeof(info));
				auto* adapter = Powercall::StorageAdapter::Register(source, info);
				StorageDriverReply reply = {};
				reply.status = adapter ? 0 : -1;
				if (adapter) {
					ploginfo("[Devsman] storage attached tid=%u unit=%u name=%s media=%u",
						source, info.unit, info.name, adapter->HasMedia());
				}
				else {
					plogwarn("[Devsman] storage attach rejected tid=%u unit=%u name=%s",
						source, info.unit, info.name);
				}
				if (syssend_async(source, &reply, sizeof(reply), _IMM(StorageDriverMsg::Attach))) {
					plogwarn("[Devsman] storage attach reply failed tid=%u", source);
				}
				else if (adapter && adapter->HasMedia()) {
					DeviceNode* node = adapter->GetNode();
					if (syssend_async(Task_FileSys, &node, sizeof(node), _IMM(FilemanMsg::STORAGE_READY)))
						plogwarn("[Devsman] storage mount notice failed tid=%u", source);
				}
			}
			#endif
			else {
				DispatchDevsmanRequest(type, source);
			}
		}
	}
}

stduint Devsman::GetDriverStartHookCount() {
	#if (_MCCA & 0xFF00) == 0x8600
	return driver_start_hook_count;
	#else
	return 0;
	#endif
}

const char* Devsman::GetDriverStartHookName(stduint index) {
	#if (_MCCA & 0xFF00) == 0x8600
	if (index >= driver_start_hook_count) return nullptr;
	return driver_start_hooks[index].driver_name;
	#else
	(void)index;
	return nullptr;
	#endif
}

stduint Devsman::GetDriverProcessCount() {
	return driver_processes.Count();
}

const char* Devsman::GetDriverProcessName(stduint index) {
	if (index >= driver_processes.Count()) return nullptr;
	return driver_processes[index].name;
}

void serv_devs_loop() {
	auto current = Taskman::CurrentTB();
	if (!current || !current->parent_process) return;
	if (!device_event_prepare(current)) {
		plogerro("[Devsman] failed to initialize device event queue");
	}
	#if _MCCA == 0x8664 && defined(_UEFI)
	else if (!Devsman::BindXHCIEventOwner(current->parent_process->pid, current->tid)) {
		plogerro("[Devsman] failed to bind xHCI interrupt events");
	}
	#endif
	RunDevsmanMessageLoop();
}

#if (_MCCA & 0xFF00) == 0x8600
uni::AudioManager audio_manager;
#endif
