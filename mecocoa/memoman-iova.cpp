// ASCII g++ TAB4 LF
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include "../include/handler.hpp"

#include <cpp/Device/ACPI.hpp>
#include <cpp/Device/Bus/PCI.hpp>
#include <cpp/ISO_IEC_STD/memory>
#include <c/bitmap.h>

#if (_MCCA & 0xFF00) == 0x8600

namespace {
	static constexpr uint16 kDmarHardwareUnit = 0;
	static constexpr uint16 kDmarReservedMemory = 1;
	static constexpr uint8 kDmarIncludeAll = 1;
	static constexpr uint8 kDmarScopePciEndpoint = 1;
	static constexpr uint8 kDmarScopePciBridge = 2;
	static constexpr uint8 kDmarScopeIoApic = 3;

	static constexpr stduint kVtdVersionRegister = 0x00;
	static constexpr stduint kVtdCapabilityRegister = 0x08;
	static constexpr stduint kVtdExtendedCapabilityRegister = 0x10;
	static constexpr stduint VtdGlobalCommandRegister = 0x18;
	static constexpr stduint kVtdGlobalStatusRegister = 0x1C;
	static constexpr stduint kVtdRootTableAddressRegister = 0x20;
	static constexpr stduint VtdContextCommandRegister = 0x28;
	// Intel VT-d specification, section 11.4.7: fault reporting interface.
	static constexpr stduint kVtdFaultStatusRegister = 0x34;
	static constexpr stduint VtdFaultEventControlRegister = 0x38;
	static constexpr stduint VtdFaultEventDataRegister = 0x3C;
	static constexpr stduint VtdFaultEventAddressRegister = 0x40;
	static constexpr stduint VtdFaultEventUpperAddressRegister = 0x44;
	static constexpr uint32 VtdPrimaryFaultOverflow = 1u << 0;
	static constexpr uint32 VtdPrimaryFaultPending = 1u << 1;
	static constexpr uint32 VtdFaultEventMask = 1u << 31;
	static constexpr uint32 VtdFaultRecordValid = 1u << 31;
	static constexpr uint64 VtdExtendedInterruptModeSupported = uint64(1) << 4;
	static constexpr uint64 VtdExtendedInterruptModeRequired = uint64(1) << 61;
	static constexpr uint16 VtdFaultRecordSize = 16;
	static constexpr uint16 VtdFaultCursorUnset = 0xFFFF;
	static constexpr uint8 VtdFaultIrqBudget = 4;
	static constexpr uint8 VtdFaultReportLimit = 4;
	static constexpr stduint VtdInvalidationQueueHeadRegister = 0x80;
	static constexpr stduint VtdInvalidationQueueTailRegister = 0x88;
	static constexpr stduint VtdInvalidationQueueAddressRegister = 0x90;
	static constexpr stduint VtdInterruptRemappingTableAddressRegister = 0xB8;
	static constexpr stduint kVtdRegisterWindowLength = 0xC0;
	static constexpr uint64 VtdPageAddressMask = ~uint64(PAGE_SIZE - 1);
	static constexpr uint64 VtdContextCommandInvalidate = uint64(1) << 63;
	static constexpr uint64 VtdContextCommandGlobal = uint64(1) << 61;
	static constexpr uint64 VtdIotlbCommandInvalidate = uint64(1) << 63;
	static constexpr uint64 VtdIotlbCommandGlobal = uint64(1) << 60;
	static constexpr uint64 VtdIotlbReadDrain = uint64(1) << 49;
	static constexpr uint64 VtdIotlbWriteDrain = uint64(1) << 48;
	static constexpr uint32 VtdGlobalCommandEnableTranslation = uint32(1) << 31;
	static constexpr uint32 VtdGlobalCommandSetRoot = uint32(1) << 30;
	static constexpr uint32 VtdGlobalCommandEnableQueuedInvalidation = uint32(1) << 26;
	static constexpr uint32 VtdGlobalCommandEnableInterruptRemapping = uint32(1) << 25;
	static constexpr uint32 VtdGlobalCommandSetInterruptRemappingTable = uint32(1) << 24;
	static constexpr uint32 VtdGlobalCommandCompatibilityInterrupts = uint32(1) << 23;
	static constexpr uint32 VtdGlobalStatusTranslationEnabled = uint32(1) << 31;
	static constexpr uint32 VtdGlobalStatusRootSet = uint32(1) << 30;
	static constexpr uint32 VtdGlobalStatusQueuedInvalidation = uint32(1) << 26;
	static constexpr uint32 VtdGlobalStatusInterruptRemapping = uint32(1) << 25;
	static constexpr uint32 VtdGlobalStatusInterruptRemappingTableSet = uint32(1) << 24;
	static constexpr uint32 VtdGlobalStatusCompatibilityInterrupts = uint32(1) << 23;
	static constexpr uint32 VtdInvalidationQueueFaults =
		(uint32(1) << 4) | (uint32(1) << 5) | (uint32(1) << 6);
	static constexpr uint32 VtdFaultEventStatus =
		VtdPrimaryFaultPending | VtdPrimaryFaultOverflow | VtdInvalidationQueueFaults;
	static constexpr uint64 VtdInterruptRemappingExtendedMode = uint64(1) << 11;
	static constexpr uint64 VtdInterruptCacheInvalidationType = 4;
	static constexpr uint64 VtdInterruptCacheInvalidationSelective = uint64(1) << 4;
	static constexpr uint64 VtdInvalidationWaitType = 5;
	static constexpr uint64 VtdInvalidationWaitStatusWrite = uint64(1) << 5;
	static constexpr uint32 VtdInvalidationStatusPending = 1;
	static constexpr uint32 VtdInvalidationStatusComplete = 2;
	static constexpr uint64 VtdRootPresent = 1;
	static constexpr uint64 VtdContextPresent = 1;
	static constexpr uint64 VtdContextPassthrough = uint64(2) << 2;
	static constexpr uint64 VtdPageRead = 1;
	static constexpr uint64 VtdPageWrite = 1 << 1;
	static constexpr uint8 VtdAddressWidth48 = 2;
	static constexpr uint16 VtdPassthroughDomain = 1;
	static constexpr uint16 VtdFirstTranslatedDomain = 2;
	static constexpr stduint VtdCommandTimeoutSpins = 1000000;
	static constexpr uint64 IovaStartAddress = 0x00100000;
	static constexpr stduint IovaDomainCapacity = 8;
	static constexpr stduint IovaMappingCapacity = 48;
	static constexpr stduint IovaTableCapacity = 64;
	static constexpr uint8 VtdMinimumInterruptTableSize = 7;
	// A uint16 handle stores index + 1; the next hardware size would overflow it.
	static constexpr uint8 VtdMaximumInterruptTableSize = 14;
	static constexpr stduint VtdInvalidationQueueLength = 256;

	_PACKED(struct) DmarTable {
		uni::ACPI::DescriptionHeader header;
		uint8 host_address_width;
		uint8 flags;
		uint8 reserved[10];
	};

	_PACKED(struct) DmarEntryHeader {
		uint16 type;
		uint16 length;
	};

	_PACKED(struct) DmarHardwareUnit {
		DmarEntryHeader header;
		uint8 flags;
		uint8 size;
		uint16 segment;
		uint64 register_base;
	};

	_PACKED(struct) DmarReservedMemory {
		DmarEntryHeader header;
		uint16 reserved;
		uint16 segment;
		uint64 base_address;
		uint64 end_address;
	};

	_PACKED(struct) DmarDeviceScope {
		uint8 type;
		uint8 length;
		uint8 flags;
		uint8 reserved;
		uint8 enumeration_id;
		uint8 start_bus;
	};

	_PACKED(struct) DmarPciPath {
		uint8 device;
		uint8 function;
	};

	struct VtdRootEntry {
		uint64 low;
		uint64 high;
	};

	struct VtdContextEntry {
		uint64 low;
		uint64 high;
	};

	struct VtdInterruptRemappingEntry {
		uint64 low;
		uint64 high;
	};

	struct VtdInterruptStorageDeleter {
		stduint bytes = 0;

		void operator()(void* storage) const {
			mempool.deallocate(storage, bytes);
		}
	};

	constexpr stduint InterruptTableCapacity(uint8 size) {
		// Intel VT-d IRTA.S encodes 2^(S+1) entries.
		return stduint(1) << (size + 1);
	}

	constexpr uint8 SelectInterruptTableSize(stduint required_entries) {
		uint8 size = VtdMinimumInterruptTableSize;
		while (size < VtdMaximumInterruptTableSize && InterruptTableCapacity(size) < required_entries)
			++size;
		return size;
	}

	struct RmrrRoute {
		uint64 base;
		uint64 limit;
		uint16 requester;
		uint8 secondary_bus;
		uint8 subordinate_bus;
	};

	struct IovaRange {
		bool active;
		bool reserved;
		uint64 device_address;
		uint64 physical;
		stduint length;
	};

	struct VtdFaultRecord {
		uint64 info;
		uint64 detail;
	};

	struct VtdFaultEvents {
		VtdFaultRecord records[16];
		uint16 record_offset;
		uint16 record_count;
		uint16 next_record;
		uint8 head;
		uint8 count;
		bool enabled;
		bool overflow;
		stduint report_tick;
		stduint suppressed;
		uint8 reports;
		uint32 last_qi_faults;
	};

	struct ResolvedDmarScope {
		uint8 type;
		uint8 enumeration_id;
		uint16 requester_id;
		uint8 secondary_bus;
		uint8 subordinate_bus;
		bool resolved;
	};

	// A DRHD owns its registers and all table and queue storage published to that hardware.
	struct VtdUnit {
		const DmarHardwareUnit* descriptor;
		volatile uint8* registers;
		uint64 capability;
		uint64 extended_capability;

		uni::UniquePtr<ResolvedDmarScope[]> scopes;
		stduint scope_count;

		bool passthrough_enabled;
		VtdRootEntry* root_table;
		stduint context_table_count;
		stduint device_count;
		stduint xhci_extra_interrupts;

		// Resolved RMRR routes use the PCI topology of this unit's segment.
		uni::UniquePtr<RmrrRoute[]> rmrr_routes;
		stduint rmrr_route_count;
		bool rmrr_routes_ready;

		bool interrupt_remapping_enabled;
		bool extended_interrupt_mode;
		bool compatibility_interrupts_before;
		uni::UniquePtr<VtdInterruptRemappingEntry[], VtdInterruptStorageDeleter> interrupt_remapping_table;
		uni::UniquePtr<byte[], VtdInterruptStorageDeleter> interrupt_remapping_allocated;
		stduint interrupt_remapping_capacity;

		void* invalidation_queue;
		uint32* invalidation_completion;
		stduint invalidation_queue_length;
		uint8 invalidation_queue_shift;
		bool invalidation_queue_failed;

		VtdFaultEvents faults;
	};

	struct IovaDomain {
		bool active;
		bool attached;
		bool restore_failed;
		DeviceNode* device;
		// Keep the same unit until hardware can no longer reference the domain tables.
		VtdUnit* unit;
		uint16 domain_id;
		uint64* root;
		void* tables[IovaTableCapacity];
		stduint table_count;
		IovaRange mappings[IovaMappingCapacity];
	};

	// The DMAR header and the probed unit array are shared platform metadata.
	struct VtdState {
		const DmarTable* dmar_table;
		uint8 dmar_flags;
		uint8 host_address_width;
		bool probe_complete;
		stduint rmrr_route_capacity;
		uni::UniquePtr<VtdUnit[]> units;
		stduint unit_count;
		bool scopes_prepared;
	};

	static_assert(sizeof(DmarTable) == 48);
	static_assert(sizeof(DmarHardwareUnit) == 16);
	static_assert(sizeof(DmarReservedMemory) == 24);
	static_assert(sizeof(DmarDeviceScope) == 6);
	static_assert(sizeof(DmarPciPath) == 2);
	static_assert(sizeof(VtdRootEntry) == 16);
	static_assert(sizeof(VtdContextEntry) == 16);
	static_assert(sizeof(VtdInterruptRemappingEntry) == 16);
	static_assert(InterruptTableCapacity(VtdMinimumInterruptTableSize) *
		sizeof(VtdInterruptRemappingEntry) == PAGE_SIZE);
	static_assert(InterruptTableCapacity(VtdMaximumInterruptTableSize) <= 0xFFFFu);
	static_assert(SelectInterruptTableSize(256) == 7 && SelectInterruptTableSize(257) == 8);

