// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Syscalls - Power Calls
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include "../include/syscall-pow.hpp"
#if (_MCCA & 0xFF00) == 0x8600
#include <cpp/Device/Bus/PCI.hpp>
#include <cpp/Device/DMA>
#include "../include/devsman-storage.hpp"
#endif

extern "C" stdsint sysc_UMAP(stduint addr, stduint len);

bool IsPwcall(syscall_t callid) {
	return _IMM(callid) >= _IMM(syscall_t::POWERCALL_HELLO) &&
		_IMM(callid) < _IMM(syscall_t::POWERCALL__END__);
}

static constexpr stduint PwcallDeviceHandleBase = 1;
static constexpr stduint PwcallDmaHandleBase = 0x10000;
#if (_MCCA & 0xFF00) == 0x8600
static constexpr uint8 PwcallPciIrqVectorBase = 0x78;
#if _MCCA == 0x8664
static constexpr uint8 PwcallPciIrqVectorExtensionBase = 0x82;
static constexpr uint8 PwcallPciIrqVectorCount = 32;
#else
static constexpr uint8 PwcallPciIrqVectorCount = 8;
#endif
static constexpr stduint PwcallIrqBindingCapacity = 32;
extern "C" void register_interrupt_vector_handler(stduint irq_id, void (*handler)(stduint));
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
		DeviceNode* device = nullptr;
		stduint physical = 0;
		uint64 device_address = 0;
		stduint size = 0;
		stduint mapped_addr = 0;
		uint8 dma_channel = 0xFF;
		bool iova_mapped = false;
	};

	#if (_MCCA & 0xFF00) == 0x8600
	enum class PwcallPciInterruptMode : uint8 {
		None,
		Intx,
		Msi,
		Msix,
	};

	struct PwcallDeviceIrqBinding {
		DeviceNode* node = nullptr;
		stduint owner_pid = 0;
		stduint owner_tid = 0;
		stduint device_handle = 0;
		uint32 resource_index = 0;
		uint32 generation = 0;
		uint32 pending_count = 0;
		uint32 delivery_count = 0;
		uint64 sequence = 0;
		uint16 delivery_flags = DeviceEventFlag_None;
		uint8 vector = 0;
		uint32 irq_line = 0xFFFFFFFFu;
		uint8 msi_capability = 0;
		uint16 interrupt_remapping_handle = 0;
		PwcallPciInterruptMode pci_interrupt_mode = PwcallPciInterruptMode::None;
		bool level_triggered = false;
		bool active_low = false;
		bool shareable = false;
		bool active = false;
		bool awaiting_ack = false;
		bool delivery_pending = false;
		bool overflowed = false;
	};
	#endif

	dchain_t power_handle_table = {};
	alignas(Spinlock) byte power_handle_lock_raw[sizeof(Spinlock)] = {};
	#if (_MCCA & 0xFF00) == 0x8600
	PwcallDeviceIrqBinding pwcall_irq_bindings[PwcallIrqBindingCapacity] = {};
	alignas(Spinlock) byte pwcall_irq_lock_raw[sizeof(Spinlock)] = {};
	uint32 pwcall_irq_generation = 0;
	#endif

	static inline Spinlock* PwcallHandleLock() {
		return reinterpret_cast<Spinlock*>(power_handle_lock_raw);
	}

	#if (_MCCA & 0xFF00) == 0x8600
	static inline Spinlock* PwcallIrqLock() {
		return reinterpret_cast<Spinlock*>(pwcall_irq_lock_raw);
	}
	#endif

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

	#if (_MCCA & 0xFF00) == 0x8600
	static void PwcallDmaWrite8(void*, uint16 port, byte value) {
		OUT_b(port, value);
	}

	const uni::DMA8237_IO pwcall_dma_io = { nullptr, PwcallDmaWrite8 };
	const uni::DMA8237_t pwcall_dma(pwcall_dma_io);

	static void StopPwcallDmaSlot(PwcallDmaSlot* slot) {
		if (!slot || slot->dma_channel > 7 || slot->dma_channel == 4) return;
		(void)pwcall_dma.Abort(slot->dma_channel);
		slot->dma_channel = 0xFF;
	}
	#else
	static void StopPwcallDmaSlot(PwcallDmaSlot*) {}
	#endif

	static void FreePwcallDmaSlotNode(pureptr_t inp) {
		auto* node = reinterpret_cast<Dnode*>(inp);
		auto* slot = node ? reinterpret_cast<PwcallDmaSlot*>(node->offs) : nullptr;
		if (!slot) return;
		StopPwcallDmaSlot(slot);
		#if (_MCCA & 0xFF00) == 0x8600
		if (slot->iova_mapped && !Iova::UnmapDeviceDma(slot->device,
			slot->device_address, slot->size)) {
			plogerro("[IOMMU] Failed to release DMA mapping iova=%[64H] bytes=%u",
				slot->device_address, slot->size);
			slot->physical = 0;
		}
		#endif
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

	static void DetachPwcallHandleChainNode(dchain_t* chain, Dnode* node) {
		if (!chain || !node) return;
		auto* left = node->left;
		auto* next = node->next;
		if (left) left->next = next;
		else chain->root_node = next;
		if (next) next->left = left;
		else chain->last_node = left;
		if (chain->node_count) chain->node_count--;
		node->left = nullptr;
		node->next = nullptr;
	}
}

#if (_MCCA & 0xFF00) == 0x8600
static uint8 PwcallLegacyVectorToLine(uint8 vector) {
	if (vector >= IRQ_PIT && vector < IRQ_PIT + 8) return vector - IRQ_PIT;
	if (vector >= IRQ_RTC && vector < IRQ_RTC + 8) return uint8(8 + vector - IRQ_RTC);
	return 0xFF;
}

static stduint PwcallPciInterruptEntry(uint8 vector) {
	uint8 index = 0;
	#if _MCCA == 0x8664
	if (vector >= PwcallPciIrqVectorBase && vector < PwcallPciIrqVectorBase + 8)
		index = uint8(vector - PwcallPciIrqVectorBase);
	else if (vector >= PwcallPciIrqVectorExtensionBase &&
		vector < PwcallPciIrqVectorExtensionBase + PwcallPciIrqVectorCount - 8)
		index = uint8(vector - PwcallPciIrqVectorExtensionBase + 8u);
	else return 0;
	#else
	if (vector < PwcallPciIrqVectorBase ||
		vector >= PwcallPciIrqVectorBase + PwcallPciIrqVectorCount) return 0;
	index = uint8(vector - PwcallPciIrqVectorBase);
	#endif
	switch (index) {
	case 0: return mglb(Handint_PWDEV0_Entry);
	case 1: return mglb(Handint_PWDEV1_Entry);
	case 2: return mglb(Handint_PWDEV2_Entry);
	case 3: return mglb(Handint_PWDEV3_Entry);
	case 4: return mglb(Handint_PWDEV4_Entry);
	case 5: return mglb(Handint_PWDEV5_Entry);
	case 6: return mglb(Handint_PWDEV6_Entry);
	case 7: return mglb(Handint_PWDEV7_Entry);
	#if _MCCA == 0x8664
	case 8: return mglb(Handint_PWDEV8_Entry);
	case 9: return mglb(Handint_PWDEV9_Entry);
	case 10: return mglb(Handint_PWDEV10_Entry);
	case 11: return mglb(Handint_PWDEV11_Entry);
	case 12: return mglb(Handint_PWDEV12_Entry);
	case 13: return mglb(Handint_PWDEV13_Entry);
	case 14: return mglb(Handint_PWDEV14_Entry);
	case 15: return mglb(Handint_PWDEV15_Entry);
	case 16: return mglb(Handint_PWDEV16_Entry);
	case 17: return mglb(Handint_PWDEV17_Entry);
	case 18: return mglb(Handint_PWDEV18_Entry);
	case 19: return mglb(Handint_PWDEV19_Entry);
	case 20: return mglb(Handint_PWDEV20_Entry);
	case 21: return mglb(Handint_PWDEV21_Entry);
	case 22: return mglb(Handint_PWDEV22_Entry);
	case 23: return mglb(Handint_PWDEV23_Entry);
	case 24: return mglb(Handint_PWDEV24_Entry);
	case 25: return mglb(Handint_PWDEV25_Entry);
	case 26: return mglb(Handint_PWDEV26_Entry);
	case 27: return mglb(Handint_PWDEV27_Entry);
	case 28: return mglb(Handint_PWDEV28_Entry);
	case 29: return mglb(Handint_PWDEV29_Entry);
	case 30: return mglb(Handint_PWDEV30_Entry);
	case 31: return mglb(Handint_PWDEV31_Entry);
	#endif
	default: return 0;
	}
}

