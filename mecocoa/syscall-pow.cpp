// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Syscalls - Power Calls
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include "../include/syscall-pow.hpp"
#if (_MCCA & 0xFF00) == 0x8600
#include <cpp/Device/Bus/PCI.hpp>
#endif

extern "C" stdsint sysc_UMAP(stduint addr, stduint len);

bool IsPwcall(syscall_t callid) {
	return _IMM(callid) >= _IMM(syscall_t::POWERCALL_HELLO) &&
		_IMM(callid) <= _IMM(syscall_t::POWERCALL_DEV_PUBLISH);
}

static constexpr stduint PwcallDeviceHandleBase = 1;
static constexpr stduint PwcallDmaHandleBase = 0x10000;
#if (_MCCA & 0xFF00) == 0x8600
static constexpr uint8 PwcallNetIrqVector = 0x78;
#endif

namespace {
	static void FreePwcallHandleSlotNode(pureptr_t inp);
	static void FreePwcallDmaSlotNode(pureptr_t inp);
	static void FreePwcallProcessHandlesNode(pureptr_t inp);

	struct PwcallHandleSlot {
		stduint handle_id = 0;
		PwcallDeviceHandleEntry entry = {};
	};

	struct PwcallProcessHandles {
		stduint pid = 0;
		stduint next_handle = PwcallDeviceHandleBase;
		stduint next_dma_handle = PwcallDmaHandleBase;
		dchain_t handles = {};
		dchain_t dmas = {};
	};

	struct PwcallDmaSlot {
		stduint handle_id = 0;
		stduint physical = 0;
		stduint size = 0;
		stduint mapped_addr = 0;
	};

	dchain_t power_handle_table = {};
	alignas(Spinlock) byte power_handle_lock_raw[sizeof(Spinlock)] = {};
	#if (_MCCA & 0xFF00) == 0x8600
	stduint pwcall_net_irq_tid = 0;
	#endif

	static inline Spinlock* PwcallHandleLock() {
		return reinterpret_cast<Spinlock*>(power_handle_lock_raw);
	}

	static inline void EnsurePwcallHandleTableInitialized() {
		if (!power_handle_table.func_free) {
			DchainInit(&power_handle_table);
			power_handle_table.func_free = FreePwcallProcessHandlesNode;
		}
	}

	static void FreePwcallHandleSlotNode(pureptr_t inp) {
		auto* node = reinterpret_cast<Dnode*>(inp);
		if (node && node->offs) {
			memf(node->offs);
		}
	}

	static void FreePwcallDmaSlotNode(pureptr_t inp) {
		auto* node = reinterpret_cast<Dnode*>(inp);
		auto* slot = node ? reinterpret_cast<PwcallDmaSlot*>(node->offs) : nullptr;
		if (!slot) return;
		if (slot->physical && slot->size) {
			#if (_MCCA & 0xFF00) == 0x8600
			if (DmaLowIsInRange((void*)slot->physical)) {
				DmaLowFree((void*)slot->physical, slot->size);
			}
			else {
				mempool.deallocate((void*)slot->physical, slot->size);
			}
			#else
			mempool.deallocate((void*)slot->physical, slot->size);
			#endif
		}
		memf(slot);
	}

	static void FreePwcallProcessHandlesNode(pureptr_t inp) {
		auto* node = reinterpret_cast<Dnode*>(inp);
		auto* owner = node ? reinterpret_cast<PwcallProcessHandles*>(node->offs) : nullptr;
		if (!owner) return;
		DchainDrop(&owner->handles);
		DchainDrop(&owner->dmas);
		memf(owner);
	}