	VtdState vtd_state{};
	// One Devsman notification covers pending hardware faults. The records themselves belong to each unit.
	uni::Atomic<byte> vtd_fault_work{0};
	uni::Atomic<byte> vtd_fault_event_posted{0};
	IovaDomain iova_domains[IovaDomainCapacity] = {};

	// Until device ownership is resolved, only a single include-all unit can be used.
	VtdUnit* SingleVtdUnit() {
		if (!vtd_state.probe_complete || vtd_state.unit_count != 1) return nullptr;
		auto& unit = vtd_state.units[0];
		return unit.descriptor->flags & kDmarIncludeAll ? &unit : nullptr;
	}

	Spinlock* IovaLock() {
		// RTE and IRTE changes share the VT-d queue serialization lock.
		return Devs::x86_ioapic_route_lock();
	}

	uint32 ReadRegister32(volatile uint8* registers, stduint offset) {
		return *reinterpret_cast<volatile uint32*>(registers + offset);
	}

	uint64 ReadRegister64(volatile uint8* registers, stduint offset) {
		const uint32 low = ReadRegister32(registers, offset);
		const uint32 high = ReadRegister32(registers, offset + sizeof(uint32));
		return uint64(low) | (uint64(high) << 32);
	}

	void WriteRegister32(volatile uint8* registers, stduint offset,
		uint32 value) {
		*reinterpret_cast<volatile uint32*>(registers + offset) = value;
	}

	void WriteRegister64(volatile uint8* registers, stduint offset,
		uint64 value) {
		WriteRegister32(registers, offset, uint32(value));
		WriteRegister32(registers, offset + sizeof(uint32), uint32(value >> 32));
	}

	void StoreFence() {
		_ASM volatile ("mfence" ::: "memory");
	}

	bool WaitRegister32(volatile uint8* registers, stduint offset,
		uint32 mask, uint32 expected) {
		for0(i, VtdCommandTimeoutSpins) {
			if ((ReadRegister32(registers, offset) & mask) == expected)
				return true;
			_ASM volatile ("pause" ::: "memory");
		}
		return false;
	}

	bool WaitRegister64Clear(volatile uint8* registers, stduint offset,
		uint64 mask) {
		for0(i, VtdCommandTimeoutSpins) {
			if (!(ReadRegister64(registers, offset) & mask)) return true;
			_ASM volatile ("pause" ::: "memory");
		}
		return false;
	}

	uint32 VtdPersistentCommand(uint32 status) {
		uint32 command = 0;
		if (status & VtdGlobalStatusTranslationEnabled)
			command |= VtdGlobalCommandEnableTranslation;
		if (status & VtdGlobalStatusQueuedInvalidation)
			command |= VtdGlobalCommandEnableQueuedInvalidation;
		if (status & VtdGlobalStatusInterruptRemapping)
			command |= VtdGlobalCommandEnableInterruptRemapping;
		if (status & VtdGlobalStatusCompatibilityInterrupts)
			command |= VtdGlobalCommandCompatibilityInterrupts;
		return command;
	}

	bool SetVtdFeature(const VtdUnit& unit, uint32 command_bit, uint32 status_bit, bool enabled) {
		auto registers = unit.registers;
		uint32 command = VtdPersistentCommand(ReadRegister32(registers,
			kVtdGlobalStatusRegister));
		if (enabled) command |= command_bit;
		else command &= ~command_bit;
		WriteRegister32(registers, VtdGlobalCommandRegister, command);
		return WaitRegister32(registers, kVtdGlobalStatusRegister,
			status_bit, enabled ? status_bit : 0);
	}

	bool PhysicalRangeFits(uint64 address, stduint length,
		uint8 address_width) {
		if (!address_width || address_width > 64 || !length) return false;
		if (address + length < address) return false;
		if (address_width == 64) return true;
		const uint64 limit = uint64(1) << address_width;
		return address < limit && uint64(length) <= limit - address;
	}

	// The shared I/O APIC route lock serializes access to each unit's queue and wait status.
	bool SubmitQueuedInvalidation(VtdUnit& unit, uint64 descriptor_low,
		uint64 descriptor_high = 0) {
		if (unit.invalidation_queue_failed || !unit.invalidation_queue ||
			!unit.invalidation_completion ||
			!(ReadRegister32(unit.registers, kVtdGlobalStatusRegister) &
			VtdGlobalStatusQueuedInvalidation)) return false;
		const stduint descriptor_length = stduint(1) <<
			unit.invalidation_queue_shift;
		const uint32 queue_mask = uint32(unit.invalidation_queue_length - 1);
		const uint32 tail = ReadRegister32(unit.registers,
			VtdInvalidationQueueTailRegister) & queue_mask;
		if ((tail & (descriptor_length - 1)) ||
			tail + descriptor_length > unit.invalidation_queue_length)
			return false;
		const uint32 wait_tail =
			(tail + uint32(descriptor_length)) & queue_mask;
		const uint32 next =
			(wait_tail + uint32(descriptor_length)) & queue_mask;
		const uint32 head = ReadRegister32(unit.registers,
			VtdInvalidationQueueHeadRegister) & queue_mask;
		if (wait_tail == head || next == head) return false;
		auto* descriptor = reinterpret_cast<uint64*>(
			reinterpret_cast<uint8*>(unit.invalidation_queue) + tail);
		MemSet(descriptor, 0, descriptor_length);
		descriptor[0] = descriptor_low;
		descriptor[1] = descriptor_high;
		auto* wait_descriptor = reinterpret_cast<uint64*>(
			reinterpret_cast<uint8*>(unit.invalidation_queue) + wait_tail);
		MemSet(wait_descriptor, 0, descriptor_length);
		*unit.invalidation_completion = VtdInvalidationStatusPending;
		wait_descriptor[0] = VtdInvalidationWaitType |
			VtdInvalidationWaitStatusWrite |
			(uint64(VtdInvalidationStatusComplete) << 32);
		wait_descriptor[1] = uint64(stduint(unit.invalidation_completion));
		StoreFence();
		WriteRegister32(unit.registers,
			VtdInvalidationQueueTailRegister, next);
		for0(spin, VtdCommandTimeoutSpins) {
			const uint32 faults = ReadRegister32(unit.registers,
				kVtdFaultStatusRegister);
			if (faults & VtdInvalidationQueueFaults) break;
			if (*reinterpret_cast<volatile uint32*>(
				unit.invalidation_completion) ==
				VtdInvalidationStatusComplete) return true;
			_ASM volatile ("pause" ::: "memory");
		}
		// A late wait descriptor must not complete a later submission.
		unit.invalidation_queue_failed = true;
		return false;
	}

	bool SubmitInterruptCacheInvalidation(VtdUnit& unit, uint16 index = 0,
		uint8 mask = 0, bool selective = false) {
		uint64 descriptor = VtdInterruptCacheInvalidationType;
		if (selective) {
			descriptor |= VtdInterruptCacheInvalidationSelective;
			descriptor |= uint64(mask & 0x1Fu) << 27;
			descriptor |= uint64(index) << 32;
		}
		return SubmitQueuedInvalidation(unit, descriptor);
	}

	void ReleaseInterruptRemappingStorage(VtdUnit& unit) {
		if (unit.invalidation_queue) {
			mempool.deallocate(unit.invalidation_queue,
				unit.invalidation_queue_length);
			unit.invalidation_queue = nullptr;
			unit.invalidation_queue_length = 0;
		}
		if (unit.invalidation_completion) {
			mempool.deallocate(unit.invalidation_completion, PAGE_SIZE);
			unit.invalidation_completion = nullptr;
		}
		unit.interrupt_remapping_table.reset();
		unit.interrupt_remapping_allocated.reset();
		unit.interrupt_remapping_capacity = 0;
		unit.interrupt_remapping_enabled = false;
		unit.extended_interrupt_mode = false;
		unit.invalidation_queue_failed = false;
	}

	// Stop the hardware before returning any queue or interrupt table storage to the allocator.
	bool DisableInterruptRemappingForRollback(const VtdUnit& unit, bool compatibility_enabled_before) {
		const uint32 status = ReadRegister32(unit.registers,
			kVtdGlobalStatusRegister);
		if (status & VtdGlobalStatusInterruptRemapping) {
			if (!SetVtdFeature(unit, VtdGlobalCommandEnableInterruptRemapping,
				VtdGlobalStatusInterruptRemapping, false)) return false;
		}
		if (ReadRegister32(unit.registers, kVtdGlobalStatusRegister) &
			VtdGlobalStatusQueuedInvalidation) {
			if (!SetVtdFeature(unit, VtdGlobalCommandEnableQueuedInvalidation,
				VtdGlobalStatusQueuedInvalidation, false)) return false;
		}
		const bool compatibility_enabled =
			(ReadRegister32(unit.registers, kVtdGlobalStatusRegister) &
			VtdGlobalStatusCompatibilityInterrupts) != 0;
		if (compatibility_enabled != compatibility_enabled_before &&
			!SetVtdFeature(unit, VtdGlobalCommandCompatibilityInterrupts,
				VtdGlobalStatusCompatibilityInterrupts,
				compatibility_enabled_before)) return false;
		return true;
	}

