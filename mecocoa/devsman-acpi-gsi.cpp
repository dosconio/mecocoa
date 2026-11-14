// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: ACPI Global System Interrupt Routing
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include "devsman-acpi-gsi.h"

#if defined(_MCCA) && ((_MCCA & 0xFF00) == 0x8600)

extern "C" void Handint_ACPI_GSI_Entry();
extern "C" void register_interrupt_vector_handler(stduint irq_id, void (*handler)(stduint));

namespace {
	constexpr uint8 ACPIGSIVector = 0xA0;
	constexpr stduint ACPIGSIBindingCapacity = 32;

	struct ACPIGSIBindingSlot {
		ACPIGSIFilter filter;
		void* context;
		uni::Atomic<uint32> dispatching;
		uni::Atomic<uint8> active;
		uint32 generation;
		uint32 gsi;
	};

	ACPIGSIBindingSlot acpi_GSI_bindings[ACPIGSIBindingCapacity]{};
	uint32 acpi_GSI_generation = 0;
	bool acpi_GSI_vector_ready = false;

	bool GetIOAPICDestination(uint32& destination) {
		if (!acpi_cpu_count || acpi_cpu_lapic_ids[0] == CORE_ID_INVALID) return false;
		destination = acpi_cpu_lapic_ids[0];
		#if _MCCA == 0x8664
		if (Iova::ExtendedInterruptMode()) return true;
		#endif
		return destination < 0xFFu;
	}

	bool DispatchACPIGSIBinding(ACPIGSIBindingSlot& slot) {
		if (slot.active.load(uni::MemoryOrder_Acquire) != 1) return false;
		{
			// Claim the callback under the same lock used to retire its slot.
			SpinlockLocal guard(Devs::x86_ioapic_route_lock());
			if (slot.active.load(uni::MemoryOrder_Acquire) != 1) return false;
			slot.dispatching.fetch_add(1, uni::MemoryOrder_Acq_Rel);
		}
		const bool claimed = slot.filter && slot.filter(slot.context);
		slot.dispatching.fetch_sub(1, uni::MemoryOrder_Acq_Rel);
		return claimed;
	}

	void HandleACPIGSIInterrupt(stduint vector) {
		for0(index, ACPIGSIBindingCapacity) (void)DispatchACPIGSIBinding(acpi_GSI_bindings[index]);
		IC.SendEOI(vector);
	}

	void PrepareACPIGSIVector() {
		if (acpi_GSI_vector_ready) return;
		#if _MCCA == 0x8664
		IC[ACPIGSIVector].setModeRupt(mglb(Handint_ACPI_GSI_Entry), SegCo64);
		#else
		IC[ACPIGSIVector].setRange(mglb(Handint_ACPI_GSI_Entry), SegCo32);
		#endif
		register_interrupt_vector_handler(ACPIGSIVector, HandleACPIGSIInterrupt);
		acpi_GSI_vector_ready = true;
	}

	ACPIGSIBindingSlot* AllocateGSIBinding(stduint& slot) {
		for0(index, ACPIGSIBindingCapacity) {
			if (acpi_GSI_bindings[index].active.load(uni::MemoryOrder_Acquire) != 0 || acpi_GSI_bindings[index].dispatching.load(uni::MemoryOrder_Acquire)) continue;
			slot = index;
			return &acpi_GSI_bindings[index];
		}
		return nullptr;
	}

	#if CONFIG_ACPI_BOOT_TEST
	struct ACPIGSITestContext {
		uint32 count;
		bool claimed;
	};

	bool ACPIGSITestFilter(void* context) {
		auto* test = static_cast<ACPIGSITestContext*>(context);
		if (!test) return false;
		++test->count;
		return test->claimed;
	}
	#endif
}

ACPIGSIStatus Devs::BindACPIGSI(const ACPIGSIConfiguration& configuration, ACPIGSIFilter filter, void* context, ACPIGSIBinding& binding) {
	if (!filter || !context || binding.active) return ACPIGSIStatus::InvalidArgument;
	if (IC.getType() == 0) return ACPIGSIStatus::Unsupported;
	uint32 destination = 0;
	if (!GetIOAPICDestination(destination)) return ACPIGSIStatus::Unsupported;
	SpinlockLocal guard(x86_ioapic_route_lock());
	stduint binding_slot = 0;
	auto* slot = AllocateGSIBinding(binding_slot);
	if (!slot) return ACPIGSIStatus::ResourceUnavailable;
	X86IoApicRoute route{configuration.gsi, destination, ACPIGSIVector, configuration.active_low,
		configuration.level_triggered, true, configuration.shareable};
	uint64 entry;
	switch (x86_ioapic_acquire_route_locked(X86IoApicOwner::Acpi, route, false, entry)) {
		case X86IoApicStatus::Unavailable: return ACPIGSIStatus::GSIUnavailable;
		case X86IoApicStatus::Conflict: return ACPIGSIStatus::ConfigurationConflict;
		case X86IoApicStatus::Capacity: return ACPIGSIStatus::ResourceUnavailable;
		case X86IoApicStatus::RouteFailure: return ACPIGSIStatus::RouteFailure;
		case X86IoApicStatus::Success: break;
	}
	PrepareACPIGSIVector();
	slot->filter = filter;
	slot->context = context;
	slot->generation = ++acpi_GSI_generation;
	if (!slot->generation) slot->generation = ++acpi_GSI_generation;
	slot->gsi = configuration.gsi;
	slot->active.store(1, uni::MemoryOrder_Release);
	if (!x86_ioapic_set_owned_mask_locked(X86IoApicOwner::Acpi, configuration.gsi, false)) {
		slot->active.store(0, uni::MemoryOrder_Release);
		slot->filter = nullptr;
		slot->context = nullptr;
		x86_ioapic_release_route_locked(X86IoApicOwner::Acpi, configuration.gsi);
		x86_ioapic_finish_acpi_release_locked(configuration.gsi);
		return ACPIGSIStatus::RouteFailure;
	}
	binding.gsi = configuration.gsi;
	binding.generation = slot->generation;
	binding.slot = uint16(binding_slot);
	binding.vector = ACPIGSIVector;
	binding.active = true;
	return ACPIGSIStatus::Success;
}