	static void RemovePwcallHandleChainNode(dchain_t* chain, Dnode* node) {
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

#if (_MCCA & 0xFF00) == 0x8600
static bool PwcallIsE1000Node(const DeviceNode* node) {
	if (!node || DeviceNodeType(node->fields.node_type) != DeviceNodeType::PciDevice) return false;
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

static void PwcallEnablePciDeviceAccess(DeviceNode* node) {
	if (!node || DeviceNodeType(node->fields.node_type) != DeviceNodeType::PciDevice) return;
	uni::PCI::Device dev{};
	dev.bus = node->fields.pci_bus;
	dev.device = node->fields.pci_device;
	dev.function = node->fields.pci_function;
	dev.header_type = 0;
	dev.class_code.base = node->fields.class_base;
	dev.class_code.sub = node->fields.class_sub;
	dev.class_code.interface = node->fields.class_if;
	uint16 cmd = uint16(uni::PCI::read_config_register(dev, 0x04) & 0xFFFFu);
	cmd |= 0x0004u;
	for (uint32 i = 0; i < node->fields.resource_count; ++i) {
		const auto& res = node->fields.resources[i];
		if (DeviceResourceType(res.type) == DeviceResourceType::PciBarMmio) cmd |= 0x0002u;
		if (DeviceResourceType(res.type) == DeviceResourceType::PciBarIo ||
			DeviceResourceType(res.type) == DeviceResourceType::IoPortRange) cmd |= 0x0001u;
	}
	uni::PCI::write_config_register(dev, 0x04, cmd);
}

extern "C" void Handint_E1000();

static bool PwcallRouteNetInterrupt(DeviceNode* node, stduint tid) {
	if (!PwcallIsE1000Node(node) || !tid) return false;
	const auto* irq = Devsman::FindResource(node, DeviceResourceType::IrqLine, 0);
	if (!irq) return false;
	const uint8 line = uint8(irq->start);
	if (line >= 24 || IC.getType() == 0) return false;
	#if _MCCA == 0x8664
	IC[PwcallNetIrqVector].setModeRupt(mglb(Handint_E1000_Entry), SegCo64);
	#else
	IC[PwcallNetIrqVector].setRange(mglb(Handint_E1000_Entry), SegCo32);
	#endif
	register_interrupt_handler(PwcallNetIrqVector, Handint_E1000);
	IC.IO_Writ64(0x10 + line * 2, PwcallNetIrqVector);
	pwcall_net_irq_tid = tid;
	return true;
}
#endif

#if (_MCCA & 0xFF00) == 0x8600
extern "C" void Handint_E1000() {
	if (pwcall_net_irq_tid) rupt_proc(pwcall_net_irq_tid, PwcallNetIrqVector);
	IC.SendEOI(PwcallNetIrqVector);
}
#endif

static bool IsDeviceNodeReachable(DeviceNode* node, DeviceNode* target) {
	for (auto* crt = node; crt; crt = reinterpret_cast<DeviceNode*>(crt->link.next)) {
		if (crt == target) return true;
		if (crt->link.subf && IsDeviceNodeReachable(reinterpret_cast<DeviceNode*>(crt->link.subf), target)) {
			return true;
		}
	}
	return false;
}

static DeviceNode* ResolvePwcallDevice(stduint node_id, stduint cls, stduint flags) {
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

static PwcallProcessHandles* FindPwcallProcessHandles(stduint pid) {
	EnsurePwcallHandleTableInitialized();
	for (auto* crt = power_handle_table.root_node; crt; crt = crt->next) {
		auto* owner = reinterpret_cast<PwcallProcessHandles*>(crt->offs);
		if (owner && owner->pid == pid) {
			return owner;
		}
	}
	return nullptr;
}

static PwcallProcessHandles* GetOrCreatePwcallProcessHandles(stduint pid) {
	auto* owner = FindPwcallProcessHandles(pid);
	if (owner) return owner;
	owner = zalcof(PwcallProcessHandles);
	if (!owner) return nullptr;
	owner->pid = pid;
	owner->next_handle = PwcallDeviceHandleBase;
	owner->next_dma_handle = PwcallDmaHandleBase;
	DchainInit(&owner->handles);
	owner->handles.func_free = FreePwcallHandleSlotNode;
	DchainInit(&owner->dmas);
	owner->dmas.func_free = FreePwcallDmaSlotNode;
	EnsurePwcallHandleTableInitialized();
	DchainAppend(&power_handle_table, owner, false, nullptr);
	return owner;
}

static PwcallHandleSlot* FindPwcallHandleSlotByNode(PwcallProcessHandles* owner, DeviceNode* node) {
	if (!owner || !node) return nullptr;
	for (auto* crt = owner->handles.root_node; crt; crt = crt->next) {
		auto* slot = reinterpret_cast<PwcallHandleSlot*>(crt->offs);
		if (slot && slot->entry.node == node) {
			return slot;
		}
	}
	return nullptr;
}

static Dnode* FindPwcallHandleSlotNodeByHandle(PwcallProcessHandles* owner, stduint dev_handle) {
	if (!owner || dev_handle < PwcallDeviceHandleBase) return nullptr;
	for (auto* crt = owner->handles.root_node; crt; crt = crt->next) {
		auto* slot = reinterpret_cast<PwcallHandleSlot*>(crt->offs);
		if (slot && slot->handle_id == dev_handle) {
			return crt;
		}
	}
	return nullptr;
}

static Dnode* FindPwcallDmaSlotNodeByHandle(PwcallProcessHandles* owner, stduint dma_handle) {
	if (!owner || dma_handle < PwcallDmaHandleBase) return nullptr;
	for (auto* crt = owner->dmas.root_node; crt; crt = crt->next) {
		auto* slot = reinterpret_cast<PwcallDmaSlot*>(crt->offs);
		if (slot && slot->handle_id == dma_handle) {
			return crt;
		}
	}
	return nullptr;
}

static stduint AllocPwcallDeviceHandle(ProcessBlock* pb, DeviceNode* node, uint32 flags) {
	if (!pb || !node) return 0;
	SpinlockLocal guard(PwcallHandleLock());
	auto* owner = GetOrCreatePwcallProcessHandles(pb->pid);
	if (!owner) return 0;
	auto* slot = FindPwcallHandleSlotByNode(owner, node);
	if (slot) {
		slot->entry.flags = flags;
		return slot->handle_id;
	}
	slot = zalcof(PwcallHandleSlot);
	if (!slot) return 0;
	slot->handle_id = owner->next_handle++;
	slot->entry.node = node;
	slot->entry.flags = flags;
	DchainAppend(&owner->handles, slot, false, nullptr);
	return slot->handle_id;
}

static DeviceNode* ResolvePwcallDeviceHandle(ProcessBlock* pb, stduint dev_handle) {
	if (!pb || dev_handle < PwcallDeviceHandleBase) return nullptr;
	SpinlockLocal guard(PwcallHandleLock());
	auto* owner = FindPwcallProcessHandles(pb->pid);
	auto* node = FindPwcallHandleSlotNodeByHandle(owner, dev_handle);
	if (!node) return nullptr;
	auto* slot = reinterpret_cast<PwcallHandleSlot*>(node->offs);
	return slot ? slot->entry.node : nullptr;
}

static bool ClosePwcallDeviceHandle(ProcessBlock* pb, stduint dev_handle) {
	if (!pb || dev_handle < PwcallDeviceHandleBase) return false;
	SpinlockLocal guard(PwcallHandleLock());
	auto* owner = FindPwcallProcessHandles(pb->pid);
	if (!owner) return false;
	auto* slot_node = FindPwcallHandleSlotNodeByHandle(owner, dev_handle);
	if (!slot_node) return false;
	RemovePwcallHandleChainNode(&owner->handles, slot_node);
	return true;
}

static stduint MapPwcallPhysicalRange(ProcessBlock* pb, stduint phys_begin, stduint want_length, uint32 map_flags) {
	if (!pb || !phys_begin || !want_length) return 0;
	const uint64 aligned_phys = uint64(phys_begin) & ~0xFFFull;
	const uint64 page_bias = uint64(phys_begin) - aligned_phys;
	const uint64 map_size64 = (page_bias + uint64(want_length) + 0xFFFu) & ~0xFFFu;
	if (!map_size64 || (uint64)(stduint)aligned_phys != aligned_phys || (uint64)(stduint)map_size64 != map_size64) {
		return 0;
	}
	const stduint map_size = stduint(map_size64);
	const stduint page_flags =
		PGPROP_user_access |
		((map_flags & _IMM(PwcallDeviceMapFlag::Writable)) ? PGPROP_writable : 0);

	SpinlockLocal guard(&pb->vma_lock);
	if (&pb->paging == &kernel_paging) return 0;
	const stduint vaddr = pb->heaptop;
	pb->heaptop += map_size;

	VirtualMemoryArea vma(vaddr, pb->heaptop, page_flags);
	vma.vm_type = VMA_DEVICE;
	pb->vmas.Append(vma);

	for (stduint off = 0; off < map_size; off += 0x1000) {
		const stduint virt = vaddr + off;
		const stduint phys = stduint(aligned_phys) + off;
		pb->paging.Map(virt, phys, 0x1000, PAGESIZE_4KB, PGPROP_present | page_flags);
	}
	return vaddr + stduint(page_bias);
}

static stdsint HandlePwcallDeviceDmaAlloc(ProcessBlock* pb, stduint dev_handle, stduint size, stduint flags) {
	(void)flags;
	if (!pb || !dev_handle || !size) return -1;
	if (!ResolvePwcallDeviceHandle(pb, dev_handle)) return -1;
	const stduint alloc_size = (size + 0xFFFu) & ~0xFFFu;
	void* phys = nullptr;
	#if (_MCCA & 0xFF00) == 0x8600
	phys = DmaLowAlloc(alloc_size);
	#endif
	if (!phys) {
		phys = mempool.allocate(alloc_size, 12);
	}
	if (!phys) return -1;

	SpinlockLocal guard(PwcallHandleLock());
	auto* owner = GetOrCreatePwcallProcessHandles(pb->pid);
	if (!owner) {
		#if (_MCCA & 0xFF00) == 0x8600
		if (DmaLowIsInRange(phys)) DmaLowFree(phys, alloc_size);
		else mempool.deallocate(phys, alloc_size);
		#else
		mempool.deallocate(phys, alloc_size);
		#endif
		return -1;
	}
	auto* slot = zalcof(PwcallDmaSlot);
	if (!slot) {
		#if (_MCCA & 0xFF00) == 0x8600
		if (DmaLowIsInRange(phys)) DmaLowFree(phys, alloc_size);
		else mempool.deallocate(phys, alloc_size);
		#else
		mempool.deallocate(phys, alloc_size);
		#endif
		return -1;
	}
	slot->handle_id = owner->next_dma_handle++;
	slot->physical = stduint(phys);
	slot->size = alloc_size;
	slot->mapped_addr = 0;
	DchainAppend(&owner->dmas, slot, false, nullptr);
	return stdsint(slot->handle_id);
}

static stdsint HandlePwcallDeviceDmaFree(ProcessBlock* pb, stduint dma_handle) {
	if (!pb || dma_handle < PwcallDmaHandleBase) return -1;
	SpinlockLocal guard(PwcallHandleLock());
	auto* owner = FindPwcallProcessHandles(pb->pid);
	if (!owner) return -1;
	auto* slot_node = FindPwcallDmaSlotNodeByHandle(owner, dma_handle);
	if (!slot_node) return -1;
	auto* slot = reinterpret_cast<PwcallDmaSlot*>(slot_node->offs);
	if (slot && slot->mapped_addr && slot->size) {
		sysc_UMAP(slot->mapped_addr, slot->size);
		slot->mapped_addr = 0;
	}
	if (slot && slot->physical && slot->size) {
		#if (_MCCA & 0xFF00) == 0x8600
		if (DmaLowIsInRange((void*)slot->physical)) {
			DmaLowFree((void*)slot->physical, slot->size);
		}
		else {
			mempool.deallocate((void*)slot->physical, slot->size);
		}
		#else
		mempool.deallocate((void*)slot->physical, slot->size);
		#endif
		slot->physical = 0;
		slot->size = 0;
	}
	RemovePwcallHandleChainNode(&owner->dmas, slot_node);
	return 0;
}

static stdsint HandlePwcallDeviceDmaMap(ProcessBlock* pb, stduint dma_handle, stduint args, stduint flags) {
	(void)flags;
	if (!pb || dma_handle < PwcallDmaHandleBase || !args) return -1;
	PwcallDeviceDmaMapRequest request{};
	MccaMemCopyP(&request, nullptr, true, (void*)args, pb, false, sizeof(request));

	stduint physical = 0;
	stduint size = 0;
	stduint mapped_addr = 0;
	{
		SpinlockLocal guard(PwcallHandleLock());
		auto* owner = FindPwcallProcessHandles(pb->pid);
		if (!owner) return -1;
		auto* slot_node = FindPwcallDmaSlotNodeByHandle(owner, dma_handle);
		if (!slot_node) return -1;
		auto* slot = reinterpret_cast<PwcallDmaSlot*>(slot_node->offs);
		if (!slot || !slot->physical || !slot->size) return -1;
		physical = slot->physical;
		size = slot->size;
		mapped_addr = slot->mapped_addr;
	}

	stduint duplicate_map = 0;
	bool map_valid = true;
	if (!mapped_addr) {
		mapped_addr = MapPwcallPhysicalRange(pb, physical, size,
			request.map_flags ? request.map_flags : _IMM(PwcallDeviceMapFlag::Writable));
		if (!mapped_addr) return -1;
		{
			SpinlockLocal guard(PwcallHandleLock());
			auto* owner = FindPwcallProcessHandles(pb->pid);
			if (!owner) {
				duplicate_map = mapped_addr;
				map_valid = false;
			}
			else {
				auto* slot_node = FindPwcallDmaSlotNodeByHandle(owner, dma_handle);
				auto* slot = slot_node ? reinterpret_cast<PwcallDmaSlot*>(slot_node->offs) : nullptr;
				if (!slot || slot->physical != physical || slot->size != size) {
					duplicate_map = mapped_addr;
					map_valid = false;
				}
				else if (!slot->mapped_addr) {
					slot->mapped_addr = mapped_addr;
				}
				else if (slot->mapped_addr != mapped_addr) {
					duplicate_map = mapped_addr;
					mapped_addr = slot->mapped_addr;
				}
			}
		}
	}

	if (duplicate_map) {
		sysc_UMAP(duplicate_map & ~_IMM(0xFFF), size);
	}
	if (!map_valid) return -1;
	request.physical = physical;
	request.length = size;
	request.address = mapped_addr;
	MccaMemCopyP((void*)args, pb, false, &request, nullptr, true, sizeof(request));
	return 0;
}

static stdsint HandlePwcallDeviceProper(ProcessBlock* pb, DeviceNode* node, stduint proper, stduint args) {
	if (!pb || !node) return -1;
	switch (PwcallDeviceProper(proper)) {
	case PwcallDeviceProper::GetIdentity:
	{
		if (!args) return -1;
		PwcallDeviceIdentity info{};
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
	case PwcallDeviceProper::GetResourceCount:
		return node->fields.resource_count;
	case PwcallDeviceProper::GetResource:
	{
		if (!args || !node->fields.resources) return -1;
		PwcallDeviceResourceQuery query{};
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

static const DeviceResource* FindPwcallDeviceResource(const DeviceNode* node, uint32 resource_type, uint32 resource_index) {
	#if (_MCCA & 0xFF00) != 0x8600
	(void)node;
	(void)resource_type;
	(void)resource_index;
	return nullptr;
	#else
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
	#endif
}

bool PwcallValidateDeviceResourceRange(ProcessBlock* pb, stduint dev_handle, uint32 resource_type, uint32 resource_index, uint64 start, uint64 length) {
	#if (_MCCA & 0xFF00) == 0x8600
	if (!pb || !dev_handle || !length) return false;
	auto* node = ResolvePwcallDeviceHandle(pb, dev_handle);
	const auto* res = FindPwcallDeviceResource(node, resource_type, resource_index);
	if (!res) return false;
	if (resource_type && res->type != resource_type) return false;
	if (start < res->start) return false;
	const uint64 end = start + length;
	const uint64 resource_end = res->start + res->length;
	if (end < start) return false;
	if (resource_end < res->start) return false;
	return end <= resource_end;
	#else
	(void)pb;
	(void)dev_handle;
	(void)resource_type;
	(void)resource_index;
	(void)start;
	(void)length;
	return false;
	#endif
}

static stdsint HandlePwcallDevicePublishFramebufferAperture(ProcessBlock* pb, DeviceNode* node, stduint args) {
	#if (_MCCA & 0xFF00) == 0x8600
	if (!pb || !node || !args) return -1;
	PwcallDeviceFramebufferAperture aperture{};
	MccaMemCopyP(&aperture, nullptr, true, (void*)args, pb, false, sizeof(aperture));
	if (aperture.resource_type != _IMM(DeviceResourceType::PciBarMmio)) return -1;
	if (!aperture.start || !aperture.length) return -1;
	if (node->fields.class_base != 0x03u) return -1;
	for (uint32 i = 0; i < node->fields.resource_count; ++i) {
		auto* res = &node->fields.resources[i];
		if (res->type != aperture.resource_type || res->index != aperture.resource_index) continue;
		if (!(res->flags & DeviceResourceFlag_SizeEstimated)) return -1;
		if (res->start != aperture.start) return -1;
		if (aperture.length <= res->length) return 0;
		if (aperture.length > 256ull * 1024ull * 1024ull) return -1;
		res->length = aperture.length;
		return 0;
	}
	return -1;
	#else
	(void)pb;
	(void)node;
	(void)args;
	return -1;
	#endif
}

static stdsint HandlePwcallDeviceMap(ProcessBlock* pb, DeviceNode* node, stduint args) {
	if (!pb || !node || !args) return -1;
	PwcallDeviceMapRequest request{};
	MccaMemCopyP(&request, nullptr, true, (void*)args, pb, false, sizeof(request));

	const uint32 resource_type = request.resource_type ? request.resource_type : _IMM(DeviceResourceType::PciBarMmio);
	const auto* res = FindPwcallDeviceResource(node, resource_type, request.resource_index);
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
		((request.map_flags & _IMM(PwcallDeviceMapFlag::Writable)) ? PGPROP_writable : 0);

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

static stdsint HandlePwcallDeviceUnmap(ProcessBlock* pb, stduint addr, stduint len, stduint flags) {
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

static stdsint HandlePwcallDeviceIo(ProcessBlock* pb, DeviceNode* node, stduint args, bool is_write) {
	if (!pb || !node || !args) return -1;
	PwcallDeviceIoRequest request{};
	MccaMemCopyP(&request, nullptr, true, (void*)args, pb, false, sizeof(request));

	#if (_MCCA & 0xFF00) == 0x8600
	const uint32 resource_type = request.resource_type ? request.resource_type : _IMM(DeviceResourceType::PciBarIo);
	const auto* res = FindPwcallDeviceResource(node, resource_type, request.resource_index);
	if (!res) {
		res = FindPwcallDeviceResource(node, _IMM(DeviceResourceType::IoPortRange), request.resource_index);
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

void CleanupPwcallProcessHandles(stduint pid) {
	SpinlockLocal guard(PwcallHandleLock());
	EnsurePwcallHandleTableInitialized();
	for (auto* crt = power_handle_table.root_node; crt; ) {
		auto* next = crt->next;
		auto* owner = reinterpret_cast<PwcallProcessHandles*>(crt->offs);
		if (owner && owner->pid == pid) {
			RemovePwcallHandleChainNode(&power_handle_table, crt);
			return;
		}
		crt = next;
	}
}

stdsint HandlePwcall(syscall_t callid, stduint p1, stduint p2, stduint p3) {
	auto pb = Taskman::CurrentPB();
	if (!pb || pb->ring != RING_S) {
		return -1;
	}

	switch (callid) {
	case syscall_t::POWERCALL_HELLO:
		return 0;
	case syscall_t::POWERCALL_DEV_OPEN:
	{
		auto* node = ResolvePwcallDevice(p1, p2, p3);
		if (!node) return -1;
		#if (_MCCA & 0xFF00) == 0x8600
		PwcallEnablePciDeviceAccess(node);
		#endif
		const stduint handle = AllocPwcallDeviceHandle(pb, node, uint32(p3));
		return handle ? (stdsint)handle : -1;
	}
	case syscall_t::POWERCALL_DEV_CLOSE:
		return ClosePwcallDeviceHandle(pb, p1) ? 0 : -1;
	case syscall_t::POWERCALL_DEV_PROPER:
	{
		auto* node = ResolvePwcallDeviceHandle(pb, p1);
		return HandlePwcallDeviceProper(pb, node, p2, p3);
	}
	case syscall_t::POWERCALL_DEV_CTRL:
	{
		auto* node = ResolvePwcallDeviceHandle(pb, p1);
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
		auto* node = ResolvePwcallDeviceHandle(pb, p1);
		return HandlePwcallDeviceMap(pb, node, p2);
	}
	case syscall_t::POWERCALL_DEV_UMAP:
		return HandlePwcallDeviceUnmap(pb, p1, p2, p3);
	case syscall_t::POWERCALL_DEV_IO_READ:
	{
		auto* node = ResolvePwcallDeviceHandle(pb, p1);
		return HandlePwcallDeviceIo(pb, node, p2, false);
	}
	case syscall_t::POWERCALL_DEV_IO_WRITE:
	{
		auto* node = ResolvePwcallDeviceHandle(pb, p1);
		return HandlePwcallDeviceIo(pb, node, p2, true);
	}
	case syscall_t::POWERCALL_DEV_READ:
	case syscall_t::POWERCALL_DEV_WRITE:
	case syscall_t::POWERCALL_DEV_WAIT:
	case syscall_t::POWERCALL_DEV_ACK:
		return -1;
	case syscall_t::POWERCALL_DEV_DMA_ALLOC:
		return HandlePwcallDeviceDmaAlloc(pb, p1, p2, p3);
	case syscall_t::POWERCALL_DEV_DMA_FREE:
		return HandlePwcallDeviceDmaFree(pb, p1);
	case syscall_t::POWERCALL_DEV_DMA_MAP:
		return HandlePwcallDeviceDmaMap(pb, p1, p2, p3);
	case syscall_t::POWERCALL_DEV_PUBLISH:
	{
		auto* node = ResolvePwcallDeviceHandle(pb, p1);
		if (!node) return -1;
		switch (PwcallDevicePublishCommand(p2)) {
		case PwcallDevicePublishCommand::Started:
			if (!node->fields.binding.driver_name) return -1;
			#if (_MCCA & 0xFF00) == 0x8600
			(void)PwcallRouteNetInterrupt(node, Taskman::CurrentTID());
			#endif
			node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
			node->fields.binding.probe_result = 0;
			return 0;
		case PwcallDevicePublishCommand::FramebufferAperture:
			return HandlePwcallDevicePublishFramebufferAperture(pb, node, p3);
		default:
			return -1;
		}
	}
	default:
		return -1;
	}
}
