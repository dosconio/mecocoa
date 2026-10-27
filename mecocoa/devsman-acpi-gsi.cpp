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
	constexpr stduint ACPIGSIRouteCapacity = 16;
	constexpr stduint ACPIGSIBindingCapacity = 32;
	constexpr uint64 IOAPICRouteConfigurationMask = uint64(0xFFFu | (1u << 13) | (1u << 15) | (1u << 16)) | (uint64(0xFFu) << 56);

	struct ACPIGSIRoute {
		ACPIGSIConfiguration configuration;
		uint64 original_route;
		uint16 reference_count;
		bool active;
		bool retiring;
	};

	struct ACPIGSIBindingSlot {
		ACPIGSIFilter filter;
		void* context;
		uni::Atomic<uint32> dispatching;
		uni::Atomic<uint8> active;
		uint32 generation;
		uint16 route_slot;
	};

	ACPIGSIRoute acpi_GSI_routes[ACPIGSIRouteCapacity]{};
	ACPIGSIBindingSlot acpi_GSI_bindings[ACPIGSIBindingCapacity]{};
	Spinlock acpi_GSI_lock;
	uint32 acpi_GSI_generation = 0;
	bool acpi_GSI_vector_ready = false;

	bool GetIOAPICDestination(uint8& destination) {
		if (!acpi_cpu_count || acpi_cpu_lapic_ids[0] >= 0xFFu) return false;
		destination = uint8(acpi_cpu_lapic_ids[0]);
		return true;
	}

	bool ReadIOAPICRoute(uint32 gsi, uint64& route) {
		if (IC.getType() == 0) return false;
		#if _MCCA == 0x8664
		return Devs::x64_ioapic_read_route(gsi, route);
		#else
		if (gsi >= 24) return false;
		route = IC.IO_Read64(byte(0x10u + gsi * 2u));
		return true;
		#endif
	}

	bool WriteIOAPICRoute(uint32 gsi, uint64 route) {
		if (IC.getType() == 0) return false;
		#if _MCCA == 0x8664
		return Devs::x64_ioapic_write_route(gsi, route);
		#else
		if (gsi >= 24) return false;
		const byte index = byte(0x10u + gsi * 2u);
		IC.IO_Writ32(index, uint32(route) | uint32(1u << 16));
		IC.IO_Writ32(byte(index + 1u), uint32(route >> 32));
		IC.IO_Writ32(index, uint32(route));
		return true;
		#endif
	}

	uint64 ConfigureIOAPICRoute(uint64 route, const ACPIGSIConfiguration& configuration, uint8 destination, bool masked) {
		// Intel 82093AA I/O APIC, section 3.2.4: vector, polarity, trigger, mask and destination fields.
		route &= ~IOAPICRouteConfigurationMask;
		route |= uint64(ACPIGSIVector) | (uint64(destination) << 56);
		if (configuration.active_low) route |= uint64(1) << 13;
		if (configuration.level_triggered) route |= uint64(1) << 15;
		if (masked) route |= uint64(1) << 16;
		return route;
	}

	bool WriteVerifiedIOAPICRoute(uint32 gsi, uint64 route) {
		if (!WriteIOAPICRoute(gsi, route)) return false;
		uint64 readback = 0;
		return ReadIOAPICRoute(gsi, readback) && (readback & IOAPICRouteConfigurationMask) == (route & IOAPICRouteConfigurationMask);
	}

	bool SameGSIConfiguration(const ACPIGSIConfiguration& left, const ACPIGSIConfiguration& right) {
		return left.gsi == right.gsi && left.level_triggered == right.level_triggered && left.active_low == right.active_low;
	}

	bool ShareableGSIConfiguration(const ACPIGSIConfiguration& existing, const ACPIGSIConfiguration& requested) {
		return SameGSIConfiguration(existing, requested) && existing.shareable && requested.shareable;
	}

	bool DispatchACPIGSIBinding(ACPIGSIBindingSlot& slot) {
		if (slot.active.load(uni::MemoryOrder_Acquire) != 1) return false;
		slot.dispatching.fetch_add(1, uni::MemoryOrder_Acq_Rel);
		if (slot.active.load(uni::MemoryOrder_Acquire) != 1) {
			slot.dispatching.fetch_sub(1, uni::MemoryOrder_Acq_Rel);
			return false;
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

	ACPIGSIRoute* FindGSI(uint32 gsi, stduint& slot) {
		for0(index, ACPIGSIRouteCapacity) {
			if (!acpi_GSI_routes[index].active || acpi_GSI_routes[index].configuration.gsi != gsi) continue;
			slot = index;
			return &acpi_GSI_routes[index];
		}
		return nullptr;
	}

	ACPIGSIRoute* AllocateGSIRoute(stduint& slot) {
		for0(index, ACPIGSIRouteCapacity) {
			if (acpi_GSI_routes[index].active) continue;
			slot = index;
			return &acpi_GSI_routes[index];
		}
		return nullptr;
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
	uint8 destination = 0;
	if (!GetIOAPICDestination(destination)) return ACPIGSIStatus::Unsupported;
	uint64 original_route = 0;
	if (!ReadIOAPICRoute(configuration.gsi, original_route)) return ACPIGSIStatus::GSIUnavailable;

	SpinlockLocal guard(&acpi_GSI_lock);
	stduint route_slot = 0;
	auto* route = FindGSI(configuration.gsi, route_slot);
	if (route) {
		if (route->retiring || !ShareableGSIConfiguration(route->configuration, configuration)) return ACPIGSIStatus::ConfigurationConflict;
	}
	else {
		route = AllocateGSIRoute(route_slot);
		if (!route) return ACPIGSIStatus::ResourceUnavailable;
		*route = {};
		route->configuration = configuration;
		route->original_route = original_route;
		route->active = true;
		if (!WriteVerifiedIOAPICRoute(configuration.gsi, ConfigureIOAPICRoute(original_route, configuration, destination, true))) {
			(void)WriteIOAPICRoute(configuration.gsi, original_route);
			*route = {};
			return ACPIGSIStatus::RouteFailure;
		}
	}

	stduint binding_slot = 0;
	auto* slot = AllocateGSIBinding(binding_slot);
	if (!slot) {
		if (!route->reference_count) {
			(void)WriteIOAPICRoute(configuration.gsi, route->original_route);
			*route = {};
		}
		return ACPIGSIStatus::ResourceUnavailable;
	}
	PrepareACPIGSIVector();
	slot->filter = filter;
	slot->context = context;
	slot->generation = ++acpi_GSI_generation;
	if (!slot->generation) slot->generation = ++acpi_GSI_generation;
	slot->route_slot = uint16(route_slot);
	slot->active.store(1, uni::MemoryOrder_Release);
	++route->reference_count;
	if (!WriteVerifiedIOAPICRoute(configuration.gsi, ConfigureIOAPICRoute(route->original_route, route->configuration, destination, false))) {
		slot->active.store(0, uni::MemoryOrder_Release);
		--route->reference_count;
		slot->filter = nullptr;
		slot->context = nullptr;
		if (!route->reference_count) {
			(void)WriteIOAPICRoute(configuration.gsi, route->original_route);
			*route = {};
		}
		return ACPIGSIStatus::RouteFailure;
	}
	binding.gsi = configuration.gsi;
	binding.generation = slot->generation;
	binding.slot = uint16(binding_slot);
	binding.vector = ACPIGSIVector;
	binding.active = true;
	return ACPIGSIStatus::Success;
}

ACPIGSIStatus Devs::UnbindACPIGSI(ACPIGSIBinding& binding) {
	if (!binding.active || binding.slot >= ACPIGSIBindingCapacity) return ACPIGSIStatus::InvalidArgument;
	auto& slot = acpi_GSI_bindings[binding.slot];
	uint16 route_slot = 0;
	bool retire_route = false;
	{
		SpinlockLocal guard(&acpi_GSI_lock);
		if (slot.active.load(uni::MemoryOrder_Acquire) != 1 || slot.generation != binding.generation || slot.route_slot >= ACPIGSIRouteCapacity) return ACPIGSIStatus::OwnershipMismatch;
		route_slot = slot.route_slot;
		auto& route = acpi_GSI_routes[route_slot];
		slot.active.store(2, uni::MemoryOrder_Release);
		if (route.reference_count) --route.reference_count;
		retire_route = !route.reference_count;
		if (retire_route) {
			route.retiring = true;
			uint8 destination = 0;
			if (GetIOAPICDestination(destination)) (void)WriteIOAPICRoute(route.configuration.gsi, ConfigureIOAPICRoute(route.original_route, route.configuration, destination, true));
		}
	}
	while (slot.dispatching.load(uni::MemoryOrder_Acquire)) asm volatile("pause" ::: "memory");
	{
		SpinlockLocal guard(&acpi_GSI_lock);
		slot.filter = nullptr;
		slot.context = nullptr;
		slot.generation = 0;
		slot.route_slot = 0;
		slot.active.store(0, uni::MemoryOrder_Release);
		if (retire_route) {
			auto& route = acpi_GSI_routes[route_slot];
			(void)WriteIOAPICRoute(route.configuration.gsi, route.original_route);
			route = {};
		}
	}
	binding = {};
	return ACPIGSIStatus::Success;
}

#if CONFIG_ACPI_BOOT_TEST
void Devs::RunACPIGSITest() {
	const ACPIGSIConfiguration level_low_shared{19u, true, true, true};
	const ACPIGSIConfiguration same{19u, true, true, true};
	const ACPIGSIConfiguration conflict{19u, false, true, true};
	const uint64 route = ConfigureIOAPICRoute((uint64(0xAAu) << 56) | 0xF00u, level_low_shared, 0x2Au, false);
	const bool route_passed = uint8(route) == ACPIGSIVector && !(route & 0xF00u) && (route & (uint64(1) << 13)) && (route & (uint64(1) << 15)) && !(route & (uint64(1) << 16)) && uint8(route >> 56) == 0x2Au;
	const bool shared_passed = ShareableGSIConfiguration(level_low_shared, same);
	const bool conflict_passed = !ShareableGSIConfiguration(level_low_shared, conflict);
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
