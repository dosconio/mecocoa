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
#include <cpp/Device/ACPI.hpp>
#include <cpp/Device/USB/xHCI/xHCI.hpp>
#endif
#if (_MCCA & 0xFF00) == 0x8600
#include "../include/devsman-storage.hpp"
#include "../devdriv/vmhost/vmci.hpp"
#include "c/proctrl/IAx86_64.ext.h"
#include "c/proctrl/IAx86_64.msr.h"
#endif

#if _MCCA == 0x8664
extern "C" void Handint_Double_Fault_Emergency_Entry();
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

	bool is_vmci_pci_device(const DeviceNode* node) {
		if (!node) return false;
		if (DeviceNodeType(node->fields.node_type) != DeviceNodeType::PciDevice) return false;
		return node->fields.vendor_id == 0x15ADu && node->fields.device_id == 0x0740u;
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
		plogtrac("[DEVSMAN] LANCE %s I/O BAR%u=%[64H] len=%[64H] irq=%[64H]",
			node->link.addr ? node->link.addr : "(unnamed)",
			io->index, io->start, io->length, irq->start);
		return true;
	}

	bool probe_vmci_device(DeviceNode* node) {
		if (!node || !is_vmci_pci_device(node)) return false;
		constexpr uint64 VmciRegisterWindowMinimum = 0x1Cu;
		constexpr uint64 VmciMmioRegisterBarSize = 256u * 1024u;
		const auto* io = Devsman::FindResource(node, DeviceResourceType::PciBarIo, 0);
		const auto* mmio = Devsman::FindResource(node, DeviceResourceType::PciBarMmio, 1);
		const bool usable_io = io && io->length >= VmciRegisterWindowMinimum;
		const bool usable_mmio = mmio && mmio->length == VmciMmioRegisterBarSize;
		if (!usable_io && !usable_mmio) {
			plogwarn("[DEVSMAN] VMCI %s has no usable register BAR",
				node->link.addr ? node->link.addr : "(unnamed)");
			return false;
		}
		const auto* irq = Devsman::FindResource(node, DeviceResourceType::IrqLine, 0);
		#if _MCCA == 0x8632
		if (!irq) {
			plogwarn("[DEVSMAN] VMCI %s missing IRQ resource",
				node->link.addr ? node->link.addr : "(unnamed)");
			return false;
		}
		#endif
		if (irq && io) {
			plogtrac("[DEVSMAN] VMCI %s I/O BAR%u=%[64H] len=%[64H] irq=%[64H]",
				node->link.addr ? node->link.addr : "(unnamed)",
				io->index, io->start, io->length, irq->start);
		}
		if (irq && mmio) {
			plogtrac("[DEVSMAN] VMCI %s MMIO BAR%u=%[64H] len=%[64H] irq=%[64H]",
				node->link.addr ? node->link.addr : "(unnamed)",
				mmio->index, mmio->start, mmio->length, irq->start);
		}
		node->fields.binding.probe_result = 0;
		return true;
	}

	bool probe_pata_device(DeviceNode* node) {
		if (!node) return false;
		const auto* bmide = Devsman::FindResource(node, DeviceResourceType::PciBarIo, 4);
		const auto* irq = Devsman::FindResource(node, DeviceResourceType::IrqLine, 0);
		node->fields.binding.probe_result = 0;
		if (bmide) {
			plogtrac("[DEVSMAN] PATA %s BMIDE=%[64H]%s",
				node->link.addr ? node->link.addr : "(unnamed)",
				bmide->start,
				irq ? "" : " irq=none");
		}
		else {
			plogtrac("[DEVSMAN] PATA %s (legacy IO ports)%s",
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
		plogtrac("[DEVSMAN] ISA bridge %s on %s -> %s",
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
				plogtrac("[DEVSMAN] Reparented %s under %s",
					secondary_bus_node->link.addr, dev_node->link.addr);
			}
		}
	}

	#if _MCCA == 0x8664
	struct IoApicTopologyEntry {
		uint32 address;
		uint32 gsi_base;
		uint32 gsi_count;
		uint8 id;
	};

	struct InterruptSourceOverrideEntry {
		uint32 gsi;
		uint16 flags;
		uint8 source;
	};

	struct AcpiPciRoutingEntry {
		uint32 address;
		uint32 gsi;
		uint32 source_name;
		uint32 source_index;
		uint16 segment;
		uint16 irq_flags;
		uint8 root_bus;
		uint8 pin;
		bool resolved;
	};

	struct AcpiIrqResource {
		uint32 gsi;
		uint16 flags;
	};

	struct AmlTableView {
		const uint8* aml;
		stduint length;
	};

	static_assert(sizeof(uni::ACPI::MADTIOAPIC) == 12, "MADT I/O APIC entry size changed");
	static_assert(sizeof(uni::ACPI::MADTInterruptSourceOverride) == 10,
		"MADT interrupt source override entry size changed");

	constexpr stduint IoApicTopologyCapacity = 8;
	constexpr stduint InterruptSourceOverrideCapacity = 16;
	constexpr stduint AcpiPciRoutingCapacity = 512;
	constexpr stduint AmlTableCapacity = 64;
	IoApicTopologyEntry ioapic_topology[IoApicTopologyCapacity] = {};
	InterruptSourceOverrideEntry interrupt_source_overrides[InterruptSourceOverrideCapacity] = {};
	AcpiPciRoutingEntry acpi_pci_routes[AcpiPciRoutingCapacity] = {};
	AmlTableView aml_tables[AmlTableCapacity] = {};
	stduint ioapic_topology_count = 0;
	stduint interrupt_source_override_count = 0;
	stduint acpi_pci_route_count = 0;
	stduint aml_table_count = 0;
	bool acpi_pic_apic_mode = false;
	uint32 acpi_pic_target_name = 0;

	uint32 read_x64_ioapic_register(uint32 address, uint8 index) {
		auto* selector = reinterpret_cast<volatile uint32*>(stduint(address));
		auto* data = reinterpret_cast<volatile uint32*>(stduint(address) + 0x10u);
		*selector = index;
		mfence();
		const uint32 value = *data;
		mfence();
		return value;
	}

	void write_x64_ioapic_register(uint32 address, uint8 index, uint32 value) {
		auto* selector = reinterpret_cast<volatile uint32*>(stduint(address));
		auto* data = reinterpret_cast<volatile uint32*>(stduint(address) + 0x10u);
		*selector = index;
		mfence();
		*data = value;
		mfence();
	}

	bool append_x64_ioapic(uint32 address, uint32 gsi_base, uint8 id) {
		if (!address || (address & 0xFFFu) ||
			ioapic_topology_count >= IoApicTopologyCapacity) return false;
		const uint32 version = read_x64_ioapic_register(address, 1);
		const uint32 gsi_count = ((version >> 16) & 0xFFu) + 1u;
		if (!gsi_count || gsi_count > 120u ||
			gsi_base > ~uint32(0) - (gsi_count - 1u)) return false;
		const uint32 gsi_end = gsi_base + gsi_count;
		for0(index, ioapic_topology_count) {
			const auto& existing = ioapic_topology[index];
			const uint32 existing_end = existing.gsi_base + existing.gsi_count;
			if (existing.address == address ||
				(gsi_base < existing_end && existing.gsi_base < gsi_end)) return false;
		}
		ioapic_topology[ioapic_topology_count++] = {
			address, gsi_base, gsi_count, id };
		ploginfo("[ACPI] I/O APIC id=%u MMIO=%[32H] GSI=%u..%u",
			id, address, gsi_base, gsi_end - 1u);
		return true;
	}

	IoApicTopologyEntry* find_x64_ioapic(uint32 gsi) {
		for0(index, ioapic_topology_count) {
			auto& ioapic = ioapic_topology[index];
			if (gsi >= ioapic.gsi_base &&
				gsi - ioapic.gsi_base < ioapic.gsi_count) return &ioapic;
		}
		return nullptr;
	}

	bool x64_ioapic_has_gsi(uint32 gsi) {
		return find_x64_ioapic(gsi) != nullptr;
	}

	bool x64_ioapic_read_route(uint32 gsi, uint64& route) {
		auto* ioapic = find_x64_ioapic(gsi);
		if (!ioapic) return false;
		const uint32 pin = gsi - ioapic->gsi_base;
		const uint8 index = uint8(0x10u + pin * 2u);
		route = uint64(read_x64_ioapic_register(ioapic->address, uint8(index + 1u))) << 32;
		route |= read_x64_ioapic_register(ioapic->address, index);
		return true;
	}

	bool x64_ioapic_write_route(uint32 gsi, uint64 route) {
		auto* ioapic = find_x64_ioapic(gsi);
		if (!ioapic) return false;
		const uint32 pin = gsi - ioapic->gsi_base;
		const uint8 index = uint8(0x10u + pin * 2u);
		write_x64_ioapic_register(ioapic->address, index,
			uint32(route) | uint32(1u << 16));
		write_x64_ioapic_register(ioapic->address, uint8(index + 1u), uint32(route >> 32));
		write_x64_ioapic_register(ioapic->address, index, uint32(route));
		return true;
	}

	const uni::ACPI::FADT* find_x64_fadt() {
		constexpr uint32 MinimumFadtLength =
			offsetof(uni::ACPI::FADT, dsdt) + sizeof(uint32);
		if (!acpi_rsdp_addr) return nullptr;
		const auto* rsdp = reinterpret_cast<const uni::ACPI::RSDP*>(acpi_rsdp_addr);
		if (!rsdp->isValid()) return nullptr;
		if (rsdp->revision >= 2 && rsdp->xsdt_address) {
			const auto* xsdt = reinterpret_cast<const uni::ACPI::XSDT*>(
				stduint(rsdp->xsdt_address));
			if (xsdt->header.length >= sizeof(uni::ACPI::DescriptionHeader) &&
				xsdt->header.isValid("XSDT")) {
				for (size_t index = 0; index < xsdt->Count(); ++index) {
					const auto& table = (*xsdt)[index];
					if (table.length >= MinimumFadtLength &&
						table.isValid("FACP")) {
						return reinterpret_cast<const uni::ACPI::FADT*>(&table);
					}
				}
			}
		}
		if (!rsdp->rsdt_address) return nullptr;
		const auto* rsdt = reinterpret_cast<const uni::ACPI::RSDT*>(
			stduint(rsdp->rsdt_address));
		if (rsdt->header.length < sizeof(uni::ACPI::DescriptionHeader) ||
			!rsdt->header.isValid("RSDT")) return nullptr;
		for (size_t index = 0; index < rsdt->Count(); ++index) {
			const auto& table = (*rsdt)[index];
			if (table.length >= MinimumFadtLength &&
				table.isValid("FACP")) {
				return reinterpret_cast<const uni::ACPI::FADT*>(&table);
			}
		}
		return nullptr;
	}

	bool append_x64_aml_table(const uni::ACPI::DescriptionHeader* table,
		const char* signature) {
		constexpr uint32 MaximumAmlTableLength = 2u * 1024u * 1024u;
		if (!table || table->length < sizeof(*table) ||
			table->length > MaximumAmlTableLength || !table->isValid(signature)) return false;
		const auto* aml = reinterpret_cast<const uint8*>(table + 1);
		for0(index, aml_table_count) {
			if (aml_tables[index].aml == aml) return true;
		}
		if (aml_table_count >= AmlTableCapacity) return false;
		aml_tables[aml_table_count++] = { aml, table->length - sizeof(*table) };
		return true;
	}

	void collect_x64_aml_tables() {
		aml_table_count = 0;
		const auto* fadt = find_x64_fadt();
		if (fadt) {
			uint64 dsdt_address = fadt->dsdt;
			if (fadt->header.length >= offsetof(uni::ACPI::FADT, x_dsdt) +
				sizeof(fadt->x_dsdt) && fadt->x_dsdt) dsdt_address = fadt->x_dsdt;
			if (dsdt_address && dsdt_address <= uint64(~stduint(0))) {
				append_x64_aml_table(
					reinterpret_cast<const uni::ACPI::DescriptionHeader*>(stduint(dsdt_address)),
					"DSDT");
			}
		}
		if (!acpi_rsdp_addr) return;
		const auto* rsdp = reinterpret_cast<const uni::ACPI::RSDP*>(acpi_rsdp_addr);
		if (!rsdp->isValid()) return;
		if (rsdp->revision >= 2 && rsdp->xsdt_address) {
			const auto* xsdt = reinterpret_cast<const uni::ACPI::XSDT*>(
				stduint(rsdp->xsdt_address));
			if (xsdt->header.length >= sizeof(uni::ACPI::DescriptionHeader) &&
				xsdt->header.isValid("XSDT")) {
				for (size_t index = 0; index < xsdt->Count(); ++index) {
					append_x64_aml_table(&(*xsdt)[index], "SSDT");
				}
				return;
			}
		}
		if (!rsdp->rsdt_address) return;
		const auto* rsdt = reinterpret_cast<const uni::ACPI::RSDT*>(
			stduint(rsdp->rsdt_address));
		if (rsdt->header.length < sizeof(uni::ACPI::DescriptionHeader) ||
			!rsdt->header.isValid("RSDT")) return;
		for (size_t index = 0; index < rsdt->Count(); ++index) {
			append_x64_aml_table(&(*rsdt)[index], "SSDT");
		}
	}

	bool decode_aml_package_length(const uint8* aml, stduint available,
		uint32& package_length, uint8& encoded_length) {
		if (!aml || !available) return false;
		const uint8 following = aml[0] >> 6;
		encoded_length = following + 1;
		if (encoded_length > available) return false;
		package_length = following ? aml[0] & 0x0Fu : aml[0] & 0x3Fu;
		for (uint8 index = 0; index < following; ++index) {
			package_length |= uint32(aml[index + 1]) << (4 + index * 8);
		}
		return package_length >= encoded_length;
	}

	bool decode_aml_integer(const uint8*& cursor, const uint8* end,
		uint64& value) {
		if (!cursor || cursor >= end) return false;
		switch (*cursor++) {
		case 0x00:
			value = 0;
			return true;
		case 0x01:
			value = 1;
			return true;
		case 0xFF:
			value = ~uint64(0);
			return true;
		case 0x0A:
			if (end - cursor < 1) return false;
			value = *cursor++;
			return true;
		case 0x0B:
			if (end - cursor < 2) return false;
			value = uint64(cursor[0]) | uint64(cursor[1]) << 8;
			cursor += 2;
			return true;
		case 0x0C:
			if (end - cursor < 4) return false;
			value = uint64(cursor[0]) | uint64(cursor[1]) << 8 |
				uint64(cursor[2]) << 16 | uint64(cursor[3]) << 24;
			cursor += 4;
			return true;
		case 0x0E:
			if (end - cursor < 8) return false;
			value = 0;
			for0(index, 8) value |= uint64(cursor[index]) << (index * 8);
			cursor += 8;
			return true;
		default:
			return false;
		}
	}

	bool decode_aml_name_string(const uint8*& cursor, const uint8* end,
		uint32& last_segment) {
		if (!cursor || cursor >= end) return false;
		last_segment = 0;
		if (*cursor == '\\') cursor++;
		while (cursor < end && *cursor == '^') cursor++;
		if (cursor >= end) return false;
		if (*cursor == 0x00) {
			cursor++;
			return true;
		}
		stduint segment_count = 1;
		if (*cursor == 0x2E) {
			cursor++;
			segment_count = 2;
		}
		else if (*cursor == 0x2F) {
			cursor++;
			if (cursor >= end) return false;
			segment_count = *cursor++;
		}
		if (segment_count > stduint(end - cursor) / 4) return false;
		for0(segment, segment_count) {
			last_segment = uint32(cursor[0]) | uint32(cursor[1]) << 8 |
				uint32(cursor[2]) << 16 | uint32(cursor[3]) << 24;
			cursor += 4;
		}
		return true;
	}

	bool find_enclosing_aml_device(const uint8* aml, stduint aml_length,
		stduint object_offset, stduint& content_begin, stduint& content_end,
		uint32& device_name) {
		bool found = false;
		stduint selected_length = ~stduint(0);
		for (stduint offset = 0; offset + 3 < aml_length; ++offset) {
			if (aml[offset] != 0x5B || aml[offset + 1] != 0x82) continue;
			uint32 device_length = 0;
			uint8 encoded_length = 0;
			if (!decode_aml_package_length(aml + offset + 2,
				aml_length - offset - 2, device_length, encoded_length) ||
				device_length > aml_length - offset - 2) continue;
			const stduint device_end = offset + 2 + device_length;
			const uint8* cursor = aml + offset + 2 + encoded_length;
			uint32 candidate_name = 0;
			if (!decode_aml_name_string(cursor, aml + device_end, candidate_name)) continue;
			const stduint candidate_begin = stduint(cursor - aml);
			if (object_offset < candidate_begin || object_offset >= device_end ||
				device_end - candidate_begin >= selected_length) continue;
			content_begin = candidate_begin;
			content_end = device_end;
			device_name = candidate_name;
			selected_length = device_end - candidate_begin;
			found = true;
		}
		return found;
	}

	bool find_aml_device_integer(const uint8* aml, stduint aml_length,
		stduint content_begin, stduint content_end, uint32 name, uint64& value) {
		for (stduint offset = content_begin; offset + 5 < content_end; ++offset) {
			if (aml[offset] != 0x08) continue;
			const uint8* cursor = aml + offset + 1;
			uint32 candidate_name = 0;
			if (!decode_aml_name_string(cursor, aml + content_end, candidate_name) ||
				candidate_name != name) continue;
			stduint owner_begin = 0;
			stduint owner_end = 0;
			uint32 owner_name = 0;
			if (!find_enclosing_aml_device(aml, aml_length, offset,
				owner_begin, owner_end, owner_name) || owner_begin != content_begin ||
				owner_end != content_end) continue;
			(void)owner_name;
			return decode_aml_integer(cursor, aml + content_end, value);
		}
		for (stduint name_offset = content_begin;
			name_offset + 5 < content_end; ++name_offset) {
			const uint32 candidate_name = uint32(aml[name_offset]) |
				uint32(aml[name_offset + 1]) << 8 |
				uint32(aml[name_offset + 2]) << 16 |
				uint32(aml[name_offset + 3]) << 24;
			if (candidate_name != name) continue;
			for (uint8 distance = 1; distance <= 4 &&
				name_offset >= content_begin + stduint(distance) + 1; ++distance) {
				const stduint package_offset = name_offset - distance;
				if (aml[package_offset - 1] != 0x14) continue;
				uint32 method_length = 0;
				uint8 encoded_length = 0;
				if (!decode_aml_package_length(aml + package_offset,
					content_end - package_offset, method_length, encoded_length) ||
					encoded_length != distance || method_length > content_end - package_offset)
					continue;
				const stduint method_end = package_offset + method_length;
				for (stduint offset = name_offset + 5; offset + 2 <= method_end; ++offset) {
					if (aml[offset] != 0xA4) continue;
					const uint8* result = aml + offset + 1;
					if (decode_aml_integer(result, aml + method_end, value)) return true;
				}
			}
		}
		return false;
	}

	bool find_enclosing_aml_scope(const uint8* aml, stduint aml_length,
		stduint object_offset, uint32& scope_name) {
		bool found = false;
		stduint selected_length = ~stduint(0);
		for (stduint offset = 0; offset + 2 < aml_length; ++offset) {
			if (aml[offset] != 0x10) continue;
			uint32 scope_length = 0;
			uint8 encoded_length = 0;
			if (!decode_aml_package_length(aml + offset + 1,
				aml_length - offset - 1, scope_length, encoded_length) ||
				scope_length > aml_length - offset - 1) continue;
			const stduint scope_end = offset + 1 + scope_length;
			const uint8* cursor = aml + offset + 1 + encoded_length;
			uint32 candidate_name = 0;
			if (!decode_aml_name_string(cursor, aml + scope_end, candidate_name)) continue;
			const stduint scope_begin = stduint(cursor - aml);
			if (object_offset < scope_begin || object_offset >= scope_end ||
				scope_end - scope_begin >= selected_length) continue;
			scope_name = candidate_name;
			selected_length = scope_end - scope_begin;
			found = true;
		}
		return found;
	}

	void find_named_aml_device_route_scope(uint32 name, uint16& segment,
		uint8& root_bus, bool& segment_found, bool& bus_found) {
		const uint32 seg_name = uint32('_') | uint32('S') << 8 |
			uint32('E') << 16 | uint32('G') << 24;
		const uint32 bbn_name = uint32('_') | uint32('B') << 8 |
			uint32('B') << 16 | uint32('N') << 24;
		for0(table_index, aml_table_count) {
			const auto& table = aml_tables[table_index];
			for (stduint offset = 0; offset + 3 < table.length; ++offset) {
				if (table.aml[offset] != 0x5B || table.aml[offset + 1] != 0x82) continue;
				uint32 device_length = 0;
				uint8 encoded_length = 0;
				if (!decode_aml_package_length(table.aml + offset + 2,
					table.length - offset - 2, device_length, encoded_length) ||
					device_length > table.length - offset - 2) continue;
				const stduint content_end = offset + 2 + device_length;
				const uint8* cursor = table.aml + offset + 2 + encoded_length;
				uint32 device_name = 0;
				if (!decode_aml_name_string(cursor, table.aml + content_end,
					device_name) || device_name != name) continue;
				const stduint content_begin = stduint(cursor - table.aml);
				uint64 value = 0;
				if (!segment_found && find_aml_device_integer(table.aml, table.length,
					content_begin, content_end, seg_name, value) && value <= 0xFFFFu) {
					segment = uint16(value);
					segment_found = true;
				}
				if (!bus_found && find_aml_device_integer(table.aml, table.length,
					content_begin, content_end, bbn_name, value) && value <= 0xFFu) {
					root_bus = uint8(value);
					bus_found = true;
				}
				if (segment_found && bus_found) return;
			}
		}
	}

	void find_aml_route_scope(const uint8* aml, stduint aml_length,
		stduint name_offset, uint16& segment, uint8& root_bus) {
		segment = 0;
		root_bus = 0;
		stduint content_begin = 0;
		stduint content_end = 0;
		uint32 device_name = 0;
		const uint32 seg_name = uint32('_') | uint32('S') << 8 |
			uint32('E') << 16 | uint32('G') << 24;
		const uint32 bbn_name = uint32('_') | uint32('B') << 8 |
			uint32('B') << 16 | uint32('N') << 24;
		uint64 value = 0;
		bool segment_found = false;
		bool bus_found = false;
		if (find_enclosing_aml_device(aml, aml_length, name_offset,
			content_begin, content_end, device_name)) {
			if (find_aml_device_integer(aml, aml_length, content_begin,
				content_end, seg_name, value) && value <= 0xFFFFu) {
				segment = uint16(value);
				segment_found = true;
			}
			if (find_aml_device_integer(aml, aml_length, content_begin,
				content_end, bbn_name, value) && value <= 0xFFu) {
				root_bus = uint8(value);
				bus_found = true;
			}
		}
		uint32 scope_name = 0;
		if ((!segment_found || !bus_found) &&
			find_enclosing_aml_scope(aml, aml_length, name_offset, scope_name)) {
			find_named_aml_device_route_scope(scope_name, segment, root_bus,
				segment_found, bus_found);
		}
		(void)device_name;
	}

	bool evaluate_aml_pic_method(const uint8* aml, stduint aml_length,
		stduint name_offset) {
		if (!aml || name_offset < 2 || name_offset + 5 > aml_length) return false;
		for (uint8 distance = 1; distance <= 4 &&
			name_offset >= stduint(distance) + 1; ++distance) {
			const stduint package_offset = name_offset - distance;
			if (aml[package_offset - 1] != 0x14) continue;
			uint32 method_length = 0;
			uint8 encoded_length = 0;
			if (!decode_aml_package_length(aml + package_offset,
				aml_length - package_offset, method_length, encoded_length) ||
				encoded_length != distance || method_length > aml_length - package_offset)
				continue;
			const stduint method_end = package_offset + method_length;
			if ((aml[name_offset + 4] & 7u) != 1u) return false;
			for (stduint offset = name_offset + 5; offset + 6 <= method_end; ++offset) {
				if (aml[offset] != 0x70 || aml[offset + 1] != 0x68) continue;
				const uint8* target = aml + offset + 2;
				uint32 target_name = 0;
				if (!decode_aml_name_string(target, aml + method_end, target_name) ||
					!target_name) continue;
				plogtrac("[ACPI] _PIC(1) evaluated target=%c%c%c%c",
					char(target_name), char(target_name >> 8),
					char(target_name >> 16), char(target_name >> 24));
				acpi_pic_apic_mode = true;
				acpi_pic_target_name = target_name;
				return true;
			}
		}
		return false;
	}

	bool append_acpi_pci_route(const AcpiPciRoutingEntry& route) {
		for0(index, acpi_pci_route_count) {
			auto& existing = acpi_pci_routes[index];
			if (existing.segment != route.segment ||
				existing.root_bus != route.root_bus ||
				existing.address != route.address || existing.pin != route.pin) continue;
			if (existing.source_name == route.source_name &&
				existing.source_index == route.source_index) return true;
			if (route.resolved && (!existing.resolved || acpi_pic_apic_mode)) {
				existing = route;
				return true;
			}
			if (existing.resolved) return true;
		}
		if (acpi_pci_route_count >= AcpiPciRoutingCapacity) return false;
		acpi_pci_routes[acpi_pci_route_count++] = route;
		return true;
	}

	void collect_aml_prt_package(const uint8* package_opcode,
		const uint8* method_end, uint16 segment, uint8 root_bus) {
		if (!package_opcode || package_opcode >= method_end ||
			*package_opcode != 0x12) return;
		uint32 package_length = 0;
		uint8 encoded_length = 0;
		if (!decode_aml_package_length(package_opcode + 1,
			method_end - package_opcode - 1, package_length, encoded_length) ||
			package_length > stduint(method_end - package_opcode - 1)) return;
		const uint8* package_end = package_opcode + 1 + package_length;
		const uint8* cursor = package_opcode + 1 + encoded_length;
		if (cursor >= package_end) return;
		const uint8 element_count = *cursor++;
		for0(element, element_count) {
			if (cursor >= package_end || *cursor++ != 0x12) {
				break;
			}
			uint32 entry_length = 0;
			uint8 entry_encoded_length = 0;
			if (!decode_aml_package_length(cursor, package_end - cursor,
				entry_length, entry_encoded_length) ||
				entry_length > stduint(package_end - cursor)) {
				break;
			}
			const uint8* entry_end = cursor + entry_length;
			const uint8* entry = cursor + entry_encoded_length;
			cursor = entry_end;
			if (entry >= entry_end || *entry++ != 4) {
				continue;
			}
			uint64 address = 0;
			uint64 pin = 0;
			uint64 source_index = 0;
			uint32 source_name = 0;
			if (!decode_aml_integer(entry, entry_end, address) ||
				!decode_aml_integer(entry, entry_end, pin)) {
				continue;
			}
			if (!decode_aml_name_string(entry, entry_end, source_name) ||
				!decode_aml_integer(entry, entry_end, source_index)) {
				continue;
			}
			const bool direct = source_name == 0;
			if (address > 0xFFFFFFFFu || pin >= 4 || source_index > 0xFFFFFFFFu) {
				continue;
			}
			const uint16 irq_flags = DeviceResourceFlag_IrqLevel |
				DeviceResourceFlag_IrqActiveLow | DeviceResourceFlag_IrqShareable;
			append_acpi_pci_route({ uint32(address),
				direct ? uint32(source_index) : 0, source_name, uint32(source_index),
				segment, irq_flags, root_bus, uint8(pin), direct });
		}
	}

	void collect_aml_route_method(const uint8* aml, stduint aml_length,
		stduint name_offset, uint16 segment, uint8 root_bus) {
		if (!aml || name_offset < 2 || name_offset + 5 > aml_length) return;
		stduint package_offset = 0;
		uint32 method_length = 0;
		uint8 encoded_length = 0;
		for (uint8 distance = 1; distance <= 4 &&
			name_offset >= stduint(distance) + 1;
			++distance) {
			const stduint candidate = name_offset - distance;
			if (aml[candidate - 1] != 0x14) continue;
			uint32 length = 0;
			uint8 encoded = 0;
			if (!decode_aml_package_length(aml + candidate,
				aml_length - candidate, length, encoded) || encoded != distance) continue;
			package_offset = candidate;
			method_length = length;
			encoded_length = encoded;
			break;
		}
		if (!package_offset || method_length < uint32(encoded_length) + 5u ||
			method_length > aml_length - package_offset) return;
		const stduint method_end = package_offset + method_length;
		const stduint body_offset = name_offset + 5;
		for (stduint offset = body_offset; offset + 3 <= method_end; ++offset) {
			if (aml[offset] != 0xA4 || aml[offset + 1] != 0x12) continue;
			uint32 route_length = 0;
			uint8 route_encoded_length = 0;
			if (!decode_aml_package_length(aml + offset + 2,
				method_end - offset - 2, route_length, route_encoded_length)) continue;
			if (offset + 2 + route_encoded_length >= method_end ||
				route_length > method_end - offset - 2) continue;
			collect_aml_prt_package(aml + offset + 1, aml + method_end,
				segment, root_bus);
		}
	}

	bool decode_aml_buffer(const uint8* buffer_opcode, const uint8* end,
		const uint8*& data, stduint& data_length) {
		if (!buffer_opcode || buffer_opcode >= end || *buffer_opcode != 0x11)
			return false;
		uint32 package_length = 0;
		uint8 encoded_length = 0;
		if (!decode_aml_package_length(buffer_opcode + 1,
			end - buffer_opcode - 1, package_length, encoded_length) ||
			package_length > stduint(end - buffer_opcode - 1)) return false;
		const uint8* buffer_end = buffer_opcode + 1 + package_length;
		const uint8* cursor = buffer_opcode + 1 + encoded_length;
		uint64 declared_length = 0;
		if (!decode_aml_integer(cursor, buffer_end, declared_length) ||
			declared_length > stduint(buffer_end - cursor)) return false;
		data = cursor;
		data_length = stduint(declared_length);
		return true;
	}

	bool decode_acpi_irq_resource(const uint8* data, stduint length,
		uint32 source_index, AcpiIrqResource& resource) {
		const uint8* cursor = data;
		const uint8* end = data + length;
		uint32 descriptor_index = 0;
		while (cursor < end) {
			const uint8 header = *cursor++;
			if (header & 0x80u) {
				if (end - cursor < 2) return false;
				const uint16 descriptor_length = uint16(cursor[0]) |
					uint16(cursor[1]) << 8;
				cursor += 2;
				if (descriptor_length > stduint(end - cursor)) return false;
				if ((header & 0x7Fu) == 0x09u && descriptor_index == source_index) {
					if (descriptor_length < 6) return false;
					const uint8 irq_flags = cursor[0];
					const uint8 interrupt_count = cursor[1];
					if (interrupt_count != 1 ||
						descriptor_length < stduint(2u + interrupt_count * 4u)) return false;
					resource.gsi = uint32(cursor[2]) | uint32(cursor[3]) << 8 |
						uint32(cursor[4]) << 16 | uint32(cursor[5]) << 24;
					resource.flags = DeviceResourceFlag_None;
					if (!(irq_flags & (1u << 1)))
						resource.flags |= DeviceResourceFlag_IrqLevel;
					if (irq_flags & (1u << 2))
						resource.flags |= DeviceResourceFlag_IrqActiveLow;
					if (irq_flags & (1u << 3))
						resource.flags |= DeviceResourceFlag_IrqShareable;
					return true;
				}
				cursor += descriptor_length;
				descriptor_index++;
				continue;
			}
			const uint8 item_name = (header >> 3) & 0x0Fu;
			const uint8 descriptor_length = header & 7u;
			if (descriptor_length > stduint(end - cursor)) return false;
			if (item_name == 0x0Fu) return false;
			if (item_name == 0x04u && descriptor_index == source_index) {
				if (descriptor_length < 2) return false;
				const uint16 mask = uint16(cursor[0]) | uint16(cursor[1]) << 8;
				if (!mask || (mask & uint16(mask - 1u))) return false;
				const uint8 irq_flags = descriptor_length >= 3 ? cursor[2] : 1u;
				for0(irq, 16) {
					if (mask & uint16(1u << irq)) {
						resource.gsi = uint32(irq);
						resource.flags = DeviceResourceFlag_None;
						if (!(irq_flags & 1u))
							resource.flags |= DeviceResourceFlag_IrqLevel;
						if (irq_flags & (1u << 3))
							resource.flags |= DeviceResourceFlag_IrqActiveLow;
						if (irq_flags & (1u << 4))
							resource.flags |= DeviceResourceFlag_IrqShareable;
						return true;
					}
				}
				return false;
			}
			cursor += descriptor_length;
			descriptor_index++;
		}
		return false;
	}

	void translate_isa_irq_to_gsi(AcpiIrqResource& resource) {
		if (resource.gsi >= 16) return;
		for0(index, interrupt_source_override_count) {
			const auto& override_entry = interrupt_source_overrides[index];
			if (override_entry.source != resource.gsi) continue;
			resource.gsi = override_entry.gsi;
			return;
		}
		const auto* madt = acpi_madt_addr ?
			reinterpret_cast<const uni::ACPI::MADT*>(acpi_madt_addr) : nullptr;
		if (!madt || madt->header.length < sizeof(*madt) ||
			!madt->header.isValid("APIC")) return;
		const stduint end = acpi_madt_addr + madt->header.length;
		for (stduint address = acpi_madt_addr + sizeof(*madt);
			address + sizeof(uni::ACPI::MADTEntry) <= end;) {
			const auto* entry = reinterpret_cast<const uni::ACPI::MADTEntry*>(address);
			if (entry->length < sizeof(*entry) || entry->length > end - address) return;
			if (entry->type == uni::ACPI::MADTTypeInterruptSourceOverride &&
				entry->length >= sizeof(uni::ACPI::MADTInterruptSourceOverride)) {
				const auto* override_entry =
					reinterpret_cast<const uni::ACPI::MADTInterruptSourceOverride*>(entry);
				if (override_entry->bus == 0 && override_entry->source == resource.gsi) {
					resource.gsi = override_entry->global_system_interrupt;
					return;
				}
			}
			address += entry->length;
		}
	}

	bool find_named_aml_buffer(const uint8* aml, stduint scope_begin,
		stduint scope_end, uint32 name, const uint8*& data,
		stduint& data_length) {
		for (stduint offset = scope_begin; offset + 5 < scope_end; ++offset) {
			if (aml[offset] != 0x08) continue;
			const uint8* cursor = aml + offset + 1;
			uint32 candidate_name = 0;
			if (!decode_aml_name_string(cursor, aml + scope_end, candidate_name) ||
				candidate_name != name) continue;
			if (decode_aml_buffer(cursor, aml + scope_end, data, data_length))
				return true;
		}
		return false;
	}

	bool find_static_aml_crs_buffer(const uint8* aml, stduint scope_begin,
		stduint scope_end, stduint name_offset, const uint8*& data,
		stduint& data_length) {
		const uint8* end = aml + scope_end;
		if (name_offset < scope_begin || name_offset + 4 >= scope_end) return false;
		if (name_offset && aml[name_offset - 1] == 0x08 &&
			decode_aml_buffer(aml + name_offset + 4, end, data, data_length)) {
			return true;
		}
		for (uint8 distance = 1; distance <= 4 &&
			name_offset >= scope_begin + stduint(distance) + 1; ++distance) {
			const stduint candidate = name_offset - distance;
			if (aml[candidate - 1] != 0x14) continue;
			uint32 method_length = 0;
			uint8 encoded_length = 0;
			if (!decode_aml_package_length(aml + candidate,
				scope_end - candidate, method_length, encoded_length) ||
				encoded_length != distance || method_length > scope_end - candidate)
				continue;
			const stduint method_end = candidate + method_length;
			for (stduint offset = name_offset + 5; offset + 2 < method_end; ++offset) {
				if (aml[offset] != 0xA4) continue;
				if (aml[offset + 1] == 0x11 &&
					decode_aml_buffer(aml + offset + 1, aml + method_end,
						data, data_length)) return true;
				const uint8* return_value = aml + offset + 1;
				uint32 return_name = 0;
				if (decode_aml_name_string(return_value, aml + method_end,
					return_name) && return_name &&
					find_named_aml_buffer(aml, scope_begin, scope_end,
						return_name, data, data_length)) return true;
			}
		}
		return false;
	}

	bool find_dynamic_aml_crs_field(const uint8* aml, stduint scope_begin,
		stduint scope_end, stduint name_offset, uint32& field_name) {
		if (!aml || name_offset < scope_begin || name_offset + 4 >= scope_end)
			return false;
		for (uint8 distance = 1; distance <= 4 &&
			name_offset >= scope_begin + stduint(distance) + 1; ++distance) {
			const stduint candidate = name_offset - distance;
			if (aml[candidate - 1] != 0x14) continue;
			uint32 method_length = 0;
			uint8 encoded_length = 0;
			if (!decode_aml_package_length(aml + candidate,
				scope_end - candidate, method_length, encoded_length) ||
				encoded_length != distance || method_length > scope_end - candidate)
				continue;
			const stduint method_end = candidate + method_length;
			for (stduint offset = name_offset + 5; offset + 9 <= method_end; ++offset) {
				if (aml[offset] != 0xA4) continue;
				const uint8* expression = aml + offset + 1;
				uint32 method_name = 0;
				uint32 candidate_field = 0;
				if (!decode_aml_name_string(expression, aml + method_end, method_name) ||
					!method_name || !decode_aml_name_string(expression, aml + method_end,
						candidate_field) || !candidate_field) continue;
				field_name = candidate_field;
				return true;
			}
		}
		return false;
	}

	DeviceNode* find_piix_isa_bridge() {
		auto* bridge = Devsman::FindPCIDeviceByVendorDevice(0x8086u, 0x7000u);
		if (bridge) return bridge;
		return Devsman::FindPCIDeviceByVendorDevice(0x8086u, 0x7110u);
	}

	bool resolve_dynamic_aml_link(const uint8* aml, stduint aml_length,
		uint32 source_name, uint32 source_index, AcpiIrqResource& resource) {
		if (source_index) return false;
		for (stduint offset = 0; offset + 4 < aml_length; ++offset) {
			if (aml[offset] != 0x5B || aml[offset + 1] != 0x82) continue;
			uint32 device_length = 0;
			uint8 encoded_length = 0;
			if (!decode_aml_package_length(aml + offset + 2,
				aml_length - offset - 2, device_length, encoded_length) ||
				device_length > aml_length - offset - 2) continue;
			const stduint device_end = offset + 2 + device_length;
			const uint8* name_cursor = aml + offset + 2 + encoded_length;
			uint32 device_name = 0;
			if (!decode_aml_name_string(name_cursor, aml + device_end, device_name) ||
				device_name != source_name) continue;
			for (stduint name_offset = stduint(name_cursor - aml);
				name_offset + 4 < device_end; ++name_offset) {
				if (aml[name_offset] != '_' || aml[name_offset + 1] != 'C' ||
					aml[name_offset + 2] != 'R' || aml[name_offset + 3] != 'S') continue;
				uint32 field_name = 0;
				if (!find_dynamic_aml_crs_field(aml,
					stduint(name_cursor - aml), device_end, name_offset, field_name)) continue;
				const uint32 field_prefix = uint32('P') | uint32('R') << 8 |
					uint32('Q') << 16;
				if ((field_name & 0x00FFFFFFu) != field_prefix ||
					uint8(field_name >> 24) < '0' || uint8(field_name >> 24) > '3')
					continue;
				auto* bridge = find_piix_isa_bridge();
				if (!bridge) return false;
				const uint8 pirq_index = uint8(uint8(field_name >> 24) - uint8('0'));
				const uint32 pirq_routes = uni::PCI::read_config_register(
					bridge->fields.pci_bus, bridge->fields.pci_device,
					bridge->fields.pci_function, 0x60);
				const uint8 irq = uint8(pirq_routes >> (pirq_index * 8));
				if ((irq & 0x80u) || irq >= 16) return false;
				resource.gsi = irq;
				resource.flags = DeviceResourceFlag_IrqLevel |
					DeviceResourceFlag_IrqActiveLow | DeviceResourceFlag_IrqShareable;
				const uint32 resource_name = uint32('P') | uint32('R') << 8 |
					uint32('R') << 16 | uint32('0') << 24;
				const uint8* resource_data = nullptr;
				stduint resource_length = 0;
				AcpiIrqResource template_resource = {};
				if (find_named_aml_buffer(aml, 0, aml_length, resource_name,
					resource_data, resource_length) &&
					decode_acpi_irq_resource(resource_data, resource_length, 0,
						template_resource)) resource.flags = template_resource.flags;
				translate_isa_irq_to_gsi(resource);
				return true;
			}
		}
		return false;
	}

	bool resolve_static_aml_link(const uint8* aml, stduint aml_length,
		uint32 source_name, uint32 source_index, AcpiIrqResource& resource) {
		bool found = false;
		for (stduint offset = 0; offset + 4 < aml_length; ++offset) {
			if (aml[offset] != 0x5B || aml[offset + 1] != 0x82) continue;
			uint32 device_length = 0;
			uint8 encoded_length = 0;
			if (!decode_aml_package_length(aml + offset + 2,
				aml_length - offset - 2, device_length, encoded_length) ||
				device_length > aml_length - offset - 2) continue;
			const stduint device_end = offset + 2 + device_length;
			const uint8* name_cursor = aml + offset + 2 + encoded_length;
			uint32 device_name = 0;
			if (!decode_aml_name_string(name_cursor, aml + device_end, device_name) ||
				device_name != source_name) continue;
			for (stduint name_offset = stduint(name_cursor - aml);
				name_offset + 4 < device_end; ++name_offset) {
				if (aml[name_offset] != '_' || aml[name_offset + 1] != 'C' ||
					aml[name_offset + 2] != 'R' || aml[name_offset + 3] != 'S') continue;
				const uint8* resource_data = nullptr;
				stduint resource_length = 0;
				if (!find_static_aml_crs_buffer(aml,
					stduint(name_cursor - aml), device_end, name_offset,
					resource_data, resource_length)) continue;
				AcpiIrqResource candidate = {};
				if (!decode_acpi_irq_resource(resource_data, resource_length,
					source_index, candidate)) continue;
				translate_isa_irq_to_gsi(candidate);
				if (found && (resource.gsi != candidate.gsi ||
					resource.flags != candidate.flags)) return false;
				resource = candidate;
				found = true;
			}
		}
		return found;
	}

	void resolve_static_aml_links(const uint8* aml, stduint aml_length) {
		for0(index, acpi_pci_route_count) {
			auto& route = acpi_pci_routes[index];
			if (route.resolved || !route.source_name) continue;
			AcpiIrqResource resource = {};
			if (!resolve_static_aml_link(aml, aml_length, route.source_name,
				route.source_index, resource)) continue;
			for (stduint update = index; update < acpi_pci_route_count; ++update) {
				auto& candidate = acpi_pci_routes[update];
				if (candidate.segment != route.segment ||
					candidate.root_bus != route.root_bus ||
					candidate.source_name != route.source_name ||
					candidate.source_index != route.source_index) continue;
				candidate.gsi = resource.gsi;
				candidate.irq_flags = resource.flags;
				candidate.resolved = true;
			}
			plogtrac("[ACPI] PCI link %c%c%c%c[%u] -> GSI%u",
				char(route.source_name), char(route.source_name >> 8),
				char(route.source_name >> 16), char(route.source_name >> 24),
				(unsigned)route.source_index, (unsigned)resource.gsi);
		}
	}

	void resolve_dynamic_aml_links(const uint8* aml, stduint aml_length) {
		for0(index, acpi_pci_route_count) {
			auto& route = acpi_pci_routes[index];
			if (route.resolved || !route.source_name) continue;
			AcpiIrqResource resource = {};
			if (!resolve_dynamic_aml_link(aml, aml_length, route.source_name,
				route.source_index, resource)) continue;
			for (stduint update = index; update < acpi_pci_route_count; ++update) {
				auto& candidate = acpi_pci_routes[update];
				if (candidate.segment != route.segment ||
					candidate.root_bus != route.root_bus ||
					candidate.source_name != route.source_name ||
					candidate.source_index != route.source_index) continue;
				candidate.gsi = resource.gsi;
				candidate.irq_flags = resource.flags;
				candidate.resolved = true;
			}
			plogtrac("[ACPI] PCI dynamic link %c%c%c%c[%u] -> GSI%u",
				char(route.source_name), char(route.source_name >> 8),
				char(route.source_name >> 16), char(route.source_name >> 24),
				(unsigned)route.source_index, (unsigned)resource.gsi);
		}
	}

	void collect_named_aml_route_object(uint32 name, uint16 segment,
		uint8 root_bus) {
		for0(table_index, aml_table_count) {
			const auto& table = aml_tables[table_index];
			for (stduint offset = 0; offset + 4 <= table.length; ++offset) {
				const uint32 candidate = uint32(table.aml[offset]) |
					uint32(table.aml[offset + 1]) << 8 |
					uint32(table.aml[offset + 2]) << 16 |
					uint32(table.aml[offset + 3]) << 24;
				if (candidate != name) continue;
				collect_aml_route_method(table.aml, table.length, offset,
					segment, root_bus);
				if (!offset || table.aml[offset - 1] != 0x08) continue;
				const uint8* value = table.aml + offset + 4;
				if (value < table.aml + table.length && *value == 0x12) {
					collect_aml_prt_package(value, table.aml + table.length,
						segment, root_bus);
				}
			}
		}
	}

	void collect_aml_return_names_in_range(const uint8* begin, const uint8* end,
		uint16 segment, uint8 root_bus) {
		for (const uint8* cursor = begin; cursor + 2 < end; ++cursor) {
			if (*cursor != 0xA4 || cursor[1] == 0x12) continue;
			const uint8* value = cursor + 1;
			uint32 return_name = 0;
			if (!decode_aml_name_string(value, end, return_name) || !return_name)
				continue;
			collect_named_aml_route_object(return_name, segment, root_bus);
		}
	}

	void collect_aml_route_return_names(const uint8* aml, stduint aml_length,
		stduint name_offset, uint16 segment, uint8 root_bus) {
		if (!aml || name_offset < 2 || name_offset + 5 > aml_length) return;
		for (uint8 distance = 1; distance <= 4 &&
			name_offset >= stduint(distance) + 1; ++distance) {
			const stduint package_offset = name_offset - distance;
			if (aml[package_offset - 1] != 0x14) continue;
			uint32 method_length = 0;
			uint8 encoded_length = 0;
			if (!decode_aml_package_length(aml + package_offset,
				aml_length - package_offset, method_length, encoded_length) ||
				encoded_length != distance || method_length > aml_length - package_offset)
				continue;
			const stduint method_end = package_offset + method_length;
			const uint8* body = aml + name_offset + 5;
			const uint8* end = aml + method_end;
			if (acpi_pic_apic_mode && acpi_pic_target_name) {
				for (const uint8* cursor = body; cursor + 3 < end; ++cursor) {
					if (*cursor != 0xA0) continue;
					uint32 if_length = 0;
					uint8 if_encoded_length = 0;
					if (!decode_aml_package_length(cursor + 1, end - cursor - 1,
						if_length, if_encoded_length) || if_length > stduint(end - cursor - 1))
						continue;
					const uint8* if_end = cursor + 1 + if_length;
					const uint8* predicate = cursor + 1 + if_encoded_length;
					if (predicate >= if_end || *predicate++ != 0x93) continue;
					uint32 predicate_name = 0;
					uint64 predicate_value = 0;
					if (!decode_aml_name_string(predicate, if_end, predicate_name) ||
						predicate_name != acpi_pic_target_name ||
						!decode_aml_integer(predicate, if_end, predicate_value) ||
						predicate_value != 0) continue;
					if (if_end >= end || *if_end != 0xA1) break;
					uint32 else_length = 0;
					uint8 else_encoded_length = 0;
					if (!decode_aml_package_length(if_end + 1, end - if_end - 1,
						else_length, else_encoded_length) ||
						else_length > stduint(end - if_end - 1)) break;
					const uint8* else_begin = if_end + 1 + else_encoded_length;
					const uint8* else_end = if_end + 1 + else_length;
					collect_aml_return_names_in_range(else_begin, else_end,
						segment, root_bus);
					return;
				}
			}
			collect_aml_return_names_in_range(body, end, segment, root_bus);
			return;
		}
	}

	void discover_x64_pci_routing_aml() {
		acpi_pic_apic_mode = false;
		acpi_pic_target_name = 0;
		collect_x64_aml_tables();
		if (!aml_table_count) {
			plogwarn("[ACPI] PCI routing: no DSDT or SSDT AML available");
			return;
		}
		bool pic_evaluated = false;
		for0(table_index, aml_table_count) {
			const auto& table = aml_tables[table_index];
			for (stduint offset = 0; offset + 4 <= table.length; ++offset) {
				if (table.aml[offset] == '_' && table.aml[offset + 1] == 'P' &&
					table.aml[offset + 2] == 'I' && table.aml[offset + 3] == 'C') {
					if (!pic_evaluated && evaluate_aml_pic_method(table.aml,
						table.length, offset)) pic_evaluated = true;
				}
			}
		}
		for0(table_index, aml_table_count) {
			const auto& table = aml_tables[table_index];
			for (stduint offset = 0; offset + 4 <= table.length; ++offset) {
				if (table.aml[offset] != '_' || table.aml[offset + 1] != 'P' ||
					table.aml[offset + 2] != 'R' || table.aml[offset + 3] != 'T') continue;
				uint16 segment = 0;
				uint8 root_bus = 0;
				find_aml_route_scope(table.aml, table.length, offset, segment, root_bus);
				if (offset && table.aml[offset - 1] == 0x08 &&
					offset + 4 < table.length && table.aml[offset + 4] == 0x12) {
					collect_aml_prt_package(table.aml + offset + 4,
						table.aml + table.length, segment, root_bus);
				}
				collect_aml_route_method(table.aml, table.length, offset,
					segment, root_bus);
				collect_aml_route_return_names(table.aml, table.length, offset,
					segment, root_bus);
			}
		}
		for0(table_index, aml_table_count) {
			const auto& table = aml_tables[table_index];
			resolve_static_aml_links(table.aml, table.length);
		}
		for0(table_index, aml_table_count) {
			const auto& table = aml_tables[table_index];
			resolve_dynamic_aml_links(table.aml, table.length);
		}
	}

	bool resolve_x64_pci_route(DeviceNode* node, uint8 interrupt_pin,
		uint8& routed_pin, uint8& root_device, uint8& root_function,
		uint32& gsi, uint16& irq_flags) {
		if (!node || interrupt_pin < 1 || interrupt_pin > 4 ||
			!acpi_pci_route_count) return false;
		uint8 pin = interrupt_pin - 1;
		DeviceNode* route_device = node;
		uint16 segment = node->fields.pci_segment;
		uint8 root_bus = node->fields.pci_bus;
		while (route_device) {
			auto* bus_node = reinterpret_cast<DeviceNode*>(
				route_device->link.getParent());
			if (!bus_node || DeviceNodeType(bus_node->fields.node_type) !=
				DeviceNodeType::PciBus) return false;
			auto* parent = reinterpret_cast<DeviceNode*>(bus_node->link.getParent());
			if (!parent || DeviceNodeType(parent->fields.node_type) ==
				DeviceNodeType::PCI_Root) {
				segment = bus_node->fields.pci_segment;
				root_bus = bus_node->fields.pci_bus;
				break;
			}
			if (DeviceNodeType(parent->fields.node_type) !=
				DeviceNodeType::PciDevice || parent->fields.class_base != 0x06u ||
				parent->fields.class_sub != 0x04u) return false;
			pin = uint8((pin + route_device->fields.pci_device) & 3u);
			route_device = parent;
		}
		if (!route_device) return false;
		root_device = route_device->fields.pci_device;
		root_function = route_device->fields.pci_function;
		routed_pin = pin;
		const AcpiPciRoutingEntry* wildcard = nullptr;
		for0(index, acpi_pci_route_count) {
			const auto& route = acpi_pci_routes[index];
			if (!route.resolved || route.segment != segment ||
				route.root_bus != root_bus || uint8(route.address >> 16) != root_device ||
				route.pin != pin)
				continue;
			const uint16 function = uint16(route.address);
			if (function == root_function) {
				gsi = route.gsi;
				irq_flags = route.irq_flags;
				return true;
			}
			if (function == 0xFFFFu) wildcard = &route;
		}
		if (!wildcard) return false;
		gsi = wildcard->gsi;
		irq_flags = wildcard->irq_flags;
		return true;
	}

	void apply_x64_pci_interrupt_routing() {
		acpi_pci_route_count = 0;
		discover_x64_pci_routing_aml();
		if (!acpi_pci_route_count || !pci_root) return;
		for (stduint bus = 0; bus < numsof(pci_bus_nodes); ++bus) {
			auto* bus_node = pci_bus_nodes[bus];
			if (!bus_node) continue;
			for (auto* device = reinterpret_cast<DeviceNode*>(bus_node->link.subf);
				device; device = reinterpret_cast<DeviceNode*>(device->link.next)) {
				if (DeviceNodeType(device->fields.node_type) !=
					DeviceNodeType::PciDevice) continue;
				const uint32 interrupt = uni::PCI::read_config_register(
					device->fields.pci_bus, device->fields.pci_device,
					device->fields.pci_function, 0x3C);
				const uint8 interrupt_pin = uint8(interrupt >> 8);
				uint8 routed_pin = 0;
				uint8 root_device = 0;
				uint8 root_function = 0;
				uint32 gsi = 0;
				uint16 irq_flags = DeviceResourceFlag_None;
				if (!resolve_x64_pci_route(device, interrupt_pin, routed_pin,
					root_device, root_function, gsi, irq_flags)) continue;
				DeviceResource* irq = nullptr;
				for0(resource_index, device->fields.resource_count) {
					auto& resource = device->fields.resources[resource_index];
					if (DeviceResourceType(resource.type) == DeviceResourceType::IrqLine) {
						irq = &resource;
						break;
					}
				}
				if (irq) {
					irq->flags = irq_flags;
					irq->index = 0;
					irq->start = gsi;
					irq->length = 1;
					irq->extra = interrupt_pin;
				}
				else if (!append_resource(device, DeviceResourceType::IrqLine,
					irq_flags, 0, gsi, 1, interrupt_pin)) continue;
				plogtrac("[ACPI] PCI INTx route pci=%02x:%02x.%u pin=%u root=%02x.%u/%u gsi=%u",
					(unsigned)device->fields.pci_bus,
					(unsigned)device->fields.pci_device,
					(unsigned)device->fields.pci_function,
					(unsigned)interrupt_pin, (unsigned)root_device,
					(unsigned)root_function, (unsigned)routed_pin, (unsigned)gsi);
			}
		}
	}

	void discover_x64_interrupt_topology() {
		ioapic_topology_count = 0;
		interrupt_source_override_count = 0;
		const auto* madt = acpi_madt_addr ?
			reinterpret_cast<const uni::ACPI::MADT*>(acpi_madt_addr) : nullptr;
		const bool valid_madt = madt && madt->header.length >= sizeof(*madt) &&
			madt->header.isValid("APIC");
		const stduint begin = valid_madt ? acpi_madt_addr + sizeof(*madt) : 0;
		const stduint end = valid_madt ? acpi_madt_addr + madt->header.length : 0;
		for (stduint address = begin; valid_madt && end >= begin &&
			address + sizeof(uni::ACPI::MADTEntry) <= end;) {
			const auto* entry = reinterpret_cast<const uni::ACPI::MADTEntry*>(address);
			if (entry->length < sizeof(*entry) || entry->length > end - address) break;
			if (entry->type == uni::ACPI::MADTTypeIOAPIC &&
				entry->length >= sizeof(uni::ACPI::MADTIOAPIC)) {
				const auto* ioapic = reinterpret_cast<const uni::ACPI::MADTIOAPIC*>(entry);
				if (!append_x64_ioapic(ioapic->ioapic_address,
					ioapic->global_system_interrupt_base, ioapic->ioapic_id)) {
					plogwarn("[ACPI] Ignoring invalid or overlapping I/O APIC id=%u MMIO=%[32H] GSI-base=%u",
						ioapic->ioapic_id, ioapic->ioapic_address,
						ioapic->global_system_interrupt_base);
				}
			}
			else if (entry->type == uni::ACPI::MADTTypeInterruptSourceOverride &&
				entry->length >= sizeof(uni::ACPI::MADTInterruptSourceOverride)) {
				const auto* override_entry = reinterpret_cast<const uni::ACPI::MADTInterruptSourceOverride*>(entry);
				if (override_entry->bus == 0 &&
					interrupt_source_override_count < InterruptSourceOverrideCapacity) {
					interrupt_source_overrides[interrupt_source_override_count++] = {
						override_entry->global_system_interrupt, override_entry->flags,
						override_entry->source };
					plogtrac("[ACPI] ISA IRQ%u -> GSI%u flags=%[16H]",
						override_entry->source, override_entry->global_system_interrupt,
						override_entry->flags);
				}
				else if (override_entry->bus == 0)
					plogwarn("[ACPI] IRQ override capacity exceeded");
			}
			address += entry->length;
		}
		if (!ioapic_topology_count) {
			const uint32 id_register = read_x64_ioapic_register(0xFEC00000u, 0);
			const uint8 id = uint8(id_register >> 24);
			if (!append_x64_ioapic(0xFEC00000u, 0, id)) {
				ioapic_topology[ioapic_topology_count++] = {
					0xFEC00000u, 0, 24, id };
				plogwarn("[ACPI] Using legacy 24-pin I/O APIC fallback");
			}
		}
	}
	#endif

	void program_isa_irq_routes() {
		uint16_t enabled_irq_mask = 0xFFFF;
		bool mirror_irq0_to_pin2 = true;
		#if _MCCA == 0x8664
		const uint8 bsp_lapic_id = acpi_cpu_lapic_ids[0] < 0x100u ?
			uint8(acpi_cpu_lapic_ids[0]) : 0;
		// x64 UEFI can open the ISA IRQ set, but mirroring IRQ0 onto pin2
		// currently triggers the legacy cascade path bug.
		mirror_irq0_to_pin2 = false;
		#endif
		#if _MCCA == 0x8664
		for0(index, ioapic_topology_count) {
			const auto& ioapic = ioapic_topology[index];
			for0(pin, ioapic.gsi_count) {
				x64_ioapic_write_route(ioapic.gsi_base + uint32(pin), 0x10000);
			}
		}
		#else
		// Reset all 24 IOAPIC pins to masked state first.
		for (int i = 0; i < 24; i++) {
			IC.IO_Writ64(0x10 + i * 2, 0x10000); // Bit 16: Masked
		}
		#endif
		// Then remap ISA IRQs (0-15) to the kernel vectors and clear the mask bit.
		for (int i = 0; i < 16; i++) {
			if ((enabled_irq_mask & (1u << i)) == 0) continue;
			const uint8_t vector = (i < 8) ? (IRQ_PIT + i) : (IRQ_RTC + (i - 8));
			#if _MCCA == 0x8664
			uint64_t rte = uint64_t(vector) | uint64_t(bsp_lapic_id) << 56;
			// Fixed, Physical, Edge, Active-High, Unmasked
			uint32 gsi = uint32(i);
			if (i != 0) {
				for (stduint j = 0; j < interrupt_source_override_count; ++j) {
					const auto& override_entry = interrupt_source_overrides[j];
					if (override_entry.source != i) continue;
					const uint16 polarity = override_entry.flags & 3u;
					const uint16 trigger = (override_entry.flags >> 2) & 3u;
					if ((override_entry.flags & ~uint16(0xFu)) || polarity == 2 || trigger == 2) {
						plogwarn("[ACPI] ISA IRQ%u has invalid override flags=%[16H]; keeping legacy route",
							(unsigned)i, override_entry.flags);
						break;
					}
					gsi = override_entry.gsi;
					if (polarity == 3) rte |= uint64_t(1) << 13;
					if (trigger == 3) rte |= (uint64_t(1) << 15) | (uint64_t(1) << 16);
					break;
				}
			}
			#else
			const uint64_t rte = (uint64_t)vector; // Fixed, Physical, Edge, Active-High, Unmasked
			#endif
			if (i == 0) {
				// Route PIT to pin 0. Some platforms also mirror it to pin 2 for legacy
				// compatibility, but x64 UEFI currently keeps pin 2 masked while we
				// localize the transition-stack/legacy-IRQ entry issue.
				#if _MCCA == 0x8664
				x64_ioapic_write_route(0, rte);
				#else
				IC.IO_Writ64(0x10 + 0 * 2, rte);
				#endif
				if (mirror_irq0_to_pin2) {
					IC.IO_Writ64(0x10 + 2 * 2, rte);
				}
			}
			else if (i != 2) {
				#if _MCCA == 0x8664
				if (!x64_ioapic_write_route(gsi, rte)) {
					plogwarn("[ACPI] ISA IRQ%u GSI%u is outside discovered I/O APIC ranges",
						(unsigned)i, (unsigned)gsi);
				}
				#else
				IC.IO_Writ64(0x10 + i * 2, rte);
				#endif
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
	IC[8].setModeRupt(mglb(Handint_Double_Fault_Emergency_Entry), SegCo64);
	IC[8].interrupt_stack_table = 1; // TSS.IST1: independent of the interrupted stack
	
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
		discover_x64_interrupt_topology();
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
	Iova::EnableVtdPassthrough(PCI_Root());
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
	static_assert(sizeof(VmciDriverInfo) <= sizeof(DevsmanServicePayload),
		"VmciDriverInfo exceeds Devsman receive buffer");
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
		{0x15ADu, 0x0740u, "vmci.elf"},
	};

	constexpr const char* always_load_ring_drivers[] = {
		"flopdisk.elf",
	};

	constexpr uint32 DriverRestartLimit = 0;
	uni::Vector<DriverProcessRecord> driver_processes;
	bool driver_directory_ready = false;
	#if (_MCCA & 0xFF00) == 0x8600
	VmciDriverInfo vmci_driver_info = {
		VmciProtocolVersion,
		int32(VmciStatus::Unavailable),
		0,
		VmciInvalidId,
		VmciIpcPayloadCapacity,
		VmciEndpointCapacity,
		0,
		VmciDoorbellCapacity,
		VmciEventSubscriptionCapacity,
		VmciQueuePairCapacity,
		VmciQueuePairMaxQueueSize,
	};
	#endif

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

	#if (_MCCA & 0xFF00) == 0x8600
	void ClearVmciDriver(stduint driver_tid) {
		if (vmci_driver_info.driver_tid != driver_tid) return;
		vmci_driver_info.status = int32(VmciStatus::Unavailable);
		vmci_driver_info.driver_tid = 0;
		vmci_driver_info.context_id = VmciInvalidId;
	}

	bool IsVmciDriver(stduint source) {
		auto* node = Devsman::FindOwnedNode(source);
		return node && node->fields.vendor_id == 0x15ADu &&
			node->fields.device_id == 0x0740u;
	}

	void DispatchVmciDiscovery(stduint type, stduint source,
		const DevsmanServicePayload& payload) {
		if (type == _IMM(VmciMsg::DriverAttach)) {
			VmciDriverInfo request = {};
			MemCopyN(&request, payload.data, sizeof(request));
			VmciDriverInfo response = vmci_driver_info;
			response.status = int32(VmciStatus::InvalidArgument);
			response.driver_tid = 0;
			if (!IsVmciDriver(source)) {
				response.status = int32(VmciStatus::AccessDenied);
			}
			else if (request.version == VmciProtocolVersion &&
				request.status == int32(VmciStatus::Success) &&
				request.context_id > VmciHostContextId &&
				request.context_id != VmciInvalidId &&
				request.max_payload == VmciIpcPayloadCapacity &&
				request.endpoint_capacity == VmciEndpointCapacity &&
				(request.features & ~(VmciFeatureNotifications |
					VmciFeatureEvents | VmciFeatureQueuePairs |
					VmciFeatureGuestRpcBackdoor)) == 0 &&
				(request.features & VmciFeatureEvents) != 0 &&
				request.doorbell_capacity == VmciDoorbellCapacity &&
				request.event_subscription_capacity ==
					VmciEventSubscriptionCapacity &&
				request.queue_pair_capacity == VmciQueuePairCapacity &&
				request.queue_pair_max_queue_size ==
					VmciQueuePairMaxQueueSize) {
				vmci_driver_info = request;
				vmci_driver_info.driver_tid = uint32(source);
				response = vmci_driver_info;
			}
			if (syssend_async(source, &response, sizeof(response), type)) {
				plogwarn("[Devsman] VMCI attach reply failed tid=%u", source);
			}
			return;
		}

		if (syssend_async(source, &vmci_driver_info,
			sizeof(vmci_driver_info), type)) {
			plogwarn("[Devsman] VMCI lookup reply failed tid=%u", source);
		}
	}
	#endif

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
		ProcessBlock* task = Taskman::CreateFile(record.path, drv_ring, Task_Devsman, nullptr, false);
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
		const auto& runtime_root = Filesys::GetSystemRuntimeRootPath();
		if (!runtime_root.getByteCount()) return false;

		String drv_dir_str = String::newFormat("%s/drvs", runtime_root.reference());
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
		ClearVmciDriver(event.pid);
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
			else if (type == _IMM(VmciMsg::DriverAttach) ||
				type == _IMM(VmciMsg::DriverLookup)) {
				DispatchVmciDiscovery(type, source, payload);
			}
			#endif
			#if _MCCA == 0x8664
			else if (type == _IMM(DevsmanMsg::USB_MSC_READY)) {
				if (source != Task_FileSys) {
					plogwarn("[Devsman] USB MSC ready from invalid source=%u", source);
					continue;
				}
				void* context = nullptr;
				MemCopyN(&context, payload.data, sizeof(context));
				Devsman::RegisterUSBMSCStorage(context);
			}
			else if (type == _IMM(DevsmanMsg::USB_MSC_REMOVE)) {
				if (source != Task_FileSys) {
					plogwarn("[Devsman] USB MSC removal from invalid source=%u", source);
					continue;
				}
				void* context = nullptr;
				MemCopyN(&context, payload.data, sizeof(context));
				Devsman::RemoveUSBMSCStorage(context);
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