static uint8 PwcallPciInterruptVector(stduint index) {
	#if _MCCA == 0x8664
	if (index >= 8) return uint8(PwcallPciIrqVectorExtensionBase + index - 8);
	#endif
	return uint8(PwcallPciIrqVectorBase + index);
}

static void PwcallSetInterruptLineMask(uint32 line, bool masked) {
	if (IC.getType() == 0) {
		if (line < 8) {
			byte value = innpb(PORT_i8259A_MAS_B);
			if (masked) value |= byte(1u << line);
			else value &= byte(~(1u << line));
			outpb(PORT_i8259A_MAS_B, value);
		}
		else if (line < 16) {
			byte value = innpb(PORT_i8259A_SLV_B);
			if (masked) value |= byte(1u << (line - 8));
			else value &= byte(~(1u << (line - 8)));
			outpb(PORT_i8259A_SLV_B, value);
			if (!masked) {
				const byte master = innpb(PORT_i8259A_MAS_B) & byte(~(1u << 2));
				outpb(PORT_i8259A_MAS_B, master);
			}
		}
		return;
	}
	#if _MCCA == 0x8664
	uint64 route = 0;
	if (!Devs::x64_ioapic_read_route(line, route)) return;
	if (masked) route |= uint64(1) << 16;
	else route &= ~(uint64(1) << 16);
	Devs::x64_ioapic_write_route(line, route);
	#else
	if (line >= 24) return;
	const byte index = byte(0x10 + line * 2);
	uint64 route = IC.IO_Read64(index);
	if (masked) route |= uint64(1) << 16;
	else route &= ~(uint64(1) << 16);
	IC.IO_Writ64(index, route);
	#endif
}

static void PwcallRefreshInterruptLine(uint32 line) {
	bool active = false;
	bool blocked = false;
	for0(i, numsof(pwcall_irq_bindings)) {
		const auto& binding = pwcall_irq_bindings[i];
		if (!binding.active || binding.irq_line != line) continue;
		active = true;
		if (binding.level_triggered && binding.awaiting_ack) blocked = true;
	}
	PwcallSetInterruptLineMask(line, !active || blocked);
}

static bool PwcallVectorInUse(uint8 vector) {
	for0(i, numsof(pwcall_irq_bindings)) {
		if (pwcall_irq_bindings[i].active && pwcall_irq_bindings[i].vector == vector) return true;
	}
	return false;
}

static PwcallDeviceIrqBinding* PwcallFindLineBinding(uint32 line) {
	for0(i, numsof(pwcall_irq_bindings)) {
		auto& binding = pwcall_irq_bindings[i];
		if (binding.active && binding.irq_line == line) return &binding;
	}
	return nullptr;
}

static PwcallDeviceIrqBinding* PwcallFindFreeIrqBinding() {
	for0(i, numsof(pwcall_irq_bindings)) {
		if (!pwcall_irq_bindings[i].active) return &pwcall_irq_bindings[i];
	}
	return nullptr;
}

	struct PwcallPciInterruptCapabilities {
		uint8 msi = 0;
		uint8 msix = 0;
	};

static PwcallPciInterruptCapabilities PwcallFindPciInterruptCapabilities(
	const uni::PCI::Device& dev) {
	uni::PCI pci;
	PwcallPciInterruptCapabilities result{};
	uint8 capability_address = uint8(uni::PCI::read_config_register(dev, 0x34));
	for0(i, 48) {
		if (capability_address < 0x40 || (capability_address & 3u)) break;
		const auto capability = pci.read_capability_header(dev, capability_address);
		if (capability.bits.cap_id == pci.kCapabilityMSI)
			result.msi = capability_address;
		else if (capability.bits.cap_id == pci.kCapabilityMSIX)
			result.msix = capability_address;
		if (!capability.bits.next_ptr || capability.bits.next_ptr == capability_address) break;
		capability_address = capability.bits.next_ptr;
	}
	return result;
}

static void PwcallDisablePciMsi(DeviceNode* node, uint8 capability_address) {
	if (!node || !capability_address ||
		DeviceNodeType(node->fields.node_type) != DeviceNodeType::PciDevice) return;
	uni::PCI::Device dev{};
	dev.segment_group = node->fields.pci_segment;
	dev.bus = node->fields.pci_bus;
	dev.device = node->fields.pci_device;
	dev.function = node->fields.pci_function;
	uint32 control = uni::PCI::read_config_register(dev, capability_address);
	control &= ~(1u << 16);
	uni::PCI::write_config_register(dev, capability_address, control);
}

static void PwcallDisablePciInterrupt(DeviceNode* node,
	PwcallPciInterruptMode mode, uint8 msi_capability,
	uint16 interrupt_remapping_handle) {
	if (node && DeviceNodeType(node->fields.node_type) == DeviceNodeType::PciDevice) {
		if (mode == PwcallPciInterruptMode::Msi) {
			PwcallDisablePciMsi(node, msi_capability);
		}
		else if (mode == PwcallPciInterruptMode::Msix) {
			uni::PCI::Device dev{};
			dev.segment_group = node->fields.pci_segment;
			dev.bus = node->fields.pci_bus;
			dev.device = node->fields.pci_device;
			dev.function = node->fields.pci_function;
			uni::PCI pci;
			(void)pci.DisableMSIX(dev);
		}
	}
	Iova::ReleaseInterrupt(interrupt_remapping_handle);
}

static DeviceEvent PwcallMakeDeviceInterruptEvent(const PwcallDeviceIrqBinding& binding,
	uint32 count, uint16 extra_flags = DeviceEventFlag_None) {
	DeviceEvent event = {};
	event.kind = _IMM(DeviceEventKind::Interrupt);
	event.flags = DeviceEventFlag_NeedsAck | extra_flags;
	event.device_handle = uint32(binding.device_handle);
	event.source = binding.resource_index;
	event.count = count;
	event.generation = binding.generation;
	event.sequence = binding.sequence;
	return event;
}

static bool PwcallDeliverPendingInterrupts(stduint vector) {
	while (true) {
		DeviceEvent event = {};
		stduint owner_tid = 0;
		{
			SpinlockLocal guard(PwcallIrqLock());
			for0(i, numsof(pwcall_irq_bindings)) {
				auto& binding = pwcall_irq_bindings[i];
				if (!binding.active || binding.vector != vector ||
					!binding.delivery_pending) continue;
				binding.delivery_pending = false;
				owner_tid = binding.owner_tid;
				event = PwcallMakeDeviceInterruptEvent(binding,
					binding.delivery_count, binding.delivery_flags);
				break;
			}
		}
		if (!owner_tid) return true;
		if (device_event_proc(owner_tid, event)) {
			SpinlockLocal guard(PwcallIrqLock());
			for0(i, numsof(pwcall_irq_bindings)) {
				auto& binding = pwcall_irq_bindings[i];
				if (!binding.active || binding.owner_tid != owner_tid ||
					binding.device_handle != event.device_handle ||
					binding.resource_index != event.source ||
					binding.generation != event.generation ||
					binding.sequence != event.sequence) continue;
				binding.delivery_count = 0;
				binding.delivery_flags = DeviceEventFlag_None;
				break;
			}
			continue;
		}
		{
			SpinlockLocal guard(PwcallIrqLock());
			for0(i, numsof(pwcall_irq_bindings)) {
				auto& binding = pwcall_irq_bindings[i];
				if (!binding.active || !binding.awaiting_ack ||
					binding.owner_tid != owner_tid ||
					binding.device_handle != event.device_handle ||
					binding.resource_index != event.source ||
					binding.generation != event.generation ||
					binding.sequence != event.sequence) continue;
				binding.delivery_pending = true;
				break;
			}
		}
		return false;
	}
}