	bool EnableVtdInterruptRemapping(VtdUnit& unit, bool extended_mode, stduint reserved_irq_entries) {
		if (!vtd_state.probe_complete || !unit.passthrough_enabled)
			return false;
		if (unit.interrupt_remapping_enabled)
			return unit.extended_interrupt_mode == extended_mode;
		if (unit.invalidation_queue ||
			unit.interrupt_remapping_table) return false;
		if (!(vtd_state.dmar_flags & 1u) ||
			!(unit.extended_capability & (uint64(1) << 3)) ||
			!(unit.extended_capability & (uint64(1) << 1)) ||
			!(unit.extended_capability & 1u)) {
			plogtrac("[IOMMU] Interrupt remapping unavailable flags=%02X ecap=%[64H]",
				vtd_state.dmar_flags, unit.extended_capability);
			return false;
		}
		// EIME rejects every compatibility-format MSI and I/O APIC message.
		// Keep xAPIC-format remapping until all kernel and I/O APIC sources use IRTEs.
		if (extended_mode && (vtd_state.dmar_flags & (1u << 1))) {
			plogwarn("[IOMMU] Firmware opted out of x2APIC interrupt remapping");
			return false;
		}
		if (extended_mode &&
			!(unit.extended_capability & (uint64(1) << 4))) {
			plogwarn("[IOMMU] Interrupt remapping lacks extended interrupt mode");
			return false;
		}
		// Allow one base interrupt per PCI requester; xHCI can consume multiple entries in EIME mode.
		const stduint required_entries = reserved_irq_entries + unit.device_count +
			(extended_mode ? unit.xhci_extra_interrupts : 0);
		const uint8 table_size = SelectInterruptTableSize(required_entries);
		const stduint capacity = InterruptTableCapacity(table_size);
		if (required_entries > capacity) {
			plogerro("[IOMMU] Interrupt table budget exceeds capacity required=%u limit=%u",
				unsigned(required_entries), unsigned(capacity));
			return false;
		}
		const stduint table_bytes = capacity * sizeof(VtdInterruptRemappingEntry);
		const stduint bitmap_bytes = capacity / _BYTE_BITS_;
		uni::UniquePtr<VtdInterruptRemappingEntry[], VtdInterruptStorageDeleter> table(
			static_cast<VtdInterruptRemappingEntry*>(mempool.allocate(table_bytes, 12)),
			VtdInterruptStorageDeleter{table_bytes});
		uni::UniquePtr<byte[], VtdInterruptStorageDeleter> allocated(
			static_cast<byte*>(mempool.allocate(bitmap_bytes)), VtdInterruptStorageDeleter{bitmap_bytes});
		const uint64 table_physical = uint64(stduint(table.get()));
		if (!table || !allocated ||
			!PhysicalRangeFits(table_physical, table_bytes, vtd_state.host_address_width)) {
			plogerro("[IOMMU] Failed to allocate interrupt table required=%u entries=%u bytes=%u",
				unsigned(required_entries), unsigned(capacity), unsigned(table_bytes));
			return false;
		}
		const bool scalable_queue =
			(unit.extended_capability & (uint64(1) << 43)) != 0;
		const bool compatibility_enabled_before =
			(ReadRegister32(unit.registers, kVtdGlobalStatusRegister) &
			VtdGlobalStatusCompatibilityInterrupts) != 0;
		unit.compatibility_interrupts_before = compatibility_enabled_before;
		const uint8 queue_shift = scalable_queue ? 5 : 4;
		const stduint queue_length = VtdInvalidationQueueLength << queue_shift;
		auto* queue = mempool.allocate(queue_length, 12);
		auto* completion = reinterpret_cast<uint32*>(
			mempool.allocate(PAGE_SIZE, 12));
		if (!queue || !completion ||
			!PhysicalRangeFits(uint64(stduint(queue)), queue_length,
				vtd_state.host_address_width) ||
			!PhysicalRangeFits(uint64(stduint(completion)), PAGE_SIZE,
				vtd_state.host_address_width)) {
			if (queue) mempool.deallocate(queue, queue_length);
			if (completion) mempool.deallocate(completion, PAGE_SIZE);
			plogerro("[IOMMU] Failed to allocate interrupt-remapping storage");
			return false;
		}
		MemSet(queue, 0, queue_length);
		MemSet(completion, 0, PAGE_SIZE);
		MemSet(table.get(), 0, table_bytes);
		MemSet(allocated.get(), 0, bitmap_bytes);
		StoreFence();

		unit.invalidation_queue = queue;
		unit.invalidation_completion = completion;
		unit.invalidation_queue_length = queue_length;
		unit.invalidation_queue_shift = queue_shift;
		unit.interrupt_remapping_table.swap(table);
		unit.interrupt_remapping_allocated.swap(allocated);
		unit.interrupt_remapping_capacity = capacity;
		WriteRegister32(unit.registers,
			VtdInvalidationQueueTailRegister, 0);
		uint64 queue_address = uint64(stduint(queue));
		if (scalable_queue) queue_address |= uint64(1) << 11;
		WriteRegister64(unit.registers,
			VtdInvalidationQueueAddressRegister, queue_address);
		if (!SetVtdFeature(unit, VtdGlobalCommandEnableQueuedInvalidation,
			VtdGlobalStatusQueuedInvalidation, true)) {
			plogerro("[IOMMU] Queued invalidation enable timed out");
			if (DisableInterruptRemappingForRollback(unit,
				compatibility_enabled_before))
				ReleaseInterruptRemappingStorage(unit);
			return false;
		}

		uint64 table_address = table_physical | table_size;
		if (extended_mode) table_address |= VtdInterruptRemappingExtendedMode;
		WriteRegister64(unit.registers,
			VtdInterruptRemappingTableAddressRegister, table_address);
		uint32 command = VtdPersistentCommand(ReadRegister32(unit.registers,
			kVtdGlobalStatusRegister));
		command |= VtdGlobalCommandSetInterruptRemappingTable;
		if (!extended_mode) command |= VtdGlobalCommandCompatibilityInterrupts;
		else command &= ~VtdGlobalCommandCompatibilityInterrupts;
		WriteRegister32(unit.registers, VtdGlobalCommandRegister, command);
		const uint32 required_status = VtdGlobalStatusInterruptRemappingTableSet |
			(extended_mode ? 0u : VtdGlobalStatusCompatibilityInterrupts);
		const uint32 status_mask = extended_mode ? VtdGlobalStatusInterruptRemappingTableSet : required_status;
		if (!WaitRegister32(unit.registers, kVtdGlobalStatusRegister,
			status_mask, required_status) ||
			!SubmitInterruptCacheInvalidation(unit)) {
			plogerro("[IOMMU] Interrupt-remapping table activation failed");
			if (DisableInterruptRemappingForRollback(unit,
				compatibility_enabled_before))
				ReleaseInterruptRemappingStorage(unit);
			return false;
		}
		unit.extended_interrupt_mode = extended_mode;
		ploginfo("[IOMMU] Interrupt table prepared required=%u entries=%u bytes=%u",
			unsigned(required_entries), unsigned(capacity), unsigned(table_bytes));
		// Extended mode is committed after Devsman installs masked remappable RTEs.
		if (extended_mode) return true;
		if (!SetVtdFeature(unit, VtdGlobalCommandEnableInterruptRemapping,
			VtdGlobalStatusInterruptRemapping, true)) {
			plogerro("[IOMMU] Interrupt remapping enable timed out");
			if (DisableInterruptRemappingForRollback(unit,
				compatibility_enabled_before))
				ReleaseInterruptRemappingStorage(unit);
			return false;
		}
		unit.interrupt_remapping_enabled = true;
		ploginfo("[IOMMU] Interrupt remapping enabled entries=%u mode=xAPIC",
			unsigned(capacity));
		return true;
	}

	bool MapRegisterWindow(uint64 register_base) {
		if (register_base > uint64(~_IMM0) ||
			register_base + kVtdRegisterWindowLength < register_base ||
			(register_base & (PAGE_SIZE - 1)) != 0) {
			plogwarn("[IOMMU] Invalid VT-d register base %[64H]", register_base);
			return false;
		}
		const stduint address = stduint(register_base);
		if (!Memory::MapPages(kernel_paging, address, address, PAGE_SIZE,
			PAGESIZE_ANY, PGPROP_present | PGPROP_writable |
			PGTYPE_device | PGOPTS_omit_warnings)) {
			plogwarn("[IOMMU] Failed to map VT-d registers at %[64H]",
				register_base);
			return false;
		}
		setCR3(getCR3());
		return true;
	}

	bool LogDeviceScopes(const uint8* begin, const uint8* end,
		uint16 segment, const char* owner, stduint owner_index) {
		stduint scope_index = 0;
		while (begin < end) {
			const stduint remaining = stduint(end - begin);
			if (remaining < sizeof(DmarDeviceScope)) {
				plogwarn("[IOMMU] %s%u segment=%u has truncated device scope",
					owner, owner_index, segment);
				return false;
			}
			auto scope = reinterpret_cast<const DmarDeviceScope*>(begin);
			if (scope->length < sizeof(DmarDeviceScope) ||
				scope->length > remaining ||
				(scope->length - sizeof(DmarDeviceScope)) % sizeof(DmarPciPath)) {
				plogwarn("[IOMMU] %s%u segment=%u has invalid scope length=%u",
					owner, owner_index, segment, scope->length);
				return false;
			}
			const stduint path_count =
				(scope->length - sizeof(DmarDeviceScope)) / sizeof(DmarPciPath);
			ploginfo("[IOMMU] %s%u scope%u type=%u start-bus=%u enum=%u paths=%u",
				owner, owner_index, scope_index, scope->type,
				scope->start_bus, scope->enumeration_id, path_count);
			auto path = reinterpret_cast<const DmarPciPath*>(
				begin + sizeof(DmarDeviceScope));
			for0(i, path_count) {
				ploginfo("[IOMMU] %s%u scope%u path%u=%u.%u",
					owner, owner_index, scope_index, i,
					path[i].device, path[i].function);
			}
			begin += scope->length;
			scope_index++;
		}
		return true;
	}

	bool ProbeHardwareUnit(VtdUnit& unit, const DmarHardwareUnit& descriptor, stduint index) {
		ploginfo("[IOMMU] DRHD%u segment=%u include-all=%u base=%[64H] length=%u",
			index, descriptor.segment, unsigned((descriptor.flags & kDmarIncludeAll) != 0),
			descriptor.register_base, descriptor.header.length);
		const uint8* entry_begin = reinterpret_cast<const uint8*>(&descriptor);
		if (!LogDeviceScopes(entry_begin + sizeof(DmarHardwareUnit),
			entry_begin + descriptor.header.length, descriptor.segment, "DRHD", index))
			return false;
		if (!MapRegisterWindow(descriptor.register_base)) return false;

		auto registers = reinterpret_cast<volatile uint8*>(
			stduint(descriptor.register_base));
		const uint32 version = ReadRegister32(registers, kVtdVersionRegister);
		const uint64 capability = ReadRegister64(registers,
			kVtdCapabilityRegister);
		const uint64 extended = ReadRegister64(registers,
			kVtdExtendedCapabilityRegister);
		const uint32 global_status = ReadRegister32(registers,
			kVtdGlobalStatusRegister);
		const uint64 root_table = ReadRegister64(registers,
			kVtdRootTableAddressRegister);
		const uint32 fault_status = ReadRegister32(registers,
			kVtdFaultStatusRegister);

		const uint32 domain_count = 1u << (4 + 2 * (capability & 0x7));
		const uint32 fault_count = uint32((capability >> 40) & 0xFF) + 1;
		const uint32 fault_offset = uint32((capability >> 24) & 0x3FF) * 16;
		const uint32 iotlb_offset = uint32((extended >> 8) & 0x3FF) * 16;
		ploginfo("[IOMMU] DRHD%u version=%u.%u cap=%[64H] ecap=%[64H]",
			index, (version >> 4) & 0xF, version & 0xF,
			capability, extended);
		ploginfo("[IOMMU] DRHD%u domains=%u mgaw=%u sagaw=%02X sps=%X cm=%u nfr=%u fro=%X",
			index, domain_count, unsigned(((capability >> 16) & 0x3F) + 1),
			unsigned((capability >> 8) & 0x1F),
			unsigned((capability >> 34) & 0xF),
			unsigned((capability >> 7) & 1), fault_count, fault_offset);
		ploginfo("[IOMMU] DRHD%u coherent=%u qi=%u dt=%u ir=%u pt=%u sc=%u iotlb=%X",
			index, unsigned(extended & 1), unsigned((extended >> 1) & 1),
			unsigned((extended >> 2) & 1), unsigned((extended >> 3) & 1),
			unsigned((extended >> 6) & 1), unsigned((extended >> 7) & 1),
			iotlb_offset);
		ploginfo("[IOMMU] DRHD%u gsts=%08X te=%u rtaddr=%[64H] fsts=%08X",
			index, global_status, unsigned((global_status >> 31) & 1),
			root_table, fault_status);
		unit.descriptor = &descriptor;
		unit.registers = registers;
		unit.capability = capability;
		unit.extended_capability = extended;
		return true;
	}

	bool ProbeReservedMemory(const DmarReservedMemory& range, stduint index) {
		ploginfo("[IOMMU] RMRR%u segment=%u base=%[64H] end=%[64H] length=%u",
			index, range.segment, range.base_address, range.end_address,
			range.header.length);
		const uint8* entry_begin = reinterpret_cast<const uint8*>(&range);
		return LogDeviceScopes(entry_begin + sizeof(DmarReservedMemory),
			entry_begin + range.header.length, range.segment, "RMRR", index);
	}

	bool ReadBridgeBuses(uint16 segment, uint16 requester, uint8& secondary,
		uint8& subordinate) {
		const uint8 bus = uint8(requester >> 8);
		const uint8 device = uint8(requester >> 3) & 31u;
		const uint8 function = uint8(requester) & 7u;
		const uint32 class_code = uni::PCI::read_config_register(segment,
			bus, device, function, 0x08);
		if ((class_code >> 16) != 0x0604u) return false;
		const uint32 buses = uni::PCI::read_config_register(segment,
			bus, device, function, 0x18);
		secondary = uint8(buses >> 8);
		subordinate = uint8(buses >> 16);
		return secondary && secondary != 0xFFu && uint8(buses) == bus &&
			secondary != bus && secondary <= subordinate;
	}

	// Intel VT-d specification, section 8.3.1: resolve Device Scope paths through PCI bridges.
	bool ResolveScopeRequester(uint16 segment, const DmarDeviceScope& scope,
		uint16& requester) {
		const auto* path = reinterpret_cast<const DmarPciPath*>(
			reinterpret_cast<const uint8*>(&scope) + sizeof(scope));
		const stduint count = (scope.length - sizeof(scope)) / sizeof(*path);
		uint8 bus = scope.start_bus;
		for0(i, count) {
			if (path[i].device >= 32 || path[i].function >= 8) return false;
			requester = (uint16(bus) << 8) | (uint16(path[i].device) << 3) |
				path[i].function;
			if (i + 1 == count) return true;
			uint8 subordinate;
			if (!ReadBridgeBuses(segment, requester, bus, subordinate)) return false;
		}
		return false;
	}

	bool IsPciScope(uint8 type) {
		return type == kDmarScopePciEndpoint || type == kDmarScopePciBridge;
	}

