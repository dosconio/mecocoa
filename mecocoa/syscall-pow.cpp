// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Syscalls - Power Calls
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include "../include/syscall-pow.hpp"

extern "C" stdsint sysc_UMAP(stduint addr, stduint len);

bool IsPowerCall(syscall_t callid) {
	return _IMM(callid) >= _IMM(syscall_t::POWERCALL_HELLO) &&
		_IMM(callid) <= _IMM(syscall_t::POWERCALL_DEV_PUBLISH);
}

static constexpr stduint PowerDeviceHandleBase = 1;

namespace {
	static void FreePowerHandleSlotNode(pureptr_t inp);
	static void FreePowerProcessHandlesNode(pureptr_t inp);

	struct PowerHandleSlot {
		stduint handle_id = 0;
		PowerDeviceHandleEntry entry = {};
	};

	struct PowerProcessHandles {
		stduint pid = 0;
		stduint next_handle = PowerDeviceHandleBase;
		dchain_t handles = {};
	};

	dchain_t power_handle_table = {};
	alignas(Spinlock) byte power_handle_lock_raw[sizeof(Spinlock)] = {};

	static inline Spinlock* PowerHandleLock() {
		return reinterpret_cast<Spinlock*>(power_handle_lock_raw);
	}

	static inline void EnsurePowerHandleTableInitialized() {
		if (!power_handle_table.func_free) {
			DchainInit(&power_handle_table);
			power_handle_table.func_free = FreePowerProcessHandlesNode;
		}
	}

	static void FreePowerHandleSlotNode(pureptr_t inp) {
		auto* node = reinterpret_cast<Dnode*>(inp);
		if (node && node->offs) {
			memf(node->offs);
		}
	}

	static void FreePowerProcessHandlesNode(pureptr_t inp) {
		auto* node = reinterpret_cast<Dnode*>(inp);
		auto* owner = node ? reinterpret_cast<PowerProcessHandles*>(node->offs) : nullptr;
		if (!owner) return;
		DchainDrop(&owner->handles);
		memf(owner);
	}

	static void RemovePowerHandleChainNode(dchain_t* chain, Dnode* node) {
		if (!chain || !node) return;
		auto* left = node->left;
		auto* next = node->next;
		if (left) left->next = next;
		else chain->root_node = next;
		if (next) next->left = left;
		else chain->last_node = left;
		if (chain->node_count) chain->node_count--;
		DnodeRemove(node, chain->func_free);
	}
}

static bool IsDeviceNodeReachable(DeviceNode* node, DeviceNode* target) {
	for (auto* crt = node; crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
		if (crt == target) return true;
		if (crt->link.subf && IsDeviceNodeReachable(reinterpret_cast<DeviceNode*>(crt->link.subf), target)) {
			return true;
		}
	}
	return false;
}

static DeviceNode* ResolvePowerDevice(stduint node_id, stduint cls, stduint flags) {
	(void)flags;
	auto* root = Devsman::Root();
	if (!root) return nullptr;
	if (node_id) {
		auto* node = reinterpret_cast<DeviceNode*>(node_id);
		return IsDeviceNodeReachable(root, node) ? node : nullptr;
	}
	#if (_MCCA & 0xFF00) == 0x8600
	if (cls > 0xFFFFu) {
		return Devsman::FindPCIDeviceByVendorDevice(uint16(cls >> 16), uint16(cls & 0xFFFFu));
	}
	#else
	(void)cls;
	#endif
	return nullptr;
}

static PowerProcessHandles* FindPowerProcessHandles(stduint pid) {
	EnsurePowerHandleTableInitialized();
	for (auto* crt = power_handle_table.root_node; crt; crt = crt->next) {
		auto* owner = reinterpret_cast<PowerProcessHandles*>(crt->offs);
		if (owner && owner->pid == pid) {
			return owner;
		}
	}
	return nullptr;
}

static PowerProcessHandles* GetOrCreatePowerProcessHandles(stduint pid) {
	auto* owner = FindPowerProcessHandles(pid);
	if (owner) return owner;
	owner = zalcof(PowerProcessHandles);
	if (!owner) return nullptr;
	owner->pid = pid;
	owner->next_handle = PowerDeviceHandleBase;
	DchainInit(&owner->handles);
	owner->handles.func_free = FreePowerHandleSlotNode;
	EnsurePowerHandleTableInitialized();
	DchainAppend(&power_handle_table, owner, false, nullptr);
	return owner;
}