void device_interrupt_proc(stduint vector) {
	uint32 masked_lines[PwcallIrqBindingCapacity] = {};
	stduint masked_line_count = 0;
	{
		SpinlockLocal guard(PwcallIrqLock());
		for0(i, numsof(pwcall_irq_bindings)) {
			auto& binding = pwcall_irq_bindings[i];
			if (!binding.active || binding.vector != vector) continue;
			if (binding.level_triggered) {
				bool already_masked = false;
				for0(line_index, masked_line_count) {
					if (masked_lines[line_index] == binding.irq_line) {
						already_masked = true;
						break;
					}
				}
				if (!already_masked && masked_line_count < numsof(masked_lines)) {
					PwcallSetInterruptLineMask(binding.irq_line, true);
					masked_lines[masked_line_count++] = binding.irq_line;
				}
			}
			if (binding.awaiting_ack) {
				if (binding.pending_count != 0xFFFFFFFFu) binding.pending_count++;
				else binding.overflowed = true;
				continue;
			}
			binding.awaiting_ack = true;
			binding.sequence++;
			binding.delivery_count = 1;
			binding.delivery_flags = DeviceEventFlag_None;
			binding.delivery_pending = true;
		}
	}
	(void)PwcallDeliverPendingInterrupts(vector);
}

bool device_interrupt_retry(stduint vector) {
	{
		SpinlockLocal guard(PwcallIrqLock());
		for0(i, numsof(pwcall_irq_bindings)) {
			auto& binding = pwcall_irq_bindings[i];
			if (!binding.active || binding.vector != vector || binding.awaiting_ack) continue;
			binding.awaiting_ack = true;
			binding.sequence++;
			binding.delivery_count = 1;
			binding.delivery_flags = DeviceEventFlag_None;
			binding.delivery_pending = true;
		}
	}
	return PwcallDeliverPendingInterrupts(vector);
}

static void PwcallHandleDeviceInterrupt(stduint vector) {
	device_interrupt_proc(vector);
	IC.SendEOI(vector);
}

bool device_interrupt_bind(stduint owner_pid, stduint owner_tid,
	uint32 device_handle, uint32 source, uint8 vector, uint32* generation) {
	if (!owner_pid || !owner_tid || !device_handle || vector < 0x20) return false;
	stduint old_owner_tid = 0;
	uint32 old_generation = 0;
	uint32 new_generation = 0;
	{
		SpinlockLocal guard(PwcallIrqLock());
		PwcallDeviceIrqBinding* slot = nullptr;
		stduint owner_binding_count = 0;
		for0(i, numsof(pwcall_irq_bindings)) {
			auto& binding = pwcall_irq_bindings[i];
			if (!binding.active) continue;
			if (binding.owner_tid == owner_tid && binding.device_handle != device_handle) {
				owner_binding_count++;
			}
			if (!binding.node && binding.vector == vector &&
				binding.device_handle == device_handle) {
				slot = &binding;
				continue;
			}
			if (binding.vector == vector) return false;
		}
		if (owner_binding_count >= DeviceEventQueue::Capacity) return false;
		if (!slot) slot = PwcallFindFreeIrqBinding();
		if (!slot) return false;
		if (slot->active) {
			old_owner_tid = slot->owner_tid;
			old_generation = slot->generation;
		}

		auto& binding = *slot;
		binding = {};
		binding.owner_pid = owner_pid;
		binding.owner_tid = owner_tid;
		binding.device_handle = device_handle;
		binding.resource_index = source;
		binding.generation = ++pwcall_irq_generation;
		if (!binding.generation) binding.generation = ++pwcall_irq_generation;
		new_generation = binding.generation;
		binding.vector = vector;
		binding.irq_line = 0xFFFFFFFFu;
		binding.active = true;
	}
	if (old_owner_tid) device_event_cancel(old_owner_tid, device_handle, old_generation);
	if (generation) *generation = new_generation;
	return true;
}

bool device_interrupt_ack(stduint owner_pid, stduint owner_tid, const DeviceEvent& acknowledged) {
	if (!owner_pid || !owner_tid || acknowledged.version != DeviceEventProtocolVersion ||
		DeviceEventKind(acknowledged.kind) != DeviceEventKind::Interrupt ||
		!(acknowledged.flags & DeviceEventFlag_NeedsAck) || !acknowledged.device_handle) return false;
	stduint deliver_vector = 0;
	{
		SpinlockLocal guard(PwcallIrqLock());
		PwcallDeviceIrqBinding* found = nullptr;
		for0(i, numsof(pwcall_irq_bindings)) {
			auto& binding = pwcall_irq_bindings[i];
			if (binding.active && binding.owner_pid == owner_pid &&
				binding.owner_tid == owner_tid &&
				binding.device_handle == acknowledged.device_handle) {
				found = &binding;
				break;
			}
		}
		if (!found || !found->awaiting_ack || found->generation != acknowledged.generation ||
			found->resource_index != acknowledged.source ||
			found->sequence != acknowledged.sequence) return false;
		if (found->pending_count) {
			const uint32 count = found->pending_count;
			found->pending_count = 0;
			found->sequence++;
			uint16 flags = DeviceEventFlag_Coalesced;
			if (found->overflowed) flags |= DeviceEventFlag_Overflow;
			found->overflowed = false;
			found->delivery_count = count;
			found->delivery_flags = flags;
			found->delivery_pending = true;
			deliver_vector = found->vector;
		}
		else {
			found->awaiting_ack = false;
			if (found->level_triggered) PwcallRefreshInterruptLine(found->irq_line);
		}
	}
	if (deliver_vector) (void)PwcallDeliverPendingInterrupts(deliver_vector);
	return true;
}