	bool PrepareDrhdScopes(VtdUnit& unit, stduint unit_index) {
		const auto* bytes = reinterpret_cast<const uint8*>(unit.descriptor);
		const auto* begin = bytes + sizeof(*unit.descriptor);
		const auto* end = bytes + unit.descriptor->header.length;
		const uint16 segment = unit.descriptor->segment;
		stduint count = 0;
		// ProbeHardwareUnit already checked the lengths of these firmware records.
		for (auto* cursor = begin; cursor < end;) {
			const auto* scope = reinterpret_cast<const DmarDeviceScope*>(cursor);
			cursor += scope->length;
			if (IsPciScope(scope->type) || scope->type == kDmarScopeIoApic) ++count;
		}
		if (!count) return true;
		uni::UniquePtr<ResolvedDmarScope[]> scopes(new (std::nothrow_t{}) ResolvedDmarScope[count]{});
		if (!scopes) {
			plogwarn("[IOMMU] Cannot allocate DRHD%u scopes segment=%04X", unit_index, unsigned(segment));
			return false;
		}

		stduint index = 0;
		for (auto* cursor = begin; cursor < end;) {
			const auto* scope = reinterpret_cast<const DmarDeviceScope*>(cursor);
			cursor += scope->length;
			if (!IsPciScope(scope->type) && scope->type != kDmarScopeIoApic) continue;
			auto& resolved = scopes[index++];
			resolved.type = scope->type;
			resolved.enumeration_id = scope->enumeration_id;
			resolved.resolved = ResolveScopeRequester(segment, *scope, resolved.requester_id);
			if (resolved.resolved && scope->type == kDmarScopePciBridge)
				resolved.resolved = ReadBridgeBuses(segment, resolved.requester_id,
					resolved.secondary_bus, resolved.subordinate_bus);
			if (!resolved.resolved)
				plogwarn("[IOMMU] DRHD%u unresolved scope segment=%04X type=%u id=%u start-bus=%u",
					unit_index, unsigned(segment), unsigned(scope->type),
					unsigned(scope->enumeration_id), unsigned(scope->start_bus));
		}
		unit.scopes.swap(scopes);
		unit.scope_count = count;
		return true;
	}

	enum class DrhdMatch : uint8 { None, Explicit, IncludeAll, Conflict, Unresolved };

	struct DrhdOwnership {
		DrhdMatch match = DrhdMatch::None;
		const VtdUnit* unit = nullptr;
		uint16 source_id = 0;
	};

	// Intel VT-d specification, sections 8.3 and 8.3.1: explicit PCI scopes precede INCLUDE_PCI_ALL within a segment.
	DrhdOwnership FindPciOwner(uint16 segment, uint16 requester_id) {
		const VtdUnit* explicit_owner = nullptr;
		const VtdUnit* fallback = nullptr;
		bool unresolved = false;
		const uint8 bus = uint8(requester_id >> 8);
		for0(i, vtd_state.unit_count) {
			const auto& unit = vtd_state.units[i];
			if (unit.descriptor->segment != segment) continue;
			if (unit.descriptor->flags & kDmarIncludeAll) {
				if (fallback) return {DrhdMatch::Conflict};
				fallback = &unit;
			}
			for0(scope_index, unit.scope_count) {
				const auto& scope = unit.scopes[scope_index];
				if (!IsPciScope(scope.type)) continue;
				if (!scope.resolved) {
					unresolved = true;
					continue;
				}
				// A bridge scope covers the bridge itself and every downstream bus.
				const bool downstream = scope.type == kDmarScopePciBridge &&
					bus >= scope.secondary_bus && bus <= scope.subordinate_bus;
				if (scope.requester_id != requester_id && !downstream) continue;
				if (explicit_owner && explicit_owner != &unit) return {DrhdMatch::Conflict};
				explicit_owner = &unit;
			}
		}
		// An unresolved path may claim this requester, so the segment default is not safe to assume.
		if (unresolved) return {DrhdMatch::Unresolved};
		if (explicit_owner) return {DrhdMatch::Explicit, explicit_owner, requester_id};
		if (fallback) return {DrhdMatch::IncludeAll, fallback, requester_id};
		return {};
	}

	DrhdOwnership FindIoApicOwner(uint8 id) {
		DrhdOwnership owner;
		for0(i, vtd_state.unit_count) {
			const auto& unit = vtd_state.units[i];
			for0(scope_index, unit.scope_count) {
				const auto& scope = unit.scopes[scope_index];
				if (scope.type != kDmarScopeIoApic || scope.enumeration_id != id) continue;
				if (owner.match != DrhdMatch::None) return {DrhdMatch::Conflict};
				owner = scope.resolved ? DrhdOwnership{DrhdMatch::Explicit, &unit, scope.requester_id} :
					DrhdOwnership{DrhdMatch::Unresolved};
			}
		}
		return owner;
	}

	void TracePciDrhdOwners(const DeviceNode* node) {
		if (node->fields.node_type == uint16(DeviceNodeType::PciDevice)) {
			const auto& pci = node->fields;
			const uint16 requester = (uint16(pci.pci_bus) << 8) |
				(uint16(pci.pci_device) << 3) | pci.pci_function;
			const auto owner = FindPciOwner(pci.pci_segment, requester);
			if (owner.unit)
				plogtrac("[IOMMU] PCI %04X:%02X:%02X.%u -> DRHD%u (%s)",
					unsigned(pci.pci_segment), unsigned(pci.pci_bus), unsigned(pci.pci_device),
					unsigned(pci.pci_function), stduint(owner.unit - vtd_state.units.get()),
					owner.match == DrhdMatch::Explicit ? "scope" : "include-all");
			else if (owner.match == DrhdMatch::Conflict)
				plogwarn("[IOMMU] Conflicting DRHD scopes for PCI %04X:%02X:%02X.%u",
					unsigned(pci.pci_segment), unsigned(pci.pci_bus),
					unsigned(pci.pci_device), unsigned(pci.pci_function));
			else
				plogtrac("[IOMMU] PCI %04X:%02X:%02X.%u DRHD owner %s",
					unsigned(pci.pci_segment), unsigned(pci.pci_bus), unsigned(pci.pci_device),
					unsigned(pci.pci_function), owner.match == DrhdMatch::Unresolved ? "unresolved" : "missing");
		}
		for (const Nnode* child = node->link.subf; child; child = child->next)
			TracePciDrhdOwners(reinterpret_cast<const DeviceNode*>(child));
	}

	void PrepareDrhdOwnership(const DeviceNode* pci_root) {
		if (vtd_state.scopes_prepared) return;
		for0(i, vtd_state.unit_count) {
			if (PrepareDrhdScopes(vtd_state.units[i], i)) continue;
			// Do not publish a partial index that would miss claims from other units.
			for (stduint j = 0; j < i; ++j) {
				vtd_state.units[j].scopes.reset();
				vtd_state.units[j].scope_count = 0;
			}
			return;
		}
		vtd_state.scopes_prepared = true;
		TracePciDrhdOwners(pci_root);

		// Match DMAR's enumeration ID to the MADT I/O APIC ID, with no PCI segment fallback.
		uint8 reported_ids[32] = {};
		uni::Bitmap reported(reported_ids, sizeof(reported_ids));
		for0(i, vtd_state.unit_count) {
			const auto& unit = vtd_state.units[i];
			for0(scope_index, unit.scope_count) {
				const auto& scope = unit.scopes[scope_index];
				const uint8 id = scope.enumeration_id;
				if (scope.type != kDmarScopeIoApic || reported[id]) continue;
				reported.setof(id);
				const auto owner = FindIoApicOwner(id);
				if (owner.unit)
					plogtrac("[IOMMU] I/O APIC id=%u -> DRHD%u segment=%04X source=%04X",
						unsigned(id), stduint(owner.unit - vtd_state.units.get()),
						unsigned(owner.unit->descriptor->segment), unsigned(owner.source_id));
				else if (owner.match == DrhdMatch::Conflict)
					plogwarn("[IOMMU] Conflicting DRHD scopes for I/O APIC id=%u", unsigned(id));
			}
		}
	}

	bool PrepareRmrrRoutes(VtdUnit& unit) {
		const stduint capacity = vtd_state.rmrr_route_capacity;
		if (capacity > __PTRDIFF_MAX__ / sizeof(RmrrRoute)) return false;
		if (capacity) {
			unit.rmrr_routes.reset(new (std::nothrow_t{}) RmrrRoute[capacity]);
			if (!unit.rmrr_routes) return false;
		}
		const auto* bytes = reinterpret_cast<const uint8*>(vtd_state.dmar_table);
		const auto* end = bytes + vtd_state.dmar_table->header.length;
		for (auto* cursor = bytes + sizeof(DmarTable); cursor < end;) {
			const auto* header = reinterpret_cast<const DmarEntryHeader*>(cursor);
			const auto* entry_end = cursor + header->length;
			cursor = entry_end;
			if (header->type != kDmarReservedMemory) continue;
			const auto* range = reinterpret_cast<const DmarReservedMemory*>(header);
			if (range->segment != unit.descriptor->segment) continue;
			auto* scope_bytes = reinterpret_cast<const uint8*>(range) + sizeof(*range);
			if (scope_bytes == entry_end) return false;
			while (scope_bytes < entry_end) {
				const auto* scope = reinterpret_cast<const DmarDeviceScope*>(scope_bytes);
				scope_bytes += scope->length;
				RmrrRoute route = {range->base_address, range->end_address, 0, 0, 0};
				if ((scope->type != kDmarScopePciEndpoint && scope->type != kDmarScopePciBridge) ||
					!ResolveScopeRequester(range->segment, *scope, route.requester)) return false;
				if (scope->type == kDmarScopePciBridge &&
					!ReadBridgeBuses(range->segment, route.requester,
						route.secondary_bus, route.subordinate_bus)) return false;
				unit.rmrr_routes[unit.rmrr_route_count++] = route;
			}
		}
		return true;
	}

	VtdContextEntry* GetContextTable(VtdRootEntry* root_table, uint8 bus) {
		const uint64 address = root_table[bus].low & VtdPageAddressMask;
		return reinterpret_cast<VtdContextEntry*>(stduint(address));
	}

	VtdContextEntry* EnsureContextTable(VtdRootEntry* root_table, uint8 bus,
		stduint& table_count) {
		if (root_table[bus].low & VtdRootPresent)
			return GetContextTable(root_table, bus);
		auto table = reinterpret_cast<VtdContextEntry*>(
			mempool.allocate(PAGE_SIZE, 12));
		if (!table) {
			plogerro("[IOMMU] Failed to allocate context table for bus=%u", bus);
			return nullptr;
		}
		const uint64 physical = uint64(stduint(table));
		if (!PhysicalRangeFits(physical, PAGE_SIZE,
			vtd_state.host_address_width)) {
			plogerro("[IOMMU] Context table outside host address width bus=%u addr=%[64H]",
				bus, physical);
			mempool.deallocate(table, PAGE_SIZE);
			return nullptr;
		}
		MemSet(table, 0, PAGE_SIZE);
		root_table[bus].high = 0;
		StoreFence();
		root_table[bus].low = physical | VtdRootPresent;
		table_count++;
		return table;
	}

	bool PopulatePassthroughContexts(const VtdUnit& unit, DeviceNode* node,
		VtdRootEntry* root_table, stduint& table_count,
		stduint& device_count, stduint& xhci_extra_interrupts) {
		if (!node) return true;
		if (node->fields.node_type == uint16(DeviceNodeType::PciDevice)) {
			if (node->fields.pci_segment != unit.descriptor->segment) {
				plogwarn("[IOMMU] PCI requester outside DRHD segment pci=%02X:%02X.%u segment=%u",
					node->fields.pci_bus, node->fields.pci_device,
					node->fields.pci_function, node->fields.pci_segment);
				return false;
			}
			if (node->fields.pci_device >= 32 ||
				node->fields.pci_function >= 8) {
				plogwarn("[IOMMU] Invalid PCI requester pci=%02X:%02X.%u",
					node->fields.pci_bus, node->fields.pci_device,
					node->fields.pci_function);
				return false;
			}
			auto table = EnsureContextTable(root_table,
				node->fields.pci_bus, table_count);
			if (!table) return false;
			const stduint requester = stduint(node->fields.pci_device) * 8 +
				node->fields.pci_function;
			auto& context = table[requester];
			if (context.low & VtdContextPresent) {
				plogwarn("[IOMMU] Duplicate PCI requester pci=%02X:%02X.%u",
					node->fields.pci_bus, node->fields.pci_device,
					node->fields.pci_function);
				return false;
			}
			context.high = uint64(VtdPassthroughDomain) << 8 |
				VtdAddressWidth48;
			StoreFence();
			context.low = VtdContextPassthrough | VtdContextPresent;
			device_count++;
			#if CONFIG_ENABLE_USB && CONFIG_ENABLE_USB_XHCI
			if (node->fields.class_base == ClassBase_SerialBusController &&
				node->fields.class_sub == ClassSub_UniversalSerialBusController &&
				node->fields.class_if == ClassInterface_XHCI)
				xhci_extra_interrupts += Devs::XHCIMaxConfiguredInterrupters - 1u;
			#endif
		}
		for (Nnode* child = node->link.subf; child; child = child->next) {
			if (!PopulatePassthroughContexts(unit,
				reinterpret_cast<DeviceNode*>(child), root_table,
				table_count, device_count, xhci_extra_interrupts)) return false;
		}
		return true;
	}