static PowerHandleSlot* FindPowerHandleSlotByNode(PowerProcessHandles* owner, DeviceNode* node) {
	if (!owner || !node) return nullptr;
	for (auto* crt = owner->handles.root_node; crt; crt = crt->next) {
		auto* slot = reinterpret_cast<PowerHandleSlot*>(crt->offs);
		if (slot && slot->entry.node == node) {
			return slot;
		}
	}
	return nullptr;
}

static Dnode* FindPowerHandleSlotNodeByHandle(PowerProcessHandles* owner, stduint dev_handle) {
	if (!owner || dev_handle < PowerDeviceHandleBase) return nullptr;
	for (auto* crt = owner->handles.root_node; crt; crt = crt->next) {
		auto* slot = reinterpret_cast<PowerHandleSlot*>(crt->offs);
		if (slot && slot->handle_id == dev_handle) {
			return crt;
		}
	}
	return nullptr;
}

static stduint AllocPowerDeviceHandle(ProcessBlock* pb, DeviceNode* node, uint32 flags) {
	if (!pb || !node) return 0;
	SpinlockLocal guard(PowerHandleLock());
	auto* owner = GetOrCreatePowerProcessHandles(pb->pid);
	if (!owner) return 0;
	auto* slot = FindPowerHandleSlotByNode(owner, node);
	if (slot) {
		slot->entry.flags = flags;
		return slot->handle_id;
	}
	slot = zalcof(PowerHandleSlot);
	if (!slot) return 0;
	slot->handle_id = owner->next_handle++;
	slot->entry.node = node;
	slot->entry.flags = flags;
	DchainAppend(&owner->handles, slot, false, nullptr);
	return slot->handle_id;
}

static DeviceNode* ResolvePowerDeviceHandle(ProcessBlock* pb, stduint dev_handle) {
	if (!pb || dev_handle < PowerDeviceHandleBase) return nullptr;
	SpinlockLocal guard(PowerHandleLock());
	auto* owner = FindPowerProcessHandles(pb->pid);
	auto* node = FindPowerHandleSlotNodeByHandle(owner, dev_handle);
	if (!node) return nullptr;
	auto* slot = reinterpret_cast<PowerHandleSlot*>(node->offs);
	return slot ? slot->entry.node : nullptr;
}

static bool ClosePowerDeviceHandle(ProcessBlock* pb, stduint dev_handle) {
	if (!pb || dev_handle < PowerDeviceHandleBase) return false;
	SpinlockLocal guard(PowerHandleLock());
	auto* owner = FindPowerProcessHandles(pb->pid);
	if (!owner) return false;
	auto* slot_node = FindPowerHandleSlotNodeByHandle(owner, dev_handle);
	if (!slot_node) return false;
	RemovePowerHandleChainNode(&owner->handles, slot_node);
	return true;
}

static stdsint HandlePowerDeviceProper(ProcessBlock* pb, DeviceNode* node, stduint proper, stduint args) {
	if (!pb || !node) return -1;
	switch (PowerDeviceProper(proper)) {
	case PowerDeviceProper::GetIdentity:
	{
		if (!args) return -1;
		PowerDeviceIdentity info{};
		info.node_type = node->fields.node_type;
		info.bus_type = node->fields.bus_type;
		info.dev_class = node->fields.dev_class;
		info.vendor_id = node->fields.vendor_id;
		info.device_id = node->fields.device_id;
		info.revision = node->fields.revision;
		info.class_base = node->fields.class_base;
		info.class_sub = node->fields.class_sub;
		info.class_if = node->fields.class_if;
		info.pci_segment = node->fields.pci_segment;
		info.pci_bus = node->fields.pci_bus;
		info.pci_device = node->fields.pci_device;
		info.pci_function = node->fields.pci_function;
		MccaMemCopyP((void*)args, pb, false, &info, nullptr, true, sizeof(info));
		return 0;
	}
	case PowerDeviceProper::GetResourceCount:
		return node->fields.resource_count;
	case PowerDeviceProper::GetResource:
	{
		if (!args || !node->fields.resources) return -1;
		PowerDeviceResourceQuery query{};
		MccaMemCopyP(&query, nullptr, true, (void*)args, pb, false, sizeof(query));
		if (query.index >= node->fields.resource_count) return -1;
		const auto* res = &node->fields.resources[query.index];
		query.resource.type = res->type;
		query.resource.flags = res->flags;
		query.resource.index = res->index;
		query.resource.start = res->start;
		query.resource.length = res->length;
		query.resource.extra = res->extra;
		MccaMemCopyP((void*)args, pb, false, &query, nullptr, true, sizeof(query));
		return 0;
	}
	default:
		return -1;
	}
}