static bool PwcallBindDeviceInterrupt(ProcessBlock* process, stduint device_handle,
	DeviceNode* node, stduint owner_tid, bool msi_only, bool msix_preferred) {
	if (!process || !device_handle || !node || !owner_tid) return false;
	const auto* irq = Devsman::FindResource(node, DeviceResourceType::IrqLine, 0);
	const bool pci = DeviceNodeType(node->fields.node_type) == DeviceNodeType::PciDevice;
	const bool using_ioapic = IC.getType() != 0;
	if (pci && !using_ioapic) return false;
	uni::PCI::Device pci_device{};
	uint8 msi_capability = 0;
	uint32 target_lapic_id = 0;
	bool target_lapic_routable = false;
	PwcallPciInterruptMode interrupt_mode = pci ?
		PwcallPciInterruptMode::Intx : PwcallPciInterruptMode::None;
	if (pci) {
		pci_device.segment_group = node->fields.pci_segment;
		pci_device.bus = node->fields.pci_bus;
		pci_device.device = node->fields.pci_device;
		pci_device.function = node->fields.pci_function;
		const stduint cpu_id = Taskman::getID();
		auto* percore = cpu_id < PCU_CORES_MAX ? Taskman::PCU_CORES_PERCORE[cpu_id] : nullptr;
		if (percore && percore->lapic_id != CORE_ID_INVALID &&
			(percore->lapic_id <= 0xFFu || Iova::InterruptRemappingAvailable())) {
			target_lapic_id = percore->lapic_id;
			target_lapic_routable = true;
			const auto capabilities =
				PwcallFindPciInterruptCapabilities(pci_device);
			msi_capability = capabilities.msi;
			// Preserve the established MSI path unless the driver opts into MSI-X.
			if (msix_preferred && capabilities.msix)
				interrupt_mode = PwcallPciInterruptMode::Msix;
			else if (capabilities.msi)
				interrupt_mode = PwcallPciInterruptMode::Msi;
		}
	}
	if (pci && !target_lapic_routable) return false;
	const bool use_message_interrupt =
		interrupt_mode == PwcallPciInterruptMode::Msi ||
		interrupt_mode == PwcallPciInterruptMode::Msix;
	if (pci && !use_message_interrupt && target_lapic_id > 0xFFu)
		return false;
	if (msi_only && !use_message_interrupt) return false;
	if (!irq && !use_message_interrupt) return false;
	if (!use_message_interrupt && irq->start > uint64(~uint32(0))) return false;
	if (!pci && irq->start > 0xFFu) return false;
	const uint8 requested_vector = pci ? 0 : uint8(irq->start);
	uint32 line = use_message_interrupt ? 0xFFFFFFFFu :
		(pci ? uint32(irq->start) : uint32(PwcallLegacyVectorToLine(requested_vector)));
	#if _MCCA == 0x8664
	bool isa_level_triggered = false;
	bool isa_active_low = false;
	if (!pci) {
		// ACPI MADT supplies the actual GSI and electrical mode for a legacy ISA IRQ.
		if (!using_ioapic || line >= 16 ||
			!Devs::x64_resolve_isa_irq(uint8(line), line, isa_level_triggered, isa_active_low)) return false;
	}
	#endif
	if (!use_message_interrupt && !using_ioapic && line >= 16) return false;
	#if _MCCA == 0x8664
	if (!use_message_interrupt && using_ioapic && !Devs::x64_ioapic_has_gsi(line)) return false;
	#else
	if (!use_message_interrupt && using_ioapic && line >= 24) return false;
	#endif
	if (!pci && requested_vector < 0x20) return false;
	bool level_triggered = irq && (irq->flags & DeviceResourceFlag_IrqLevel) != 0;
	bool active_low = irq && (irq->flags & DeviceResourceFlag_IrqActiveLow) != 0;
	const bool shareable = irq && (irq->flags & DeviceResourceFlag_IrqShareable) != 0;
	#if _MCCA == 0x8664
	if (!pci) {
		level_triggered = isa_level_triggered;
		active_low = isa_active_low;
	}
	#endif
	bool dynamic_vector = pci;
	#if _MCCA == 0x8664
	dynamic_vector = true;
	#endif

	uint32 generation = 0;
	uint8 vector = requested_vector;
	#if _MCCA == 0x8664
	uint64 intx_route = 0;
	#endif
	{
		SpinlockLocal guard(PwcallIrqLock());
		#if _MCCA == 0x8664
		if (pci && using_ioapic && !use_message_interrupt) {
			uint64 firmware_route = 0;
			Devs::x64_ioapic_read_route(line, firmware_route);
			if (firmware_route & (uint64(1) << 15)) {
				level_triggered = true;
				active_low = (firmware_route & (uint64(1) << 13)) != 0;
			}
		}
		#endif
		stduint owner_binding_count = 0;
		for0(i, numsof(pwcall_irq_bindings)) {
			const auto& active = pwcall_irq_bindings[i];
			if (active.active && active.owner_tid == owner_tid) owner_binding_count++;
			if (active.active && active.owner_pid == process->pid &&
				active.device_handle == device_handle) return active.owner_tid == owner_tid;
		}
		if (owner_binding_count >= DeviceEventQueue::Capacity) return false;

		auto* line_binding = use_message_interrupt ? nullptr : PwcallFindLineBinding(line);
		if (line_binding) {
			if (!line_binding->shareable || !shareable ||
				line_binding->level_triggered != level_triggered ||
				line_binding->active_low != active_low) return false;
			vector = line_binding->vector;
		}
		else if (dynamic_vector) {
			for (uint8 offset = 0; offset < PwcallPciIrqVectorCount; ++offset) {
				const uint8 candidate = PwcallPciInterruptVector(offset);
				if (!PwcallVectorInUse(candidate)) {
					vector = candidate;
					break;
				}
			}
			if (!vector) return false;
		}
		else if (PwcallVectorInUse(vector)) return false;

		auto* slot = PwcallFindFreeIrqBinding();
		if (!slot) return false;
		if (!use_message_interrupt) PwcallSetInterruptLineMask(line, true);

		auto& binding = *slot;
		binding = {};
		binding.node = node;
		binding.owner_pid = process->pid;
		binding.owner_tid = owner_tid;
		binding.device_handle = device_handle;
		binding.resource_index = irq ? irq->index : 0;
		binding.generation = ++pwcall_irq_generation;
		if (!binding.generation) binding.generation = ++pwcall_irq_generation;
		generation = binding.generation;
		binding.vector = vector;
		binding.irq_line = line;
		binding.msi_capability = msi_capability;
		binding.pci_interrupt_mode = interrupt_mode;
		binding.level_triggered = use_message_interrupt ? false : level_triggered;
		binding.active_low = use_message_interrupt ? false : active_low;
		binding.shareable = use_message_interrupt ? false : shareable;
		binding.active = true;

		if (using_ioapic && !use_message_interrupt) {
			#if _MCCA == 0x8664
			uint64 route = 0;
			Devs::x64_ioapic_read_route(line, route);
			#else
			const byte route_index = byte(0x10 + line * 2);
			uint64 route = IC.IO_Read64(route_index);
			#endif
			route &= ~uint64(0xFFu | (1u << 13) | (1u << 15));
			route |= vector;
			route |= uint64(1) << 16;
			if (active_low) route |= uint64(1) << 13;
			if (level_triggered) route |= uint64(1) << 15;
			#if _MCCA == 0x8664
			if (pci) {
				route &= ~(uint64(0xFFu) << 56);
				route |= uint64(uint8(target_lapic_id)) << 56;
			}
			Devs::x64_ioapic_write_route(line, route);
			intx_route = route;
			#else
			IC.IO_Writ64(route_index, route);
			#endif
		}
	}
	if (dynamic_vector) {
		const stduint entry = PwcallPciInterruptEntry(vector);
		if (!entry) return false;
		#if _MCCA == 0x8664
		IC[vector].setModeRupt(entry, SegCo64);
		#else
		IC[vector].setRange(entry, SegCo32);
		#endif
	}
	register_interrupt_vector_handler(vector, PwcallHandleDeviceInterrupt);
	#if _MCCA == 0x8664
	if (pci && !use_message_interrupt) {
		ploginfo("[DEVIRQ] INTx enabled pci=%02x:%02x.%u line=%u vector=%u rte=%[64H]",
			(unsigned)pci_device.bus, (unsigned)pci_device.device,
			(unsigned)pci_device.function, (unsigned)line, (unsigned)vector,
			intx_route);
	}
	#endif
	if (use_message_interrupt) {
		uni::PCI pci_bus;
		Iova::InterruptMessage interrupt_message{};
		const bool remapped = Iova::AllocatePciInterrupt(node,
			target_lapic_id, vector, interrupt_message);
		if (!remapped && target_lapic_id > 0xFFu) {
			SpinlockLocal guard(PwcallIrqLock());
			for0(i, numsof(pwcall_irq_bindings)) {
				auto& binding = pwcall_irq_bindings[i];
				if (binding.active && binding.generation == generation) {
					binding = {};
					break;
				}
			}
			return false;
		}
		uni::PCI::MSIMessage message{};
		if (remapped) {
			message.address = interrupt_message.address;
			message.data = interrupt_message.data;
		}
		else {
			message.address = 0xFEE00000u |
				(uint64(uint8(target_lapic_id)) << 12);
			message.data = vector;
		}
		uni::PCI_Result result = uni::PCI_Result::Success;
		if (interrupt_mode == PwcallPciInterruptMode::Msix) {
			// A Ring1 interrupt binding owns MSI-X table entry zero.
			result = pci_bus.ConfigureMSIX(pci_device, &message, 1);
		}
		else if (remapped) {
			result = pci_bus.configure_MSI(pci_device, uint32(message.address),
				message.data, 0);
		}
		else {
			result = pci_bus.configure_MSI_fixed_destination(
				pci_device, uint8(target_lapic_id),
				uni::PCI::MSITriggerMode::Edge,
				uni::PCI::MSIDeliveryMode::Fixed,
				vector, 0);
		}
		if (result) {
			{
				SpinlockLocal guard(PwcallIrqLock());
				for0(i, numsof(pwcall_irq_bindings)) {
					auto& binding = pwcall_irq_bindings[i];
					if (!binding.active || binding.generation != generation) continue;
					binding = {};
					break;
				}
			}
			PwcallDisablePciInterrupt(node, interrupt_mode,
				msi_capability, interrupt_message.handle);
			plogerro("[DEVIRQ] %s setup failed pci=%02x:%02x.%u: %s",
				interrupt_mode == PwcallPciInterruptMode::Msix ? "MSI-X" : "MSI",
				(unsigned)pci_device.bus, (unsigned)pci_device.device,
				(unsigned)pci_device.function, result.Name());
			return false;
		}
		bool binding_found = false;
		{
			SpinlockLocal guard(PwcallIrqLock());
			for0(i, numsof(pwcall_irq_bindings)) {
				auto& binding = pwcall_irq_bindings[i];
				if (binding.active && binding.generation == generation) {
					binding.interrupt_remapping_handle = interrupt_message.handle;
					binding_found = true;
					break;
				}
			}
		}
		if (!binding_found) {
			PwcallDisablePciInterrupt(node, interrupt_mode,
				msi_capability, interrupt_message.handle);
			return false;
		}
		ploginfo("[DEVIRQ] %s enabled pci=%02x:%02x.%u vector=%u lapic=%u%s",
			interrupt_mode == PwcallPciInterruptMode::Msix ? "MSI-X" : "MSI",
			(unsigned)pci_device.bus, (unsigned)pci_device.device,
			(unsigned)pci_device.function, (unsigned)vector,
			(unsigned)target_lapic_id, remapped ? " remapped" : "");
	}
	{
		SpinlockLocal guard(PwcallIrqLock());
		bool found = false;
		for0(i, numsof(pwcall_irq_bindings)) {
			const auto& binding = pwcall_irq_bindings[i];
			if (binding.active && binding.generation == generation) found = true;
		}
		if (!found) return false;
		if (!use_message_interrupt) PwcallRefreshInterruptLine(line);
	}
	return true;
}