	void ReleasePassthroughTables(VtdRootEntry* root_table) {
		if (!root_table) return;
		for0(bus, 256) {
			if (!(root_table[bus].low & VtdRootPresent)) continue;
			auto table = GetContextTable(root_table, uint8(bus));
			root_table[bus].low = 0;
			root_table[bus].high = 0;
			mempool.deallocate(table, PAGE_SIZE);
		}
		mempool.deallocate(root_table, PAGE_SIZE);
	}

	bool InvalidateVtdCaches(VtdUnit& unit) {
		auto registers = unit.registers;
		if (ReadRegister32(registers, kVtdGlobalStatusRegister) &
			VtdGlobalStatusQueuedInvalidation) {
			const uint64 context_descriptor = 1u | (uint64(1) << 4);
			uint64 iotlb_descriptor = 2u | (uint64(1) << 4);
			if ((unit.capability >> 55) & 1)
				iotlb_descriptor |= uint64(1) << 7;
			if ((unit.capability >> 54) & 1)
				iotlb_descriptor |= uint64(1) << 6;
			if (!SubmitQueuedInvalidation(unit, context_descriptor) ||
				!SubmitQueuedInvalidation(unit, iotlb_descriptor)) {
				plogerro("[IOMMU] Queued context/IOTLB invalidation failed");
				return false;
			}
			return true;
		}
		WriteRegister64(registers, VtdContextCommandRegister,
			VtdContextCommandInvalidate | VtdContextCommandGlobal);
		if (!WaitRegister64Clear(registers, VtdContextCommandRegister,
			VtdContextCommandInvalidate)) {
			plogerro("[IOMMU] Context-cache invalidation timed out");
			return false;
		}

		const stduint iotlb_offset = stduint(
			(unit.extended_capability >> 8) & 0x3FF) * 16;
		if (iotlb_offset + 16 > PAGE_SIZE) {
			plogerro("[IOMMU] IOTLB register outside mapped window offset=%X",
				iotlb_offset);
			return false;
		}
		uint64 command = VtdIotlbCommandInvalidate | VtdIotlbCommandGlobal;
		if ((unit.capability >> 55) & 1) command |= VtdIotlbReadDrain;
		if ((unit.capability >> 54) & 1) command |= VtdIotlbWriteDrain;
		WriteRegister64(registers, iotlb_offset + 8, command);
		if (!WaitRegister64Clear(registers, iotlb_offset + 8,
			VtdIotlbCommandInvalidate)) {
			plogerro("[IOMMU] IOTLB invalidation timed out");
			return false;
		}
		return true;
	}

	uint32 ReadFaultStatus(const VtdUnit& unit) {
		return ReadRegister32(unit.registers, kVtdFaultStatusRegister);
	}

	bool SetFaultEventMask(const VtdUnit& unit, bool masked) {
		WriteRegister32(unit.registers, VtdFaultEventControlRegister, masked ? VtdFaultEventMask : 0);
		return bool(ReadRegister32(unit.registers, VtdFaultEventControlRegister) & VtdFaultEventMask) == masked;
	}

	// The caller holds IovaLock(). FRI identifies the start of a batch, not its next record.
	uint32 CaptureFaultRecords(VtdUnit& unit, uint8 budget) {
		auto& faults = unit.faults;
		uint32 status = ReadFaultStatus(unit);
		for (uint8 i = 0; i < budget && (status & VtdPrimaryFaultPending) &&
			faults.count < numsof(faults.records); ++i) {
			if (faults.next_record == VtdFaultCursorUnset)
				faults.next_record = uint16((status >> 8) & 0xFF);
			if (faults.next_record >= faults.record_count) break;
			const stduint reg = faults.record_offset + faults.next_record * VtdFaultRecordSize;
			const uint32 high = ReadRegister32(unit.registers, reg + 12);
			if (!(high & VtdFaultRecordValid)) break;
			auto& record = faults.records[
				(faults.head + faults.count) % numsof(faults.records)];
			record.info = ReadRegister64(unit.registers, reg);
			record.detail = (uint64(high) << 32) | ReadRegister32(unit.registers, reg + 8);
			++faults.count;
			WriteRegister32(unit.registers, reg + 12, VtdFaultRecordValid);
			if (++faults.next_record == faults.record_count) faults.next_record = 0;
			status = ReadFaultStatus(unit);
		}
		if (status & VtdPrimaryFaultOverflow) {
			faults.overflow = true;
			if (!(status & VtdPrimaryFaultPending)) {
				WriteRegister32(unit.registers, kVtdFaultStatusRegister, VtdPrimaryFaultOverflow);
				status = ReadFaultStatus(unit);
			}
		}
		if (!(status & VtdPrimaryFaultPending)) faults.next_record = VtdFaultCursorUnset;
		return status;
	}

	void HandleVtdFaultEvent(stduint vector) {
		if (auto* selected = SingleVtdUnit()) {
			auto& unit = *selected;
			SpinlockLocal guard(IovaLock());
			if (unit.faults.enabled &&
				(ReadFaultStatus(unit) & VtdFaultEventStatus)) {
				(void)SetFaultEventMask(unit, true);
				CaptureFaultRecords(unit, VtdFaultIrqBudget);
				vtd_fault_work.store(1, uni::MemoryOrder_Release);
			}
		}
		IC.SendEOI(vector);
	}

	void LogVtdFaultState(const VtdUnit& unit, const char* operation, DeviceNode* device,
		uint64 device_address) {
		const uint32 status = ReadRegister32(unit.registers,
			kVtdFaultStatusRegister);
		if (!status) return;
		const stduint record_index = (status >> 8) & 0xFF;
		const stduint record_offset =
			stduint((unit.capability >> 24) & 0x3FF) * 16 +
			record_index * 16;
		if (record_offset + 16 > PAGE_SIZE) {
			plogerro("[IOMMU] %s fault pci=%02X:%02X.%u iova=%[64H] fsts=%08X record=%X unavailable",
				operation, device->fields.pci_bus, device->fields.pci_device,
				device->fields.pci_function, device_address, status,
				record_offset);
			return;
		}
		const uint64 record_low = ReadRegister64(unit.registers,
			record_offset);
		const uint64 record_high = ReadRegister64(unit.registers,
			record_offset + 8);
		plogerro("[IOMMU] %s fault pci=%02X:%02X.%u iova=%[64H] fsts=%08X record=%[64H]/%[64H]",
			operation, device->fields.pci_bus, device->fields.pci_device,
			device->fields.pci_function, device_address, status,
			record_low, record_high);
	}

	uint64* AllocateDomainTable(IovaDomain& domain) {
		if (domain.table_count >= IovaTableCapacity) return nullptr;
		auto table = reinterpret_cast<uint64*>(mempool.allocate(PAGE_SIZE, 12));
		if (!table) return nullptr;
		const uint64 physical = uint64(stduint(table));
		if (!PhysicalRangeFits(physical, PAGE_SIZE,
			vtd_state.host_address_width)) {
			mempool.deallocate(table, PAGE_SIZE);
			return nullptr;
		}
		MemSet(table, 0, PAGE_SIZE);
		domain.tables[domain.table_count++] = table;
		return table;
	}

	void ReleaseDomainTables(IovaDomain& domain) {
		for0(i, domain.table_count) {
			mempool.deallocate(domain.tables[i], PAGE_SIZE);
		}
		domain = {};
	}

	IovaDomain* FindDomain(DeviceNode* device) {
		for0(i, IovaDomainCapacity) {
			if (iova_domains[i].active &&
				iova_domains[i].device == device) return &iova_domains[i];
		}
		return nullptr;
	}

	IovaDomain* CreateDomain(VtdUnit& unit, DeviceNode* device) {
		for0(i, IovaDomainCapacity) {
			auto& domain = iova_domains[i];
			if (domain.active) continue;
			domain = {};
			domain.active = true;
			domain.device = device;
			domain.unit = &unit;
			domain.domain_id = uint16(VtdFirstTranslatedDomain + i);
			domain.root = AllocateDomainTable(domain);
			if (!domain.root) {
				domain = {};
				return nullptr;
			}
			return &domain;
		}
		return nullptr;
	}

	uint64* GetOrCreateNextTable(IovaDomain& domain, uint64* table,
		stduint index) {
		if (table[index] & (VtdPageRead | VtdPageWrite)) {
			return reinterpret_cast<uint64*>(stduint(
				table[index] & VtdPageAddressMask));
		}
		auto next = AllocateDomainTable(domain);
		if (!next) return nullptr;
		StoreFence();
		table[index] = uint64(stduint(next)) | VtdPageRead | VtdPageWrite;
		return next;
	}

	uint64* GetLeafEntry(IovaDomain& domain, uint64 device_address,
		bool create) {
		uint64* table = domain.root;
		const stduint indices[4] = {
			stduint((device_address >> 39) & 0x1FF),
			stduint((device_address >> 30) & 0x1FF),
			stduint((device_address >> 21) & 0x1FF),
			stduint((device_address >> 12) & 0x1FF),
		};
		for0(level, 3) {
			if (!(table[indices[level]] & (VtdPageRead | VtdPageWrite))) {
				if (!create) return nullptr;
				table = GetOrCreateNextTable(domain, table,
					indices[level]);
			}
			else {
				table = reinterpret_cast<uint64*>(stduint(
					table[indices[level]] & VtdPageAddressMask));
			}
			if (!table) return nullptr;
		}
		return &table[indices[3]];
	}

	bool RangesOverlap(uint64 left, stduint left_length,
		uint64 right, stduint right_length) {
		return left < right + right_length && right < left + left_length;
	}

	bool AllocateIovaRange(IovaDomain& domain, stduint length,
		uint64 preferred_address, uint64& device_address,
		IovaRange*& mapping) {
		mapping = nullptr;
		for0(i, IovaMappingCapacity) {
			if (!domain.mappings[i].active && !mapping)
				mapping = &domain.mappings[i];
		}
		if (!mapping) return false;
		if (preferred_address) {
			if ((preferred_address & (PAGE_SIZE - 1)) ||
				preferred_address + length < preferred_address ||
				preferred_address + length > (uint64(1) << 48)) return false;
			for0(i, IovaMappingCapacity) {
				const auto& current = domain.mappings[i];
				if (current.active && RangesOverlap(preferred_address, length,
					current.device_address, current.length)) return false;
			}
			device_address = preferred_address;
			return true;
		}

		uint64 candidate = IovaStartAddress;
		for0(pass, IovaMappingCapacity + 1) {
			bool collision = false;
			for0(i, IovaMappingCapacity) {
				const auto& current = domain.mappings[i];
				if (!current.active || !RangesOverlap(candidate, length,
					current.device_address, current.length)) continue;
				candidate = current.device_address + current.length;
				candidate = (candidate + PAGE_SIZE - 1) &
					~uint64(PAGE_SIZE - 1);
				collision = true;
				break;
			}
			if (!collision) {
				if (candidate + length < candidate ||
					candidate + length > (uint64(1) << 48)) return false;
				device_address = candidate;
				return true;
			}
		}
		return false;
	}

	bool PopulateIovaPages(IovaDomain& domain, uint64 device_address,
		uint64 physical, stduint length) {
		for (stduint offset = 0; offset < length; offset += PAGE_SIZE) {
			auto entry = GetLeafEntry(domain, device_address + offset, true);
			if (!entry || (*entry & (VtdPageRead | VtdPageWrite))) {
				for (stduint undo = 0; undo < offset; undo += PAGE_SIZE) {
					auto previous = GetLeafEntry(domain,
						device_address + undo, false);
					if (previous) *previous = 0;
				}
				return false;
			}
			*entry = (physical + offset) & VtdPageAddressMask;
			*entry |= VtdPageRead | VtdPageWrite;
		}
		StoreFence();
		return true;
	}