static const DeviceResource* FindPowerDeviceResource(const DeviceNode* node, uint32 resource_type, uint32 resource_index) {
	if (!node) return nullptr;
	if (resource_type) {
		return Devsman::FindResource(node, DeviceResourceType(resource_type), resource_index);
	}
	for (uint32 i = 0; i < node->fields.resource_count; ++i) {
		const auto* res = &node->fields.resources[i];
		if (res->index == resource_index) {
			return res;
		}
	}
	return nullptr;
}

static stdsint HandlePowerDeviceMap(ProcessBlock* pb, DeviceNode* node, stduint args) {
	if (!pb || !node || !args) return -1;
	PowerDeviceMapRequest request{};
	MccaMemCopyP(&request, nullptr, true, (void*)args, pb, false, sizeof(request));

	const auto* res = request.resource_type
		? FindPowerDeviceResource(node, request.resource_type, request.resource_index)
		: Devsman::FindResource(node, DeviceResourceType::PciBarMmio, request.resource_index);
	if (!res || DeviceResourceType(res->type) != DeviceResourceType::PciBarMmio) return -1;
	if (request.offset > res->length) return -1;

	const uint64 map_begin = res->start + request.offset;
	const uint64 max_length = res->length - request.offset;
	const uint64 want_length = request.length ? request.length : max_length;
	if (!want_length || want_length > max_length) return -1;

	const uint64 phys_begin = map_begin & ~0xFFFull;
	const uint64 page_bias = map_begin - phys_begin;
	const uint64 map_size64 = (page_bias + want_length + 0xFFFu) & ~0xFFFu;
	if (!map_size64) return -1;
	if ((uint64)(stduint)phys_begin != phys_begin) return -1;
	if ((uint64)(stduint)map_size64 != map_size64) return -1;

	const stduint map_size = (stduint)map_size64;
	const stduint page_flags =
		PGPROP_user_access |
		((request.map_flags & _IMM(PowerDeviceMapFlag::Writable)) ? PGPROP_writable : 0);

	SpinlockLocal guard(&pb->vma_lock);
	if (&pb->paging == &kernel_paging) return -1;
	const stduint vaddr = pb->heaptop;
	pb->heaptop += map_size;

	VirtualMemoryArea vma(vaddr, pb->heaptop, page_flags);
	vma.vm_type = VMA_DEVICE;
	pb->vmas.Append(vma);

	for (stduint page_off = 0; page_off < map_size; page_off += 0x1000) {
		pb->paging.Map(
			vaddr + page_off,
			(stduint)(phys_begin + page_off),
			0x1000,
			PAGESIZE_4KB,
			PGPROP_present | page_flags);
		RefreshVirtualAddress(vaddr + page_off);
	}
	return vaddr + (stduint)page_bias;
}

static stdsint HandlePowerDeviceUnmap(ProcessBlock* pb, stduint addr, stduint len, stduint flags) {
	(void)pb;
	(void)flags;
	if (!addr || !len) return -1;
	const uint64 end_addr = uint64(addr) + uint64(len);
	const stduint aligned_addr = addr & ~_IMM(0xFFF);
	const uint64 aligned_end64 = (end_addr + 0xFFFu) & ~0xFFFu;
	if ((uint64)(stduint)aligned_end64 != aligned_end64) return -1;
	const stduint aligned_end = (stduint)aligned_end64;
	if (aligned_end <= aligned_addr) return -1;
	return sysc_UMAP(aligned_addr, aligned_end - aligned_addr);
}

static stdsint HandlePowerDeviceIo(ProcessBlock* pb, DeviceNode* node, stduint args, bool is_write) {
	if (!pb || !node || !args) return -1;
	PowerDeviceIoRequest request{};
	MccaMemCopyP(&request, nullptr, true, (void*)args, pb, false, sizeof(request));

	#if (_MCCA & 0xFF00) == 0x8600
	const auto* res = request.resource_type
		? FindPowerDeviceResource(node, request.resource_type, request.resource_index)
		: Devsman::FindResource(node, DeviceResourceType::PciBarIo, request.resource_index);
	if (!res) {
		res = Devsman::FindResource(node, DeviceResourceType::IoPortRange, request.resource_index);
	}
	if (!res) return -1;

	const auto res_type = DeviceResourceType(res->type);
	if (res_type != DeviceResourceType::PciBarIo && res_type != DeviceResourceType::IoPortRange) return -1;
	if (request.width != 1 && request.width != 2 && request.width != 4) return -1;
	if (request.offset + request.width > res->length) return -1;

	const uint64 port64 = res->start + request.offset;
	if (port64 + request.width > 0x10000ull) return -1;
	const uint16 port = uint16(port64);

	if (is_write) {
		switch (request.width) {
		case 1: OUT_b(port, uint8(request.value)); break;
		case 2: OUT_w(port, uint16(request.value)); break;
		case 4: OUT_d(port, uint32(request.value)); break;
		default: return -1;
		}
		return 0;
	}

	switch (request.width) {
	case 1: request.value = IN_b(port); break;
	case 2: request.value = IN_w(port); break;
	case 4: request.value = IN_d(port); break;
	default: return -1;
	}
	MccaMemCopyP((void*)args, pb, false, &request, nullptr, true, sizeof(request));
	return 0;
	#else
	(void)is_write;
	return -1;
	#endif
}