static void PwcallUnbindDeviceInterrupt(stduint owner_pid, stduint device_handle) {
	struct PendingCancel {
		stduint owner_tid;
		uint32 generation;
		DeviceNode* node;
		uint8 msi_capability;
		uint16 interrupt_remapping_handle;
		PwcallPciInterruptMode interrupt_mode;
	};
	PendingCancel cancels[PwcallIrqBindingCapacity] = {};
	stduint cancel_count = 0;
	{
		SpinlockLocal guard(PwcallIrqLock());
		for0(i, numsof(pwcall_irq_bindings)) {
			auto& binding = pwcall_irq_bindings[i];
			if (!binding.active || binding.owner_pid != owner_pid ||
				binding.device_handle != device_handle) continue;
			const uint32 line = binding.irq_line;
			if (cancel_count < numsof(cancels)) {
				cancels[cancel_count++] = { binding.owner_tid, binding.generation,
					binding.node, binding.msi_capability,
					binding.interrupt_remapping_handle,
					binding.pci_interrupt_mode };
			}
			binding = {};
			PwcallRefreshInterruptLine(line);
		}
	}
	for0(i, cancel_count) {
		PwcallDisablePciInterrupt(cancels[i].node, cancels[i].interrupt_mode,
			cancels[i].msi_capability,
			cancels[i].interrupt_remapping_handle);
		device_event_cancel(cancels[i].owner_tid, device_handle, cancels[i].generation);
	}
}

static void PwcallUnbindProcessInterrupts(stduint owner_pid) {
	struct PendingCancel {
		stduint owner_tid;
		stduint device_handle;
		uint32 generation;
		DeviceNode* node;
		uint8 msi_capability;
		uint16 interrupt_remapping_handle;
		PwcallPciInterruptMode interrupt_mode;
	};
	PendingCancel cancels[PwcallIrqBindingCapacity] = {};
	stduint cancel_count = 0;
	{
		SpinlockLocal guard(PwcallIrqLock());
		for0(i, numsof(pwcall_irq_bindings)) {
			auto& binding = pwcall_irq_bindings[i];
			if (!binding.active || binding.owner_pid != owner_pid) continue;
			const uint32 line = binding.irq_line;
			if (cancel_count < numsof(cancels)) {
				cancels[cancel_count++] = {
					binding.owner_tid, binding.device_handle, binding.generation,
					binding.node, binding.msi_capability,
					binding.interrupt_remapping_handle,
					binding.pci_interrupt_mode
				};
			}
			binding = {};
			PwcallRefreshInterruptLine(line);
		}
	}
	for0(i, cancel_count) {
		PwcallDisablePciInterrupt(cancels[i].node, cancels[i].interrupt_mode,
			cancels[i].msi_capability,
			cancels[i].interrupt_remapping_handle);
		device_event_cancel(cancels[i].owner_tid, cancels[i].device_handle, cancels[i].generation);
	}
}

static stdsint PwcallAckDeviceInterrupt(ProcessBlock* process, stduint device_handle,
	uint64 sequence, uint32 generation) {
	if (!process || !device_handle) return -1;
	const bool recover = !sequence && !generation;
	stduint deliver_vector = 0;
	{
		SpinlockLocal guard(PwcallIrqLock());
		PwcallDeviceIrqBinding* found = nullptr;
		for0(i, numsof(pwcall_irq_bindings)) {
			auto& binding = pwcall_irq_bindings[i];
			if (binding.active && binding.owner_pid == process->pid &&
				binding.device_handle == device_handle) {
				found = &binding;
				break;
			}
		}
		if (!found) return -1;
		if (recover) {
			found->pending_count = 0;
			found->overflowed = false;
			found->delivery_count = 0;
			found->delivery_flags = DeviceEventFlag_None;
			found->delivery_pending = false;
			found->awaiting_ack = false;
			if (found->level_triggered) PwcallRefreshInterruptLine(found->irq_line);
			return 0;
		}
		if (!found->awaiting_ack || found->generation != generation ||
			found->sequence != sequence) return -1;
		if (found->pending_count) {
			const uint32 count = found->pending_count;
			found->pending_count = 0;
			found->sequence++;
			uint16 flags = DeviceEventFlag_Coalesced;
			if (found->overflowed) flags |= DeviceEventFlag_Overflow;
			found->overflowed = false;
			found->delivery_count = count;
			found->delivery_flags = flags;
			found->delivery_pending = true;
			deliver_vector = found->vector;
		}
		else {
			found->awaiting_ack = false;
			if (found->level_triggered) PwcallRefreshInterruptLine(found->irq_line);
		}
	}
	if (deliver_vector) (void)PwcallDeliverPendingInterrupts(deliver_vector);
	return 0;
}