	// A fresh domain contains only the reserved ranges accumulated here.
	bool ReserveIovaRange(IovaDomain& domain, uint64 begin, uint64 end) {
		IovaRange* slot = nullptr;
		for (auto& current : domain.mappings) {
			if (current.active) {
				const uint64 current_end = current.device_address + current.length;
				if (end < current.device_address || begin > current_end) continue;
				begin = minof(begin, current.device_address);
				end = maxof(end, current_end);
				current = {};
			}
			if (!slot) slot = &current;
		}
		if (!slot || end - begin > ~stduint(0)) return false;
		*slot = {true, true, begin, begin, stduint(end - begin)};
		return true;
	}

	bool MapReservedMemory(IovaDomain& domain) {
		const auto& unit = *domain.unit;
		const auto& pci = domain.device->fields;
		const uint16 requester = (uint16(pci.pci_bus) << 8) |
			(uint16(pci.pci_device) << 3) | pci.pci_function;
		for0(i, unit.rmrr_route_count) {
			const auto& route = unit.rmrr_routes[i];
			const bool downstream = route.secondary_bus &&
				pci.pci_bus >= route.secondary_bus && pci.pci_bus <= route.subordinate_bus;
			if (!downstream && requester != route.requester) continue;
			const uint64 begin = route.base;
			const uint64 end = route.limit + 1;
			// Intel VT-d specification, sections 3.16 and 8.4: reserved ranges are identity-mapped.
			if (begin >= end || ((begin | end) & (PAGE_SIZE - 1)) ||
				end > (uint64(1) << 48) || end - begin > ~stduint(0) ||
				!PhysicalRangeFits(begin, stduint(end - begin), vtd_state.host_address_width) ||
				!Memory::reserved_regions.Covers(begin, end) ||
				!ReserveIovaRange(domain, begin, end)) return false;
		}
		for (const auto& range : domain.mappings) {
			if (range.reserved && !PopulateIovaPages(domain, range.device_address,
				range.physical, range.length)) return false;
		}
		return true;
	}

	void ClearIovaPages(IovaDomain& domain, uint64 device_address,
		stduint length) {
		for (stduint offset = 0; offset < length; offset += PAGE_SIZE) {
			auto entry = GetLeafEntry(domain, device_address + offset, false);
			if (entry) *entry = 0;
		}
		StoreFence();
	}

	// The context entry and both invalidations must refer to the same DRHD.
	bool RestorePassthroughContext(VtdUnit& unit, VtdContextEntry& context) {
		context.low = 0;
		StoreFence();
		(void)InvalidateVtdCaches(unit);
		context.high = uint64(VtdPassthroughDomain) << 8 |
			VtdAddressWidth48;
		StoreFence();
		context.low = VtdContextPassthrough | VtdContextPresent;
		StoreFence();
		return InvalidateVtdCaches(unit);
	}

	bool AttachDomain(IovaDomain& domain) {
		auto& unit = *domain.unit;
		auto device = domain.device;
		if (!device || device->fields.node_type !=
			uint16(DeviceNodeType::PciDevice) ||
			device->fields.pci_segment != unit.descriptor->segment ||
			device->fields.pci_device >= 32 ||
			device->fields.pci_function >= 8) return false;
		auto context_table = GetContextTable(unit.root_table,
			device->fields.pci_bus);
		if (!context_table) return false;
		const stduint requester = stduint(device->fields.pci_device) * 8 +
			device->fields.pci_function;
		auto& context = context_table[requester];
		if (!(context.low & VtdContextPresent)) return false;

		context.low = 0;
		StoreFence();
		if (!InvalidateVtdCaches(unit)) {
			(void)RestorePassthroughContext(unit, context);
			return false;
		}
		context.high = uint64(domain.domain_id) << 8 | VtdAddressWidth48;
		StoreFence();
		context.low = uint64(stduint(domain.root)) | VtdContextPresent;
		StoreFence();
		if (!InvalidateVtdCaches(unit)) {
			domain.restore_failed = !RestorePassthroughContext(unit, context);
			return false;
		}
		domain.attached = true;
		plogtrac("[IOMMU] Translated domain attached pci=%02X:%02X.%u did=%u root=%[64H]",
			device->fields.pci_bus, device->fields.pci_device,
			device->fields.pci_function, domain.domain_id,
			uint64(stduint(domain.root)));
		return true;
	}
}

bool Iova::ProbeVtd(stduint dmar_address) {
	// A published table can still be live even if the enable command timed out.
	for0(i, vtd_state.unit_count) {
		const auto& unit = vtd_state.units[i];
		if (unit.root_table || unit.interrupt_remapping_table) return false;
	}
	vtd_state = {};
	MemSet(iova_domains, 0, sizeof(iova_domains));
	if (!dmar_address) {
		ploginfo("[IOMMU] DMAR unavailable; VT-d diagnostics disabled");
		return false;
	}
	auto table = reinterpret_cast<const DmarTable*>(dmar_address);
	if (table->header.length < sizeof(DmarTable) ||
		!table->header.isValid("DMAR")) {
		plogwarn("[IOMMU] Invalid DMAR table at %[x] length=%u",
			dmar_address, table->header.length);
		return false;
	}
	ploginfo("[IOMMU] DMAR at %[x] length=%u revision=%u host-width=%u flags=%02X",
		dmar_address, table->header.length, table->header.revision,
		unsigned(table->host_address_width) + 1, table->flags);
	const uint8* entries = reinterpret_cast<const uint8*>(table) + sizeof(DmarTable);
	const uint8* cursor = entries;
	const uint8* end = reinterpret_cast<const uint8*>(table) +
		table->header.length;
	stduint hardware_unit_count = 0;
	stduint reserved_memory_count = 0;
	stduint rmrr_route_capacity = 0;
	// Validate entry lengths before allocating an exact-sized array or probing any hardware.
	while (cursor < end) {
		const stduint remaining = stduint(end - cursor);
		if (remaining < sizeof(DmarEntryHeader)) {
			plogwarn("[IOMMU] DMAR has a truncated entry header");
			return false;
		}
		auto header = reinterpret_cast<const DmarEntryHeader*>(cursor);
		if (header->length < sizeof(DmarEntryHeader) ||
			header->length > remaining) {
			plogwarn("[IOMMU] DMAR entry type=%u has invalid length=%u remaining=%u",
				header->type, header->length, remaining);
			return false;
		}
		if (header->type == kDmarHardwareUnit) {
			if (header->length < sizeof(DmarHardwareUnit)) {
				plogwarn("[IOMMU] DRHD%u is truncated length=%u",
					hardware_unit_count, header->length);
				return false;
			}
			++hardware_unit_count;
		}
		else if (header->type == kDmarReservedMemory) {
			if (header->length < sizeof(DmarReservedMemory)) {
				plogwarn("[IOMMU] RMRR%u is truncated length=%u",
					reserved_memory_count, header->length);
				return false;
			}
			++reserved_memory_count;
			rmrr_route_capacity += (header->length - sizeof(DmarReservedMemory)) /
				(sizeof(DmarDeviceScope) + sizeof(DmarPciPath));
		}
		cursor += header->length;
	}
	if (!hardware_unit_count) {
		plogwarn("[IOMMU] DMAR contains no DRHD units");
		return false;
	}
	if (hardware_unit_count > __PTRDIFF_MAX__ / sizeof(VtdUnit)) return false;
	uni::UniquePtr<VtdUnit[]> units(new (std::nothrow_t{}) VtdUnit[hardware_unit_count]{});
	if (!units) {
		plogwarn("[IOMMU] Cannot allocate state for %u DRHD units", hardware_unit_count);
		return false;
	}

	// Keep probed state private until all entries have been read successfully.
	stduint unit_index = 0;
	stduint rmrr_index = 0;
	bool probe_ok = true;
	for (cursor = entries; cursor < end;) {
		const auto* header = reinterpret_cast<const DmarEntryHeader*>(cursor);
		if (header->type == kDmarHardwareUnit) {
			const auto& descriptor = *reinterpret_cast<const DmarHardwareUnit*>(cursor);
			if (!ProbeHardwareUnit(units[unit_index], descriptor, unit_index)) probe_ok = false;
			++unit_index;
		}
		else if (header->type == kDmarReservedMemory) {
			const auto& range = *reinterpret_cast<const DmarReservedMemory*>(cursor);
			if (!ProbeReservedMemory(range, rmrr_index++)) probe_ok = false;
		}
		else {
			ploginfo("[IOMMU] DMAR entry type=%u length=%u", header->type, header->length);
		}
		cursor += header->length;
	}
	if (!probe_ok) {
		plogwarn("[IOMMU] Read-only VT-d probe incomplete: drhd=%u rmrr=%u",
			hardware_unit_count, reserved_memory_count);
		return false;
	}
	vtd_state.dmar_table = table;
	vtd_state.dmar_flags = table->flags;
	vtd_state.host_address_width = table->host_address_width + 1;
	vtd_state.rmrr_route_capacity = rmrr_route_capacity;
	vtd_state.units.swap(units);
	vtd_state.unit_count = hardware_unit_count;
	vtd_state.probe_complete = true;
	plogtrac("[IOMMU] Read-only VT-d probe complete: drhd=%u rmrr=%u",
		hardware_unit_count, reserved_memory_count);
	return true;
}