void CleanupPowerProcessHandles(stduint pid) {
	SpinlockLocal guard(PowerHandleLock());
	EnsurePowerHandleTableInitialized();
	for (auto* crt = power_handle_table.root_node; crt; ) {
		auto* next = crt->next;
		auto* owner = reinterpret_cast<PowerProcessHandles*>(crt->offs);
		if (owner && owner->pid == pid) {
			RemovePowerHandleChainNode(&power_handle_table, crt);
			return;
		}
		crt = next;
	}
}

stdsint HandlePowerCall(syscall_t callid, stduint p1, stduint p2, stduint p3) {
	auto pb = Taskman::CurrentPB();
	if (!pb || pb->ring != RING_S) {
		return -1;
	}

	switch (callid) {
	case syscall_t::POWERCALL_HELLO:
		return 0;
	case syscall_t::POWERCALL_DEV_OPEN:
	{
		auto* node = ResolvePowerDevice(p1, p2, p3);
		if (!node) return -1;
		const stduint handle = AllocPowerDeviceHandle(pb, node, uint32(p3));
		return handle ? (stdsint)handle : -1;
	}
	case syscall_t::POWERCALL_DEV_CLOSE:
		return ClosePowerDeviceHandle(pb, p1) ? 0 : -1;
	case syscall_t::POWERCALL_DEV_PROPER:
	{
		auto* node = ResolvePowerDeviceHandle(pb, p1);
		return HandlePowerDeviceProper(pb, node, p2, p3);
	}
	case syscall_t::POWERCALL_DEV_CTRL:
	{
		auto* node = ResolvePowerDeviceHandle(pb, p1);
		#if (_MCCA & 0xFF00) == 0x8600
		if (!node || !node->fields.ops || !node->fields.ops->ctrl || !p3) return -1;
		switch (VideoCtrlCommand(p2)) {
		case VideoCtrlCommand::GetFramebufferInfo:
		{
			FramebufferInfo info{};
			const stdsint ret = Devsman::Ctrl(node, p2, &info, 0);
			if (ret != 0) return ret;
			MccaMemCopyP((void*)p3, pb, false, &info, nullptr, true, sizeof(info));
			return 0;
		}
		case VideoCtrlCommand::SetVideoMode:
		{
			VideoMode mode{};
			MccaMemCopyP(&mode, nullptr, true, (void*)p3, pb, false, sizeof(mode));
			return Devsman::Ctrl(node, p2, &mode, 0);
		}
		default:
			return -1;
		}
		#else
		(void)node;
		(void)p2;
		(void)p3;
		return -1;
		#endif
	}
	case syscall_t::POWERCALL_DEV_MMAP:
	{
		auto* node = ResolvePowerDeviceHandle(pb, p1);
		return HandlePowerDeviceMap(pb, node, p2);
	}
	case syscall_t::POWERCALL_DEV_UMAP:
		return HandlePowerDeviceUnmap(pb, p1, p2, p3);
	case syscall_t::POWERCALL_DEV_IO_READ:
	{
		auto* node = ResolvePowerDeviceHandle(pb, p1);
		return HandlePowerDeviceIo(pb, node, p2, false);
	}
	case syscall_t::POWERCALL_DEV_IO_WRITE:
	{
		auto* node = ResolvePowerDeviceHandle(pb, p1);
		return HandlePowerDeviceIo(pb, node, p2, true);
	}
	case syscall_t::POWERCALL_DEV_READ:
	case syscall_t::POWERCALL_DEV_WRITE:
	case syscall_t::POWERCALL_DEV_WAIT:
	case syscall_t::POWERCALL_DEV_ACK:
	case syscall_t::POWERCALL_DEV_DMA_ALLOC:
	case syscall_t::POWERCALL_DEV_DMA_FREE:
	case syscall_t::POWERCALL_DEV_DMA_MAP:
	case syscall_t::POWERCALL_DEV_PUBLISH:
		return -1;
	default:
		return -1;
	}
}