static void PwcallEnablePciDeviceAccess(DeviceNode* node) {
	if (!node || DeviceNodeType(node->fields.node_type) != DeviceNodeType::PciDevice) return;
	uni::PCI::Device dev{};
	dev.segment_group = node->fields.pci_segment;
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

static void PwcallDisablePciDeviceDma(DeviceNode* node, stduint owner_pid) {
	if (!node || (node->fields.binding.owner_pid && node->fields.binding.owner_pid != uint32(owner_pid)) ||
		DeviceNodeType(node->fields.node_type) != DeviceNodeType::PciDevice) return;
	uni::PCI::Device dev{};
	dev.segment_group = node->fields.pci_segment;
	dev.bus = node->fields.pci_bus;
	dev.device = node->fields.pci_device;
	dev.function = node->fields.pci_function;
	dev.header_type = 0;
	dev.class_code.base = node->fields.class_base;
	dev.class_code.sub = node->fields.class_sub;
	dev.class_code.interface = node->fields.class_if;
	uint16 cmd = uint16(uni::PCI::read_config_register(dev, 0x04) & 0xFFFFu);
	cmd &= uint16(~0x0004u);
	uni::PCI::write_config_register(dev, 0x04, cmd);
}
#endif

void CleanupPwcallThreadInterrupts(stduint tid) {
	#if (_MCCA & 0xFF00) == 0x8600
	struct PendingDisable {
		DeviceNode* node;
		uint8 msi_capability;
		uint16 interrupt_remapping_handle;
		PwcallPciInterruptMode interrupt_mode;
	};
	PendingDisable disables[PwcallIrqBindingCapacity] = {};
	stduint disable_count = 0;
	{
		SpinlockLocal guard(PwcallIrqLock());
		for0(i, numsof(pwcall_irq_bindings)) {
			auto& binding = pwcall_irq_bindings[i];
			if (!binding.active || binding.owner_tid != tid) continue;
			const uint32 line = binding.irq_line;
			if (disable_count < numsof(disables)) {
				disables[disable_count++] = { binding.node,
					binding.msi_capability,
					binding.interrupt_remapping_handle,
					binding.pci_interrupt_mode };
			}
			binding = {};
			PwcallRefreshInterruptLine(line);
		}
	}
	for0(i, disable_count) {
		PwcallDisablePciInterrupt(disables[i].node, disables[i].interrupt_mode,
			disables[i].msi_capability,
			disables[i].interrupt_remapping_handle);
	}
	#else
	(void)tid;
	#endif
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

static DeviceNode* ResolvePwcallDevice(ProcessBlock* pb, stduint node_id, stduint cls, stduint flags) {
	(void)flags;
	auto* root = Devsman::Root();
	if (!root) return nullptr;
	if (node_id) {
		auto* node = reinterpret_cast<DeviceNode*>(node_id);
		return IsDeviceNodeReachable(root, node) ? node : nullptr;
	}
	if (!cls && pb) return Devsman::FindOwnedNode(pb->pid);
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

#if (_MCCA & 0xFF00) == 0x8600
DeviceNode* Powercall::ResolveOwnedDeviceHandle(stduint sender_tid, stduint handle, stduint* owner_pid) {
	auto* process = ProcessBlock::Acquire(sender_tid);
	if (!process) return nullptr;
	DeviceNode* node = nullptr;
	if (process->ring == RING_S) {
		node = ResolvePwcallDeviceHandle(process, handle);
		if (node && owner_pid) *owner_pid = process->pid;
	}
	ProcessBlock::Release(process);
	return node;
}
#endif

static uint32 ResolvePwcallDeviceHandleFlags(ProcessBlock* pb, stduint dev_handle) {
	if (!pb || dev_handle < PwcallDeviceHandleBase) return 0;
	SpinlockLocal guard(PwcallHandleLock());
	auto* owner = FindPwcallProcessHandles(pb->pid);
	auto* node = FindPwcallHandleSlotNodeByHandle(owner, dev_handle);
	if (!node) return 0;
	auto* slot = reinterpret_cast<PwcallHandleSlot*>(node->offs);
	return slot ? slot->entry.flags : 0;
}

static bool ClosePwcallDeviceHandle(ProcessBlock* pb, stduint dev_handle) {
	if (!pb || dev_handle < PwcallDeviceHandleBase) return false;
	#if (_MCCA & 0xFF00) == 0x8600
	PwcallUnbindDeviceInterrupt(pb->pid, dev_handle);
	#endif
	SpinlockLocal guard(PwcallHandleLock());
	auto* owner = FindPwcallProcessHandles(pb->pid);
	if (!owner) return false;
	auto* slot_node = FindPwcallHandleSlotNodeByHandle(owner, dev_handle);
	if (!slot_node) return false;
	RemovePwcallHandleChainNode(&owner->handles, slot_node);
	return true;
}

#if CONFIG_ENABLE_MMU
static stduint MapPwcallPhysicalRange(ProcessBlock* pb, stduint phys_begin,
	stduint want_length, uint32 map_flags, stduint requested_type) {
	if (!pb || !phys_begin || !want_length) return 0;
	const uint64 aligned_phys = uint64(phys_begin) & ~0xFFFull;
	const uint64 page_bias = uint64(phys_begin) - aligned_phys;
	if (uint64(want_length) > ~uint64(0) - page_bias - 0xFFFu) return 0;
	const uint64 map_size64 = (page_bias + uint64(want_length) + 0xFFFu) & ~0xFFFu;
	if (!map_size64 || (uint64)(stduint)aligned_phys != aligned_phys ||
		(uint64)(stduint)map_size64 != map_size64 ||
		map_size64 > uint64(~_IMM0) - aligned_phys) {
		return 0;
	}
	const stduint map_size = stduint(map_size64);
	const stduint page_flags =
		PGPROP_user_access |
		((map_flags & _IMM(PwcallDeviceMapFlag::Writable)) ? PGPROP_writable : 0);

	MutexLocal mapping_guard(&pb->mutex_mapping);
	if (&pb->paging == &kernel_paging) return 0;
	stduint vaddr;
	{
		SpinlockLocal guard(&pb->vma_lock);
		if (!pb->FindFreeVmaRange(map_size, vaddr)) return 0;
	}

	auto select_type = [&](stduint physical, stduint& type) -> bool {
		type = requested_type;
		if (type != PGTYPE_default) return true;
		pageint page = physical;
		PageEntry* entry = kernel_paging.getEntry(page);
		if (_IMM(entry) == ~_IMM0) {
			type = PGTYPE_device;
			return true;
		}
		if (_IMM(kernel_paging[physical]) != physical) return false;
		stduint properties;
		if (!entry->DecodeLeafProperties(page.crt_level, properties)) return false;
		switch (properties & PGPROP_MASK_TYPE) {
		case PGTYPE_writecombine: type = PGTYPE_writecombine; return true;
		case PGTYPE_device:
		case PGTYPE_uncacheable: type = PGTYPE_device; return true;
		default: return false;
		}
	};

	stduint type = PGTYPE_default;
	for (stduint off = 0; off < map_size; off += 0x1000) {
		stduint page_type;
		if (!select_type(stduint(aligned_phys) + off, page_type) ||
			(type != PGTYPE_default && type != page_type)) return 0;
		type = page_type;
	}
	VirtualMemoryArea vma(vaddr, vaddr + map_size, page_flags);
	vma.vm_type = VirtualMemoryAreaType::Device;
	{
		SpinlockLocal guard(&pb->vma_lock);
		if (!pb->InsertVmaSorted(vma)) return 0;
	}
	if (!Memory::MapPages(pb->paging, vaddr, stduint(aligned_phys),
		map_size, PAGESIZE_ANY, PGPROP_present | page_flags | type,
		&pb->vma_lock)) {
		SpinlockLocal guard(&pb->vma_lock);
		for (stduint i = 0; i < pb->vmas.Count(); i++) {
			if (pb->vmas[i].vm_start == vaddr &&
				pb->vmas[i].vm_type == VirtualMemoryAreaType::Device) {
				pb->vmas.Remove(i, 1);
				break;
			}
		}
		return 0;
	}
	{
		SpinlockLocal guard(&pb->vma_lock);
		pb->heaptop = maxof(pb->heaptop, vaddr + map_size);
	}
	return vaddr + stduint(page_bias);
}
#endif

static stdsint HandlePwcallDeviceDmaAlloc(ProcessBlock* pb, stduint dev_handle, stduint size, stduint flags) {
	if (!pb || !dev_handle || !size) return -1;
	const stduint known_flags = _IMM(PwcallDeviceDmaAllocFlag::NoLowMemory) |
		_IMM(PwcallDeviceDmaAllocFlag::Iova) |
		_IMM(PwcallDeviceDmaAllocFlag::IdentityIova);
	if (flags & ~known_flags) return -1;
	if ((flags & _IMM(PwcallDeviceDmaAllocFlag::IdentityIova)) &&
		!(flags & _IMM(PwcallDeviceDmaAllocFlag::Iova))) return -1;
	auto* device = ResolvePwcallDeviceHandle(pb, dev_handle);
	if (!device) return -1;
	uint8 dma_channel = 0xFF;
	#if (_MCCA & 0xFF00) == 0x8600
	if (DeviceNodeType(device->fields.node_type) != DeviceNodeType::PciDevice) {
		const auto* resource = Devsman::FindResource(device, DeviceResourceType::DmaChannel, 0);
		if (!resource || resource->length != 1 || resource->start > 7 || resource->start == 4 ||
			(resource->start <= 3 && resource->extra != 8) ||
			(resource->start >= 5 && resource->extra != 16)) return -1;
		dma_channel = uint8(resource->start);
	}
	#endif
	const stduint alloc_size = (size + 0xFFFu) & ~0xFFFu;
	void* phys = nullptr;
	#if (_MCCA & 0xFF00) == 0x8600
	if (dma_channel != 0xFF) {
		// The 8237 uses a physical 24-bit address; never substitute high memory or an IOVA.
		if (flags & (_IMM(PwcallDeviceDmaAllocFlag::NoLowMemory) |
			_IMM(PwcallDeviceDmaAllocFlag::Iova))) return -1;
		phys = DmaLowAlloc(alloc_size);
		if (!phys) return -1;
	}
	else if ((flags & _IMM(PwcallDeviceDmaAllocFlag::NoLowMemory)) == 0) {
		phys = DmaLowAlloc(alloc_size);
	}
	#endif
	if (!phys) {
		phys = mempool.allocate(alloc_size, 12);
	}
	if (!phys) return -1;

	#if (_MCCA & 0xFF00) == 0x8600
	Iova::DmaMapping dma_mapping = { uint64(stduint(phys)), false };
	const Iova::DmaMappingFlag iova_flags =
		(flags & _IMM(PwcallDeviceDmaAllocFlag::IdentityIova)) ?
		Iova::DmaMappingFlag::Identity : Iova::DmaMappingFlag::None;
	if ((flags & _IMM(PwcallDeviceDmaAllocFlag::Iova)) &&
		!Iova::MapDeviceDma(device, stduint(phys), alloc_size, iova_flags,
			dma_mapping)) {
		if (DmaLowIsInRange(phys)) DmaLowFree(phys, alloc_size);
		else mempool.deallocate(phys, alloc_size);
		return -1;
	}
	#endif

	stduint handle_id = 0;
	{
		SpinlockLocal guard(PwcallHandleLock());
		auto* owner = GetOrCreatePwcallProcessHandles(pb->pid);
		auto* slot = owner ? zalcof(PwcallDmaSlot) : nullptr;
		if (slot) {
			slot->handle_id = owner->next_dma_handle++;
			slot->device = device;
			slot->physical = stduint(phys);
			slot->size = alloc_size;
			slot->mapped_addr = 0;
			slot->dma_channel = dma_channel;
			#if (_MCCA & 0xFF00) == 0x8600
			slot->device_address = dma_mapping.device_address;
			slot->iova_mapped = dma_mapping.translated;
			#else
			slot->device_address = _IMM(phys);
			#endif
			DchainAppend(&owner->dmas, slot, false, nullptr);
			handle_id = slot->handle_id;
		}
	}
	if (handle_id) return stdsint(handle_id);
	bool release_physical = true;
	#if (_MCCA & 0xFF00) == 0x8600
	if (dma_mapping.translated &&
		!Iova::UnmapDeviceDma(device, dma_mapping.device_address,
			alloc_size)) {
		plogerro("[IOMMU] DMA handle allocation failed; retaining mapped physical memory");
		release_physical = false;
	}
	#endif
	if (!release_physical) return -1;
	#if (_MCCA & 0xFF00) == 0x8600
	if (DmaLowIsInRange(phys)) DmaLowFree(phys, alloc_size);
	else mempool.deallocate(phys, alloc_size);
	#else
	mempool.deallocate(phys, alloc_size);
	#endif
	return -1;
}

static stdsint HandlePwcallDeviceDmaFree(ProcessBlock* pb, stduint dma_handle) {
	if (!pb || dma_handle < PwcallDmaHandleBase) return -1;
	stduint physical;
	uint64 device_address;
	stduint size;
	stduint mapped_addr;
	DeviceNode* device;
	bool iova_mapped;
	{
		SpinlockLocal guard(PwcallHandleLock());
		auto* owner = FindPwcallProcessHandles(pb->pid);
		if (!owner) return -1;
		auto* slot_node = FindPwcallDmaSlotNodeByHandle(owner, dma_handle);
		if (!slot_node) return -1;
		auto* slot = reinterpret_cast<PwcallDmaSlot*>(slot_node->offs);
		if (!slot || !slot->physical || !slot->size) return -1;
		StopPwcallDmaSlot(slot);
		physical = slot->physical;
		device_address = slot->device_address;
		size = slot->size;
		mapped_addr = slot->mapped_addr;
		device = slot->device;
		iova_mapped = slot->iova_mapped;
		slot->physical = 0;
		slot->device_address = 0;
		slot->size = 0;
		slot->mapped_addr = 0;
		slot->iova_mapped = false;
	}
	if (mapped_addr && sysc_UMAP(mapped_addr, size) == -1) {
		SpinlockLocal guard(PwcallHandleLock());
		auto* owner = FindPwcallProcessHandles(pb->pid);
		auto* slot_node = owner ? FindPwcallDmaSlotNodeByHandle(owner, dma_handle) : nullptr;
		auto* slot = slot_node ? reinterpret_cast<PwcallDmaSlot*>(slot_node->offs) : nullptr;
		if (slot && !slot->physical) {
			slot->physical = physical;
			slot->device_address = device_address;
			slot->size = size;
			slot->mapped_addr = mapped_addr;
			slot->iova_mapped = iova_mapped;
		}
		return -1;
	}
	#if (_MCCA & 0xFF00) == 0x8600
	if (iova_mapped && !Iova::UnmapDeviceDma(device, device_address, size)) {
		SpinlockLocal guard(PwcallHandleLock());
		auto* owner = FindPwcallProcessHandles(pb->pid);
		auto* slot_node = owner ? FindPwcallDmaSlotNodeByHandle(owner, dma_handle) : nullptr;
		auto* slot = slot_node ? reinterpret_cast<PwcallDmaSlot*>(slot_node->offs) : nullptr;
		if (slot && !slot->physical) {
			slot->physical = physical;
			slot->device_address = device_address;
			slot->size = size;
			slot->iova_mapped = true;
		}
		return -1;
	}
	#endif
	{
		SpinlockLocal guard(PwcallHandleLock());
		auto* owner = FindPwcallProcessHandles(pb->pid);
		auto* slot_node = owner ? FindPwcallDmaSlotNodeByHandle(owner, dma_handle) : nullptr;
		if (slot_node) RemovePwcallHandleChainNode(&owner->dmas, slot_node);
	}
	if (physical && size) {
		#if (_MCCA & 0xFF00) == 0x8600
		if (DmaLowIsInRange((void*)physical)) {
			DmaLowFree((void*)physical, size);
		}
		else {
			mempool.deallocate((void*)physical, size);
		}
		#else
		mempool.deallocate((void*)physical, size);
		#endif
	}
	return 0;
}

static stdsint HandlePwcallDeviceDmaMap(ProcessBlock* pb, stduint dma_handle, stduint args, stduint flags) {
	(void)flags;
	if (!pb || dma_handle < PwcallDmaHandleBase || !args) return -1;
	PwcallDeviceDmaMapRequest request{};
	MccaMemCopyP(&request, nullptr, true, (void*)args, pb, false, sizeof(request));

	stduint physical = 0;
	uint64 device_address = 0;
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
		device_address = slot->device_address;
		size = slot->size;
		mapped_addr = slot->mapped_addr;
	}

	stduint duplicate_map = 0;
	bool map_valid = true;
	if (!mapped_addr) {
		#if CONFIG_ENABLE_MMU
		mapped_addr = MapPwcallPhysicalRange(pb, physical, size,
			request.map_flags ? request.map_flags : _IMM(PwcallDeviceMapFlag::Writable),
			PGTYPE_writeback);
		#else
		return -1;
		#endif
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
	request.device_address = device_address;
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
	if (res->length > ~uint64(0) - res->start) return -1;
	if (request.offset > res->length) return -1;

	const uint64 map_begin = res->start + request.offset;
	const uint64 max_length = res->length - request.offset;
	const uint64 want_length = request.length ? request.length : max_length;
	if (!want_length || want_length > max_length) return -1;
	if ((uint64)(stduint)map_begin != map_begin ||
		(uint64)(stduint)want_length != want_length) return -1;
	#if CONFIG_ENABLE_MMU
	const stduint mapped = MapPwcallPhysicalRange(pb, stduint(map_begin),
		stduint(want_length), request.map_flags, PGTYPE_default);
	return mapped ? stdsint(mapped) : -1;
	#else
	return -1;
	#endif
}

static stdsint HandlePwcallDeviceUnmap(ProcessBlock* pb, stduint addr, stduint len, stduint flags) {
	(void)flags;
	if (!pb || pb->ring != RING_S || !addr || !len) return -1;
	const uint64 end_addr = uint64(addr) + uint64(len);
	if (end_addr < addr || end_addr > ~uint64(0) - 0xFFFu) return -1;
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
	if (MccaMemCopyP(&request, nullptr, true, (void*)args, pb, false,
		sizeof(request)) != sizeof(request)) return -1;

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

static stdsint HandlePwcallDeviceIoStream(ProcessBlock* pb, DeviceNode* node,
	stduint args, bool is_write) {
	if (!pb || !node || !args) return -1;
	PwcallDeviceIoStreamRequest request{};
	if (MccaMemCopyP(&request, nullptr, true, (void*)args, pb, false,
		sizeof(request)) != sizeof(request)) return -1;

	#if (_MCCA & 0xFF00) == 0x8600
	const uint32 resource_type = request.resource_type ? request.resource_type :
		_IMM(DeviceResourceType::PciBarIo);
	const auto* res = FindPwcallDeviceResource(node, resource_type,
		request.resource_index);
	if (!res) {
		res = FindPwcallDeviceResource(node,
			_IMM(DeviceResourceType::IoPortRange), request.resource_index);
	}
	if (!res) return -1;
	const auto res_type = DeviceResourceType(res->type);
	if (res_type != DeviceResourceType::PciBarIo &&
		res_type != DeviceResourceType::IoPortRange) return -1;
	if (request.width != 1 && request.width != 2 && request.width != 4) return -1;
	if (request.offset + request.width > res->length || !request.buffer ||
		!request.count || request.count > 0x100000u ||
		uint64(stduint(request.buffer)) != request.buffer ||
		request.count > (~uint64(0) / request.width)) return -1;
	const uint64 port64 = res->start + request.offset;
	if (port64 + request.width > 0x10000ull) return -1;
	const uint16 port = uint16(port64);
	const uint64 byte_count64 = request.count * request.width;
	if (uint64(stduint(byte_count64)) != byte_count64) return -1;
	const stduint byte_count = stduint(byte_count64);
	uint8 transfer[256];
	stduint transferred = 0;
	while (transferred < byte_count) {
		const stduint chunk = minof(byte_count - transferred,
			stduint(sizeof(transfer)));
		if (is_write && MccaMemCopyP(transfer, nullptr, true,
			reinterpret_cast<const void*>(stduint(request.buffer) + transferred),
			pb, false, chunk) != chunk) return -1;
		for (stduint offset = 0; offset < chunk; offset += request.width) {
			if (is_write) {
				switch (request.width) {
				case 1: OUT_b(port, transfer[offset]); break;
				case 2: OUT_w(port,
					*reinterpret_cast<uint16*>(transfer + offset)); break;
				case 4: OUT_d(port,
					*reinterpret_cast<uint32*>(transfer + offset)); break;
				default: return -1;
				}
			}
			else {
				switch (request.width) {
				case 1: transfer[offset] = IN_b(port); break;
				case 2: *reinterpret_cast<uint16*>(transfer + offset) =
					IN_w(port); break;
				case 4: *reinterpret_cast<uint32*>(transfer + offset) =
					IN_d(port); break;
				default: return -1;
				}
			}
		}
		if (!is_write && MccaMemCopyP(
			reinterpret_cast<void*>(stduint(request.buffer) + transferred),
			pb, false, transfer, nullptr, true, chunk) != chunk) return -1;
		transferred += chunk;
	}
	return 0;
	#else
	(void)is_write;
	return -1;
	#endif
}

void CleanupPwcallProcessHandles(stduint pid) {
	#if (_MCCA & 0xFF00) == 0x8600
	PwcallUnbindProcessInterrupts(pid);
	stduint device_count = 0;
	{
		SpinlockLocal guard(PwcallHandleLock());
		EnsurePwcallHandleTableInitialized();
		auto* owner = FindPwcallProcessHandles(pid);
		if (owner) device_count = owner->handles.node_count + owner->dmas.node_count;
	}
	for0(device_index, device_count) {
		DeviceNode* device = nullptr;
		{
			SpinlockLocal guard(PwcallHandleLock());
			auto* owner = FindPwcallProcessHandles(pid);
			stduint index = 0;
			for (auto* crt = owner ? owner->handles.root_node : nullptr; crt; crt = crt->next) {
				auto* slot = reinterpret_cast<PwcallHandleSlot*>(crt->offs);
				if (index++ == device_index) {
					device = slot ? slot->entry.node : nullptr;
					break;
				}
			}
			if (!device) {
				for (auto* crt = owner ? owner->dmas.root_node : nullptr; crt; crt = crt->next) {
					auto* slot = reinterpret_cast<PwcallDmaSlot*>(crt->offs);
					if (index++ == device_index) {
						device = slot ? slot->device : nullptr;
						break;
					}
				}
			}
		}
		PwcallDisablePciDeviceDma(device, pid);
	}
	#endif
	Dnode* owner_node = nullptr;
	{
		SpinlockLocal guard(PwcallHandleLock());
		EnsurePwcallHandleTableInitialized();
		for (auto* crt = power_handle_table.root_node; crt; crt = crt->next) {
			auto* owner = reinterpret_cast<PwcallProcessHandles*>(crt->offs);
			if (owner && owner->pid == pid) {
				DetachPwcallHandleChainNode(&power_handle_table, crt);
				owner_node = crt;
				break;
			}
		}
	}
	if (owner_node) DnodeRemove(owner_node, power_handle_table.func_free);
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
		auto* node = ResolvePwcallDevice(pb, p1, p2, p3);
		if (!node) {
			if (!p1 && !p2) plogwarn("[Devsman] DevOpen: no owned node pid=%u", pb->pid);
			return -1;
		}
		#if (_MCCA & 0xFF00) == 0x8600
		PwcallEnablePciDeviceAccess(node);
		#endif
		const stduint handle = AllocPwcallDeviceHandle(pb, node, uint32(p3));
		if (!handle) {
			if (!p1 && !p2) plogwarn("[Devsman] DevOpen: handle allocation failed pid=%u", pb->pid);
			return -1;
		}
		#if (_MCCA & 0xFF00) == 0x8600
		if ((uint32(p3) & _IMM(PwcallDeviceOpenFlag::Interrupt)) &&
			!device_event_prepare(Taskman::CurrentTB())) {
			if (!p1 && !p2) plogwarn("[Devsman] DevOpen: event queue failed pid=%u", pb->pid);
			(void)ClosePwcallDeviceHandle(pb, handle);
			return -1;
		}
		#endif
		return (stdsint)handle;
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
	case syscall_t::POWERCALL_DEV_IO_READ_STREAM:
	{
		auto* node = ResolvePwcallDeviceHandle(pb, p1);
		return HandlePwcallDeviceIoStream(pb, node, p2, false);
	}
	case syscall_t::POWERCALL_DEV_IO_WRITE_STREAM:
	{
		auto* node = ResolvePwcallDeviceHandle(pb, p1);
		return HandlePwcallDeviceIoStream(pb, node, p2, true);
	}
	case syscall_t::POWERCALL_DEV_READ:
	case syscall_t::POWERCALL_DEV_WRITE:
	case syscall_t::POWERCALL_DEV_WAIT:
		return -1;
	case syscall_t::POWERCALL_DEV_ACK:
		#if (_MCCA & 0xFF00) == 0x8600
		return PwcallAckDeviceInterrupt(pb, p1, p2, uint32(p3));
		#else
		return -1;
		#endif
	case syscall_t::POWERCALL_DEV_TIMER:
		#if (_MCCA & 0xFF00) == 0x8600
		if (!ResolvePwcallDeviceHandle(pb, p1) || !p2) return -1;
		return Systimex::AppendDriverMessage(p2, Taskman::CurrentTID(),
			_IMM(KernelMsg::DeviceTimeout), p3) ? 0 : -1;
		#else
		return -1;
		#endif
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
		case PwcallDevicePublishCommand::Started: {
			if (!node->fields.binding.driver_name) return -1;
			if (node->fields.binding.owner_pid && node->fields.binding.owner_pid != uint32(pb->pid)) return -1;
			#if (_MCCA & 0xFF00) == 0x8600
			const uint32 open_flags = ResolvePwcallDeviceHandleFlags(pb, p1);
			if (open_flags & _IMM(PwcallDeviceOpenFlag::Interrupt)) {
				if (!PwcallBindDeviceInterrupt(pb, p1, node, Taskman::CurrentTID(),
					(open_flags & _IMM(PwcallDeviceOpenFlag::MsiOnly)) != 0,
					(open_flags & _IMM(PwcallDeviceOpenFlag::MsixPreferred)) != 0)) return -1;
			}
			#endif
			node->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
			node->fields.binding.probe_result = 0;
			node->fields.binding.owner_pid = uint32(pb->pid);
			return 0;
		}
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