ACPIGSIStatus Devs::BindISAGSI(uint8 isa_irq, ACPIGSIFilter filter, void* context, ACPIGSIBinding& binding) {
	if (isa_irq >= 16 || !filter || !context || binding.active) return ACPIGSIStatus::InvalidArgument;
	uint32 gsi = 0;
	bool level_triggered = false;
	bool active_low = false;
	// ACPI MADT Interrupt Source Overrides may remap ISA IRQs and change their polarity or trigger mode.
	if (!x86_resolve_isa_irq(isa_irq, gsi, level_triggered, active_low)) return ACPIGSIStatus::GSIUnavailable;
	const ACPIGSIConfiguration configuration{gsi, level_triggered, active_low, false};
	return BindACPIGSI(configuration, filter, context, binding);
}

ACPIGSIStatus Devs::UnbindACPIGSI(ACPIGSIBinding& binding) {
	if (!binding.active || binding.slot >= ACPIGSIBindingCapacity) return ACPIGSIStatus::InvalidArgument;
	auto& slot = acpi_GSI_bindings[binding.slot];
	{
		SpinlockLocal guard(x86_ioapic_route_lock());
		if (slot.active.load(uni::MemoryOrder_Acquire) != 1 || slot.generation != binding.generation ||
			slot.gsi != binding.gsi) return ACPIGSIStatus::OwnershipMismatch;
		slot.active.store(2, uni::MemoryOrder_Release);
		x86_ioapic_release_route_locked(X86IoApicOwner::Acpi, binding.gsi);
	}
	while (slot.dispatching.load(uni::MemoryOrder_Acquire)) asm volatile("pause" ::: "memory");
	{
		SpinlockLocal guard(x86_ioapic_route_lock());
		slot.filter = nullptr;
		slot.context = nullptr;
		slot.generation = 0;
		slot.gsi = 0;
		slot.active.store(0, uni::MemoryOrder_Release);
		x86_ioapic_finish_acpi_release_locked(binding.gsi);
	}
	binding = {};
	return ACPIGSIStatus::Success;
}

#if CONFIG_ACPI_BOOT_TEST
void Devs::RunACPIGSITest() {
	const X86IoApicRoute level_low_shared{19u, 0x2Au, ACPIGSIVector, true, true, false, true};
	auto conflict = level_low_shared;
	conflict.level_triggered = false;
	const uint64 route = x86_ioapic_encode_compatible_route(level_low_shared, (uint64(0xAAu) << 56) | 0xF00u, true);
	const bool route_passed = uint8(route) == ACPIGSIVector && !(route & 0xF00u) && (route & (uint64(1) << 13)) && (route & (uint64(1) << 15)) && !(route & (uint64(1) << 16)) && uint8(route >> 56) == 0x2Au;
	const bool shared_passed = x86_ioapic_routes_shareable(level_low_shared, level_low_shared);
	const bool conflict_passed = !x86_ioapic_routes_shareable(level_low_shared, conflict);
	ACPIGSITestContext claimed_context{0, true};
	ACPIGSITestContext spurious_context{0, false};
	ACPIGSIBindingSlot claimed_slot{};
	claimed_slot.filter = ACPIGSITestFilter;
	claimed_slot.context = &claimed_context;
	claimed_slot.active.store(1, uni::MemoryOrder_Release);
	ACPIGSIBindingSlot spurious_slot{};
	spurious_slot.filter = ACPIGSITestFilter;
	spurious_slot.context = &spurious_context;
	spurious_slot.active.store(1, uni::MemoryOrder_Release);
	const bool dispatch_passed = DispatchACPIGSIBinding(claimed_slot) && claimed_context.count == 1;
	const bool spurious_passed = !DispatchACPIGSIBinding(spurious_slot) && spurious_context.count == 1;
	claimed_slot.active.store(0, uni::MemoryOrder_Release);
	const bool release_passed = !DispatchACPIGSIBinding(claimed_slot) && claimed_context.count == 1;
	const bool passed = route_passed && shared_passed && conflict_passed && dispatch_passed && spurious_passed && release_passed;
	MCCA_BOOT_TEST_LOG(passed, "[ACPI-GSI-TEST] route=%s shared=%s conflict=%s dispatch=%s spurious=%s release=%s", route_passed ? "success" : "failed", shared_passed ? "success" : "failed", conflict_passed ? "success" : "failed", dispatch_passed ? "success" : "failed", spurious_passed ? "success" : "failed", release_passed ? "success" : "failed");
}
#endif

#endif