bool Iova::EnableVtdPassthrough(DeviceNode* pci_root) {
	auto* selected = SingleVtdUnit();
	plogtrac("[IOMMU] Pass-through request probed=%u enabled=%u drhd=%u root=%[x]",
		unsigned(vtd_state.probe_complete), unsigned(selected && selected->passthrough_enabled),
		vtd_state.unit_count, stduint(pci_root));
	if (!vtd_state.probe_complete) return true;
	if (selected && selected->passthrough_enabled) return true;
	if (selected && selected->root_table) {
		plogwarn("[IOMMU] Previous pass-through activation did not complete");
		return false;
	}
	if (!pci_root) {
		plogerro("[IOMMU] Cannot enable pass-through without PCI topology");
		return false;
	}
	// Resolve ownership for every probed unit before checking the current single-unit activation policy.
	PrepareDrhdOwnership(pci_root);
	if (!selected) {
		plogwarn("[IOMMU] Pass-through startup supports one include-all DRHD; drhd=%u include-all=%u",
			vtd_state.unit_count, unsigned(bool(vtd_state.units[0].descriptor->flags & kDmarIncludeAll)));
		return false;
	}
	auto& unit = *selected;
	if (!(unit.extended_capability & (uint64(1) << 6))) {
		plogwarn("[IOMMU] DRHD does not support pass-through translation");
		return false;
	}
	if (!(unit.extended_capability & 1)) {
		plogwarn("[IOMMU] Non-coherent page-table access is not supported yet");
		return false;
	}
	if (!(unit.capability & (uint64(1) << (8 + VtdAddressWidth48)))) {
		plogwarn("[IOMMU] DRHD does not support 48-bit adjusted address width");
		return false;
	}
	const stduint iotlb_offset = stduint(
		(unit.extended_capability >> 8) & 0x3FF) * 16;
	if (iotlb_offset + 16 > PAGE_SIZE) {
		plogwarn("[IOMMU] IOTLB register outside mapped window offset=%X",
			iotlb_offset);
		return false;
	}

	auto registers = unit.registers;
	const uint32 initial_status = ReadRegister32(registers,
		kVtdGlobalStatusRegister);
	const uint32 active_features = VtdGlobalStatusTranslationEnabled |
		VtdGlobalStatusQueuedInvalidation |
		VtdGlobalStatusInterruptRemapping;
	if (initial_status & active_features) {
		plogwarn("[IOMMU] Refusing to replace active VT-d state gsts=%08X",
			initial_status);
		return false;
	}
	const uint32 initial_faults = ReadRegister32(registers,
		kVtdFaultStatusRegister);
	if (initial_faults) {
		plogwarn("[IOMMU] Refusing to enable with pending faults fsts=%08X",
			initial_faults);
		return false;
	}

	auto root_table = reinterpret_cast<VtdRootEntry*>(
		mempool.allocate(PAGE_SIZE, 12));
	if (!root_table) {
		plogerro("[IOMMU] Failed to allocate root table");
		return false;
	}
	const uint64 root_physical = uint64(stduint(root_table));
	if (!PhysicalRangeFits(root_physical, PAGE_SIZE,
		vtd_state.host_address_width)) {
		plogerro("[IOMMU] Root table outside host address width addr=%[64H]",
			root_physical);
		mempool.deallocate(root_table, PAGE_SIZE);
		return false;
	}
	MemSet(root_table, 0, PAGE_SIZE);
	stduint context_table_count = 0;
	stduint device_count = 0;
	stduint xhci_extra_interrupts = 0;
	if (!PopulatePassthroughContexts(unit, pci_root, root_table,
		context_table_count, device_count, xhci_extra_interrupts) || !device_count) {
		plogerro("[IOMMU] Failed to build pass-through contexts devices=%u",
			device_count);
		ReleasePassthroughTables(root_table);
		return false;
	}
	unit.rmrr_routes_ready = PrepareRmrrRoutes(unit);
	if (!unit.rmrr_routes_ready) {
		unit.rmrr_routes.reset();
		unit.rmrr_route_count = 0;
		plogerro("[IOMMU] Could not prepare RMRR scopes for DMA translation");
	}
	StoreFence();

	// Once RTADDR is written, keep the root table if a later hardware command fails.
	WriteRegister64(registers, kVtdRootTableAddressRegister, root_physical);
	WriteRegister32(registers, VtdGlobalCommandRegister,
		VtdGlobalCommandSetRoot);
	if (!WaitRegister32(registers, kVtdGlobalStatusRegister,
		VtdGlobalStatusRootSet, VtdGlobalStatusRootSet)) {
		plogerro("[IOMMU] Set-root-table command timed out gsts=%08X",
			ReadRegister32(registers, kVtdGlobalStatusRegister));
		unit.root_table = root_table;
		return false;
	}
	const uint64 loaded_root = ReadRegister64(registers,
		kVtdRootTableAddressRegister) & VtdPageAddressMask;
	if (loaded_root != root_physical) {
		plogerro("[IOMMU] Root-table readback mismatch expected=%[64H] actual=%[64H]",
			root_physical, loaded_root);
		unit.root_table = root_table;
		return false;
	}
	if (!InvalidateVtdCaches(unit)) {
		unit.root_table = root_table;
		return false;
	}
	const uint32 invalidation_faults = ReadRegister32(registers,
		kVtdFaultStatusRegister);
	if (invalidation_faults) {
		plogerro("[IOMMU] VT-d fault during invalidation fsts=%08X",
			invalidation_faults);
		unit.root_table = root_table;
		return false;
	}
	WriteRegister32(registers, VtdGlobalCommandRegister,
		VtdGlobalCommandEnableTranslation);
	if (!WaitRegister32(registers, kVtdGlobalStatusRegister,
		VtdGlobalStatusTranslationEnabled,
		VtdGlobalStatusTranslationEnabled)) {
		plogerro("[IOMMU] Translation-enable command timed out gsts=%08X",
			ReadRegister32(registers, kVtdGlobalStatusRegister));
		unit.root_table = root_table;
		return false;
	}

	const uint32 final_faults = ReadRegister32(registers,
		kVtdFaultStatusRegister);
	if (final_faults) {
		WriteRegister32(registers, VtdGlobalCommandRegister, 0);
		WaitRegister32(registers, kVtdGlobalStatusRegister,
			VtdGlobalStatusTranslationEnabled, 0);
		plogerro("[IOMMU] VT-d fault after enabling pass-through fsts=%08X gsts=%08X",
			final_faults, ReadRegister32(registers,
				kVtdGlobalStatusRegister));
		unit.root_table = root_table;
		return false;
	}

	unit.root_table = root_table;
	unit.context_table_count = context_table_count;
	unit.device_count = device_count;
	unit.xhci_extra_interrupts = xhci_extra_interrupts;
	unit.passthrough_enabled = true;
	ploginfo("[IOMMU] Pass-through enabled root=%[64H] buses=%u devices=%u bytes=%u gsts=%08X fsts=%08X",
		root_physical, context_table_count, device_count,
		(context_table_count + 1) * PAGE_SIZE,
		ReadRegister32(registers, kVtdGlobalStatusRegister),
		ReadRegister32(registers, kVtdFaultStatusRegister));
	return true;
}

void Iova::EnableFaultEvents() {
	auto* selected = SingleVtdUnit();
	if (!selected || !selected->passthrough_enabled || !IC.getType()) return;
	auto& unit = *selected;
	auto& faults = unit.faults;
	const stduint entry = Handler::GeneratedEntry(Handler::VtdFaultVector);
	const uint32 lapic = GetCurrentLapicId();
	const bool extended = IC.getType() == 2 && unit.interrupt_remapping_enabled &&
		unit.extended_interrupt_mode &&
		(unit.extended_capability & VtdExtendedInterruptModeSupported);
	if (!entry || lapic == CORE_ID_INVALID || (!extended && (lapic >= 0xFFu ||
		(unit.extended_capability & VtdExtendedInterruptModeRequired)))) {
		plogwarn("[IOMMU] Fault event unavailable for vector=%u LAPIC=%u; using synchronous checks",
			unsigned(Handler::VtdFaultVector), lapic);
		return;
	}
	const stduint record_offset = stduint((unit.capability >> 24) & 0x3FF) * VtdFaultRecordSize;
	const uint16 record_count = uint16((unit.capability >> 40) & 0xFF) + 1;
	if (record_offset < kVtdRegisterWindowLength || record_offset >= PAGE_SIZE ||
		record_count > (PAGE_SIZE - record_offset) / VtdFaultRecordSize) {
		plogwarn("[IOMMU] Fault records outside mapped register page offset=%X count=%u",
			unsigned(record_offset), unsigned(record_count));
		return;
	}

	{
		SpinlockLocal guard(IovaLock());
		if (faults.enabled) return;
		if (!SetFaultEventMask(unit, true)) {
			plogwarn("[IOMMU] Cannot mask fault events during setup; using synchronous checks");
			return;
		}
		auto* regs = unit.registers;
		// Intel VT-d specification, section 5.1.6: unit events bypass interrupt remapping.
		const uint32 addr = 0xFEE00000u | ((lapic & 0xFFu) << 12);
		const uint32 upper_addr = extended ? lapic & 0xFFFFFF00u : 0;
		if (unit.extended_capability & VtdExtendedInterruptModeSupported)
			WriteRegister32(regs, VtdFaultEventUpperAddressRegister, upper_addr);
		WriteRegister32(regs, VtdFaultEventAddressRegister, addr);
		WriteRegister32(regs, VtdFaultEventDataRegister, Handler::VtdFaultVector);
		if (ReadRegister32(regs, VtdFaultEventAddressRegister) != addr ||
			ReadRegister32(regs, VtdFaultEventDataRegister) != Handler::VtdFaultVector ||
			((unit.extended_capability & VtdExtendedInterruptModeSupported) &&
			ReadRegister32(regs, VtdFaultEventUpperAddressRegister) != upper_addr)) {
			plogwarn("[IOMMU] Fault event register readback failed; using synchronous checks");
			return;
		}
		Handler::InstallDispatch(Handler::VtdFaultVector, entry, HandleVtdFaultEvent);
		faults.record_offset = uint16(record_offset);
		faults.record_count = record_count;
		faults.next_record = VtdFaultCursorUnset;
		faults.enabled = true;
		if (ReadFaultStatus(unit) & VtdFaultEventStatus)
			vtd_fault_work.store(1, uni::MemoryOrder_Release);
		else if (!SetFaultEventMask(unit, false)) {
			faults.enabled = false;
			plogwarn("[IOMMU] Fault event stayed masked; using synchronous checks");
			return;
		}
	}
	ploginfo("[IOMMU] Fault event ready vector=%u lapic=%u records=%u",
		unsigned(Handler::VtdFaultVector), lapic, unsigned(record_count));
}

void Iova::NotifyFaultService() {
	if (!vtd_fault_work.load(uni::MemoryOrder_Acquire)) return;
	byte expected = 0;
	if (!vtd_fault_event_posted.compare_exchange(expected, 1,
		uni::MemoryOrder_Acq_Rel, uni::MemoryOrder_Relaxed)) return;
	DeviceEvent event = {};
	event.kind = _IMM(DeviceEventKind::Fault);
	event.source = Handler::VtdFaultVector;
	event.count = 1;
	if (!device_event_proc(Task_Devsman, event))
		vtd_fault_event_posted.store(0, uni::MemoryOrder_Release);
}

void Iova::ServiceFaultEvents() {
	if (!vtd_fault_event_posted.load(uni::MemoryOrder_Acquire)) return;
	auto* selected = SingleVtdUnit();
	if (!selected) return;
	auto& unit = *selected;
	auto& faults = unit.faults;
	const stduint now = tick.load(uni::MemoryOrder_Relaxed);
	if (now - faults.report_tick >= CONFIG_SysTickFreq) {
		if (faults.suppressed)
			plogwarn("[IOMMU] Suppressed %u fault reports", faults.suppressed);
		faults.report_tick = now;
		faults.reports = 0;
		faults.suppressed = 0;
	}

	uint32 status;
	bool overflow;
	{
		SpinlockLocal guard(IovaLock());
		status = CaptureFaultRecords(unit, numsof(faults.records));
		overflow = faults.overflow;
		faults.overflow = false;
	}
	const uint32 qi_faults = status & VtdInvalidationQueueFaults;
	if (qi_faults && qi_faults != faults.last_qi_faults)
		plogerro("[IOMMU] Fault event: invalidation queue fsts=%08X", status);
	faults.last_qi_faults = qi_faults;

	if (overflow) {
		if (faults.reports < VtdFaultReportLimit) {
			++faults.reports;
			plogwarn("[IOMMU] Hardware fault records overflowed");
		}
		else ++faults.suppressed;
	}
	for0(i, numsof(faults.records)) {
		VtdFaultRecord record;
		{
			SpinlockLocal guard(IovaLock());
			if (!faults.count) break;
			record = faults.records[faults.head];
			faults.head = (faults.head + 1u) % numsof(faults.records);
			--faults.count;
		}
		if (faults.reports >= VtdFaultReportLimit) {
			++faults.suppressed;
			continue;
		}
		++faults.reports;
		const uint16 sid = uint16(record.detail);
		const uint8 reason = uint8(record.detail >> 32);
		const char* kind = reason >= 0x20u && reason <= 0x2Fu ? "interrupt-remap" : "translation";
		plogerro("[IOMMU] Fault event %s sid=%04X:%02X:%02X.%u reason=%02X info=%[64H] detail=%[64H]",
			kind, unsigned(unit.descriptor->segment), unsigned(sid >> 8), unsigned((sid >> 3) & 0x1F),
			unsigned(sid & 7u), unsigned(reason), record.info, record.detail);
	}

	bool rearm_failed;
	{
		SpinlockLocal guard(IovaLock());
		// Preserve QI errors for the invalidation wait path. A sticky error can suppress a new message.
		const bool pending = faults.count || (ReadFaultStatus(unit) & VtdFaultEventStatus);
		rearm_failed = !pending && !SetFaultEventMask(unit, false);
		if (rearm_failed) faults.enabled = false;
		vtd_fault_work.store(!rearm_failed && (pending || faults.suppressed), uni::MemoryOrder_Release);
	}
	vtd_fault_event_posted.store(0, uni::MemoryOrder_Release);
	if (rearm_failed)
		plogwarn("[IOMMU] Cannot rearm fault events; using synchronous checks");
}

bool Iova::InterruptRemappingAvailable() {
	const auto* unit = SingleVtdUnit();
	return unit && unit->interrupt_remapping_enabled;
}

static bool AllocateInterruptLocked(VtdUnit& unit, uint16 source_id, uint32 target_lapic_id,
	uint8 vector, uint16& handle) {
	handle = 0;
	if (!unit.interrupt_remapping_table || vector < 0x20 ||
		(!unit.extended_interrupt_mode && target_lapic_id > 0xFFu))
		return false;
	const stduint capacity = unit.interrupt_remapping_capacity;
	uni::Bitmap allocated(unit.interrupt_remapping_allocated.get(), capacity / _BYTE_BITS_);
	stduint index = 0;
	while (index < capacity && allocated[index]) ++index;
	if (index == capacity) {
		plogwarn("[IOMMU] Interrupt table exhausted entries=%u", unsigned(capacity));
		return false;
	}
	auto& entry = unit.interrupt_remapping_table[index];
	entry.high = uint64(source_id) | (uint64(1) << 18);
	uint64 destination = target_lapic_id;
	if (!unit.extended_interrupt_mode) destination <<= 8;
	entry.low = 1u | (uint64(1) << 3) | (uint64(vector) << 16) |
		(destination << 32);
	StoreFence();
	if (!SubmitInterruptCacheInvalidation(unit, uint16(index), 0, true)) {
		entry.low = 0;
		entry.high = 0;
		StoreFence();
		if (!SubmitInterruptCacheInvalidation(unit, uint16(index), 0, true))
			allocated.setof(index);
		plogerro("[IOMMU] Interrupt-entry invalidation failed index=%u",
			index);
		return false;
	}
	allocated.setof(index);
	handle = uint16(index + 1);
	return true;
}

bool Iova::AllocatePciInterrupt(DeviceNode* device, uint32 target_lapic_id,
	uint8 vector, InterruptMessage& message) {
	message = {};
	auto* unit = SingleVtdUnit();
	if (!unit || !unit->interrupt_remapping_enabled || !device ||
		DeviceNodeType(device->fields.node_type) != DeviceNodeType::PciDevice ||
		device->fields.pci_segment != unit->descriptor->segment) return false;
	SpinlockLocal guard(IovaLock());
	const uint16 source_id = (uint16(device->fields.pci_bus) << 8) |
		(uint16(device->fields.pci_device) << 3) | device->fields.pci_function;
	if (!AllocateInterruptLocked(*unit, source_id, target_lapic_id, vector, message.handle)) return false;
	const uint16 index = message.handle - 1u;
	message.address = 0xFEE00000u | (uint64(index & 0x7FFFu) << 5) |
		(uint64(index & 0x8000u) >> 13) | 0x18u;
	message.data = 0;
	return true;
}

static void FreeInterruptLocked(VtdUnit& unit, uint16 handle) {
	if (!handle || !unit.interrupt_remapping_table) return;
	const stduint index = handle - 1u;
	const stduint capacity = unit.interrupt_remapping_capacity;
	if (index >= capacity) return;
	uni::Bitmap allocated(unit.interrupt_remapping_allocated.get(), capacity / _BYTE_BITS_);
	if (!allocated[index]) return;
	auto& entry = unit.interrupt_remapping_table[index];
	entry.low = 0;
	entry.high = 0;
	StoreFence();
	if (!SubmitInterruptCacheInvalidation(unit, uint16(index), 0, true)) {
		plogerro("[IOMMU] Interrupt-entry release invalidation failed index=%u",
			index);
		return;
	}
	allocated.setof(index, false);
}

void Iova::ReleaseInterrupt(uint16 handle) {
	if (!handle) return;
	SpinlockLocal guard(IovaLock());
	ReleaseInterruptLocked(handle);
}

bool Iova::PrepareInterruptRemapping(bool extended, stduint reserved_irq_entries) {
	auto* unit = SingleVtdUnit();
	return unit && EnableVtdInterruptRemapping(*unit, extended, reserved_irq_entries);
}

bool Iova::CommitExtendedInterruptMode() {
	auto* unit = SingleVtdUnit();
	if (!unit || !unit->interrupt_remapping_table || !unit->extended_interrupt_mode ||
		!SetVtdFeature(*unit, VtdGlobalCommandEnableInterruptRemapping,
			VtdGlobalStatusInterruptRemapping, true)) return false;
	unit->interrupt_remapping_enabled = true;
	ploginfo("[IOMMU] Interrupt remapping enabled entries=%u mode=x2APIC",
		unsigned(unit->interrupt_remapping_capacity));
	return true;
}

bool Iova::DisableInterruptRemapping() {
	auto* unit = SingleVtdUnit();
	if (!unit || !unit->interrupt_remapping_table) return true;
	if (!DisableInterruptRemappingForRollback(*unit, unit->compatibility_interrupts_before))
		return false;
	ReleaseInterruptRemappingStorage(*unit);
	return true;
}

bool Iova::ExtendedInterruptMode() {
	const auto* unit = SingleVtdUnit();
	return unit && unit->interrupt_remapping_enabled && unit->extended_interrupt_mode;
}

bool Iova::IoApicInterruptSource(uint8 id, uint16& source) {
	const auto* unit = SingleVtdUnit();
	if (!unit) return false;
	const auto* descriptor = unit->descriptor;
	const auto* bytes = reinterpret_cast<const uint8*>(descriptor);
	bool found = false;
	// Intel VT-d, DMAR Device Scope: the last PCI path component supplies the requester ID.
	for (auto* cursor = bytes + sizeof(*descriptor); cursor < bytes + descriptor->header.length;) {
		const auto* scope = reinterpret_cast<const DmarDeviceScope*>(cursor);
		cursor += scope->length;
		if (scope->type != kDmarScopeIoApic || scope->enumeration_id != id) continue;
		if (found || !ResolveScopeRequester(descriptor->segment, *scope, source)) return false;
		found = true;
	}
	return found;
}

bool Iova::AllocateIoApicInterruptLocked(uint16 source, uint32 destination, uint8 vector, uint16& handle) {
	handle = 0;
	auto* unit = SingleVtdUnit();
	return unit && AllocateInterruptLocked(*unit, source, destination, vector, handle);
}

void Iova::ReleaseInterruptLocked(uint16 handle) {
	if (auto* unit = SingleVtdUnit()) FreeInterruptLocked(*unit, handle);
}

static bool DisablePciMessages(const DeviceNode* device, bool clear_upper_address) {
	if (!device) return false;
	uni::PCI::Device dev{};
	dev.segment_group = device->fields.pci_segment;
	dev.bus = device->fields.pci_bus;
	dev.device = device->fields.pci_device;
	dev.function = device->fields.pci_function;
	uint8 cap = uint8(uni::PCI::read_config_register(dev, 0x34));
	const uni::PCI pci{};
	for (uint8 count = 0; count < 48 && cap >= 0x40 && !(cap & 3u); ++count) {
		const uint32 value = uni::PCI::read_config_register(dev, cap);
		const uint8 id = uint8(value);
		if (id == pci.kCapabilityMSI || id == pci.kCapabilityMSIX) {
			const uint32 enable = id == pci.kCapabilityMSI ? (1u << 16) : (1u << 31);
			uni::PCI::write_config_register(dev, cap, value & ~enable);
			if (uni::PCI::read_config_register(dev, cap) & enable) return false;
			// Intel VT-d remappable MSI messages use a zero upper address DWORD.
			if (clear_upper_address && id == pci.kCapabilityMSI && (value & (1u << 23)))
				uni::PCI::write_config_register(dev, cap + 8, 0);
		}
		cap = uint8(value >> 8);
	}
	return cap == 0;
}

bool Iova::DisablePciMessageInterrupts(const DeviceNode* device) {
	return DisablePciMessages(device, false);
}

bool Iova::PreparePciMessageInterrupts(const DeviceNode* device) {
	return DisablePciMessages(device, true);
}

bool Iova::MapDeviceDma(DeviceNode* device, stduint physical,
	stduint length, DmaMappingFlag flags, DmaMapping& mapping) {
	mapping.device_address = uint64(physical);
	mapping.translated = false;
	const uint32 known_flags = _IMM(DmaMappingFlag::Identity);
	if (_IMM(flags) & ~known_flags) return false;
	if (!device || !physical || !length ||
		(physical & (PAGE_SIZE - 1)) || (length & (PAGE_SIZE - 1)))
		return false;
	auto* selected = SingleVtdUnit();
	if (!selected || !selected->passthrough_enabled) return true;
	if (device->fields.node_type != uint16(DeviceNodeType::PciDevice))
		return true;
	if (!PhysicalRangeFits(uint64(physical), length,
		vtd_state.host_address_width)) return false;

	if (!selected->rmrr_routes_ready || device->fields.pci_segment != selected->descriptor->segment)
		return false;

	SpinlockLocal guard(IovaLock());
	auto domain = FindDomain(device);
	const bool new_domain = !domain;
	if (!domain) domain = CreateDomain(*selected, device);
	if (!domain) {
		plogerro("[IOMMU] No translated domain available pci=%02X:%02X.%u",
			device->fields.pci_bus, device->fields.pci_device,
			device->fields.pci_function);
		return false;
	}
	if (domain->restore_failed) return false;
	auto& unit = *domain->unit;
	if (new_domain && !MapReservedMemory(*domain)) {
		plogerro("[IOMMU] RMRR mapping failed pci=%02X:%02X.%u",
			device->fields.pci_bus, device->fields.pci_device,
			device->fields.pci_function);
		ReleaseDomainTables(*domain);
		return false;
	}
	IovaRange* range = nullptr;
	uint64 device_address = 0;
	const uint64 preferred_address = (_IMM(flags) &
		_IMM(DmaMappingFlag::Identity)) ? uint64(physical) : 0;
	if (!AllocateIovaRange(*domain, length, preferred_address,
		device_address, range)) {
		plogerro("[IOMMU] Failed to map DMA pci=%02X:%02X.%u phys=%[64H] bytes=%u",
			device->fields.pci_bus, device->fields.pci_device,
			device->fields.pci_function, uint64(physical), length);
		if (new_domain) ReleaseDomainTables(*domain);
		return false;
	}
	if (new_domain && !AttachDomain(*domain)) {
		plogerro("[IOMMU] Failed to attach translated domain pci=%02X:%02X.%u",
			device->fields.pci_bus, device->fields.pci_device,
			device->fields.pci_function);
		// Keep tables alive if hardware may still reference the translated context.
		if (!domain->restore_failed) ReleaseDomainTables(*domain);
		return false;
	}
	if (!PopulateIovaPages(*domain, device_address, physical, length)) {
		plogerro("[IOMMU] Failed to map DMA pages pci=%02X:%02X.%u phys=%[64H] bytes=%u",
			device->fields.pci_bus, device->fields.pci_device,
			device->fields.pci_function, uint64(physical), length);
		return false;
	}
	if (!InvalidateVtdCaches(unit)) {
		ClearIovaPages(*domain, device_address, length);
		return false;
	}
	// Primary faults are unit-wide and may belong to another request or device.
	// The asynchronous handler owns them when fault events are enabled.
	const uint32 faults = ReadFaultStatus(unit);
	if (unit.faults.enabled ? (faults & VtdInvalidationQueueFaults) : faults) {
		if (!unit.faults.enabled) LogVtdFaultState(unit, "DMA map", device, device_address);
		ClearIovaPages(*domain, device_address, length);
		InvalidateVtdCaches(unit);
		plogerro("[IOMMU] Fault after DMA map pci=%02X:%02X.%u fsts=%08X",
			device->fields.pci_bus, device->fields.pci_device,
			device->fields.pci_function, faults);
		return false;
	}
	*range = {true, false, device_address, physical, length};
	mapping.device_address = device_address;
	mapping.translated = true;
	plogtrac("[IOMMU] DMA mapped pci=%02X:%02X.%u iova=%[64H] phys=%[64H] bytes=%u",
		device->fields.pci_bus, device->fields.pci_device,
		device->fields.pci_function, device_address, uint64(physical), length);
	return true;
}

bool Iova::UnmapDeviceDma(DeviceNode* device, uint64 device_address,
	stduint length) {
	if (!device || !device_address || !length ||
		(device_address & (PAGE_SIZE - 1)) || (length & (PAGE_SIZE - 1)))
		return false;
	const auto* selected = SingleVtdUnit();
	if (!selected || !selected->passthrough_enabled) return true;

	SpinlockLocal guard(IovaLock());
	auto domain = FindDomain(device);
	if (!domain || !domain->attached) return false;
	auto& unit = *domain->unit;
	IovaRange* range = nullptr;
	for0(i, IovaMappingCapacity) {
		auto& current = domain->mappings[i];
		if (current.active && !current.reserved &&
			current.device_address == device_address && current.length == length) {
			range = &current;
			break;
		}
	}
	if (!range) return false;
	if (!unit.faults.enabled) LogVtdFaultState(unit, "DMA unmap", device, device_address);
	ClearIovaPages(*domain, device_address, length);
	if (!InvalidateVtdCaches(unit)) {
		PopulateIovaPages(*domain, device_address, range->physical, length);
		InvalidateVtdCaches(unit);
		return false;
	}
	*range = {};
	return true;
}

#endif
