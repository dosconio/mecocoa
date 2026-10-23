// ASCII g++ TAB4 LF
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <cpp/Device/ACPI.hpp>

#if (_MCCA & 0xFF00) == 0x8600

namespace {
	static constexpr uint16 kDmarHardwareUnit = 0;
	static constexpr uint16 kDmarReservedMemory = 1;
	static constexpr uint8 kDmarIncludeAll = 1;

	static constexpr stduint kVtdVersionRegister = 0x00;
	static constexpr stduint kVtdCapabilityRegister = 0x08;
	static constexpr stduint kVtdExtendedCapabilityRegister = 0x10;
	static constexpr stduint VtdGlobalCommandRegister = 0x18;
	static constexpr stduint kVtdGlobalStatusRegister = 0x1C;
	static constexpr stduint kVtdRootTableAddressRegister = 0x20;
	static constexpr stduint VtdContextCommandRegister = 0x28;
	static constexpr stduint kVtdFaultStatusRegister = 0x34;
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
	static constexpr stduint VtdInterruptRemappingTableCapacity = 256;
	static constexpr uint8 VtdInterruptRemappingTableSize = 7;
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

	struct IovaRange {
		bool active;
		uint64 device_address;
		uint64 physical;
		stduint length;
	};

	struct IovaDomain {
		bool active;
		bool attached;
		DeviceNode* device;
		uint16 domain_id;
		uint64* root;
		void* tables[IovaTableCapacity];
		stduint table_count;
		IovaRange mappings[IovaMappingCapacity];
	};

	struct VtdState {
		bool probe_complete;
		bool passthrough_enabled;
		bool interrupt_remapping_enabled;
		bool extended_interrupt_mode;
		bool include_all;
		uint8 dmar_flags;
		uint8 host_address_width;
		uint16 segment;
		stduint hardware_unit_count;
		volatile uint8* registers;
		uint64 capability;
		uint64 extended_capability;
		VtdRootEntry* root_table;
		VtdInterruptRemappingEntry* interrupt_remapping_table;
		void* invalidation_queue;
		uint32* invalidation_completion;
		stduint invalidation_queue_length;
		uint8 invalidation_queue_shift;
		bool interrupt_remapping_allocated[VtdInterruptRemappingTableCapacity];
		stduint context_table_count;
		stduint device_count;
	};

	static_assert(sizeof(DmarTable) == 48);
	static_assert(sizeof(DmarHardwareUnit) == 16);
	static_assert(sizeof(DmarReservedMemory) == 24);
	static_assert(sizeof(DmarDeviceScope) == 6);
	static_assert(sizeof(DmarPciPath) == 2);
	static_assert(sizeof(VtdRootEntry) == 16);
	static_assert(sizeof(VtdContextEntry) == 16);
	static_assert(sizeof(VtdInterruptRemappingEntry) == 16);

	VtdState vtd_state{};
	IovaDomain iova_domains[IovaDomainCapacity] = {};
	alignas(Spinlock) byte iova_lock_raw[sizeof(Spinlock)] = {};

	Spinlock* IovaLock() {
		return reinterpret_cast<Spinlock*>(iova_lock_raw);
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

	bool SetVtdFeature(uint32 command_bit, uint32 status_bit, bool enabled) {
		auto registers = vtd_state.registers;
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

	bool SubmitQueuedInvalidation(uint64 descriptor_low,
		uint64 descriptor_high = 0) {
		if (!vtd_state.invalidation_queue ||
			!vtd_state.invalidation_completion ||
			!(ReadRegister32(vtd_state.registers, kVtdGlobalStatusRegister) &
			VtdGlobalStatusQueuedInvalidation)) return false;
		const stduint descriptor_length = stduint(1) <<
			vtd_state.invalidation_queue_shift;
		const uint32 queue_mask = uint32(vtd_state.invalidation_queue_length - 1);
		const uint32 tail = ReadRegister32(vtd_state.registers,
			VtdInvalidationQueueTailRegister) & queue_mask;
		if ((tail & (descriptor_length - 1)) ||
			tail + descriptor_length > vtd_state.invalidation_queue_length)
			return false;
		const uint32 wait_tail =
			(tail + uint32(descriptor_length)) & queue_mask;
		const uint32 next =
			(wait_tail + uint32(descriptor_length)) & queue_mask;
		const uint32 head = ReadRegister32(vtd_state.registers,
			VtdInvalidationQueueHeadRegister) & queue_mask;
		if (wait_tail == head || next == head) return false;
		auto* descriptor = reinterpret_cast<uint64*>(
			reinterpret_cast<uint8*>(vtd_state.invalidation_queue) + tail);
		MemSet(descriptor, 0, descriptor_length);
		descriptor[0] = descriptor_low;
		descriptor[1] = descriptor_high;
		auto* wait_descriptor = reinterpret_cast<uint64*>(
			reinterpret_cast<uint8*>(vtd_state.invalidation_queue) + wait_tail);
		MemSet(wait_descriptor, 0, descriptor_length);
		*vtd_state.invalidation_completion = VtdInvalidationStatusPending;
		wait_descriptor[0] = VtdInvalidationWaitType |
			VtdInvalidationWaitStatusWrite |
			(uint64(VtdInvalidationStatusComplete) << 32);
		wait_descriptor[1] = uint64(stduint(vtd_state.invalidation_completion));
		StoreFence();
		WriteRegister32(vtd_state.registers,
			VtdInvalidationQueueTailRegister, next);
		for0(spin, VtdCommandTimeoutSpins) {
			const uint32 faults = ReadRegister32(vtd_state.registers,
				kVtdFaultStatusRegister);
			if (faults & VtdInvalidationQueueFaults) return false;
			if (*reinterpret_cast<volatile uint32*>(
				vtd_state.invalidation_completion) ==
				VtdInvalidationStatusComplete) return true;
			_ASM volatile ("pause" ::: "memory");
		}
		return false;
	}

	bool SubmitInterruptCacheInvalidation(uint16 index = 0,
		uint8 mask = 0, bool selective = false) {
		uint64 descriptor = VtdInterruptCacheInvalidationType;
		if (selective) {
			descriptor |= VtdInterruptCacheInvalidationSelective;
			descriptor |= uint64(mask & 0x1Fu) << 27;
			descriptor |= uint64(index) << 32;
		}
		return SubmitQueuedInvalidation(descriptor);
	}

	void ReleaseInterruptRemappingStorage() {
		if (vtd_state.invalidation_queue) {
			mempool.deallocate(vtd_state.invalidation_queue,
				vtd_state.invalidation_queue_length);
			vtd_state.invalidation_queue = nullptr;
			vtd_state.invalidation_queue_length = 0;
		}
		if (vtd_state.invalidation_completion) {
			mempool.deallocate(vtd_state.invalidation_completion, PAGE_SIZE);
			vtd_state.invalidation_completion = nullptr;
		}
		if (vtd_state.interrupt_remapping_table) {
			mempool.deallocate(vtd_state.interrupt_remapping_table, PAGE_SIZE);
			vtd_state.interrupt_remapping_table = nullptr;
		}
		MemSet(vtd_state.interrupt_remapping_allocated, 0,
			sizeof(vtd_state.interrupt_remapping_allocated));
		vtd_state.interrupt_remapping_enabled = false;
		vtd_state.extended_interrupt_mode = false;
	}

	bool DisableInterruptRemappingForRollback(bool compatibility_enabled_before) {
		const uint32 status = ReadRegister32(vtd_state.registers,
			kVtdGlobalStatusRegister);
		if (status & VtdGlobalStatusInterruptRemapping) {
			if (!SetVtdFeature(VtdGlobalCommandEnableInterruptRemapping,
				VtdGlobalStatusInterruptRemapping, false)) return false;
		}
		if (ReadRegister32(vtd_state.registers, kVtdGlobalStatusRegister) &
			VtdGlobalStatusQueuedInvalidation) {
			if (!SetVtdFeature(VtdGlobalCommandEnableQueuedInvalidation,
				VtdGlobalStatusQueuedInvalidation, false)) return false;
		}
		const bool compatibility_enabled =
			(ReadRegister32(vtd_state.registers, kVtdGlobalStatusRegister) &
			VtdGlobalStatusCompatibilityInterrupts) != 0;
		if (compatibility_enabled != compatibility_enabled_before &&
			!SetVtdFeature(VtdGlobalCommandCompatibilityInterrupts,
				VtdGlobalStatusCompatibilityInterrupts,
				compatibility_enabled_before)) return false;
		return true;
	}

	bool EnableVtdInterruptRemapping() {
		if (!vtd_state.probe_complete || !vtd_state.passthrough_enabled)
			return false;
		if (vtd_state.interrupt_remapping_enabled) return true;
		if (vtd_state.invalidation_queue ||
			vtd_state.interrupt_remapping_table) return false;
		if (!(vtd_state.dmar_flags & 1u) ||
			!(vtd_state.extended_capability & (uint64(1) << 3)) ||
			!(vtd_state.extended_capability & (uint64(1) << 1)) ||
			!(vtd_state.extended_capability & 1u)) {
			plogtrac("[IOMMU] Interrupt remapping unavailable flags=%02X ecap=%[64H]",
				vtd_state.dmar_flags, vtd_state.extended_capability);
			return false;
		}
		// EIME rejects every compatibility-format MSI and I/O APIC message.
		// Keep xAPIC-format remapping until all kernel and I/O APIC sources use IRTEs.
		const bool extended_mode = false;
		if (extended_mode && (vtd_state.dmar_flags & (1u << 1))) {
			plogwarn("[IOMMU] Firmware opted out of x2APIC interrupt remapping");
			return false;
		}
		if (extended_mode &&
			!(vtd_state.extended_capability & (uint64(1) << 4))) {
			plogwarn("[IOMMU] Interrupt remapping lacks extended interrupt mode");
			return false;
		}
		const bool scalable_queue =
			(vtd_state.extended_capability & (uint64(1) << 43)) != 0;
		const bool compatibility_enabled_before =
			(ReadRegister32(vtd_state.registers, kVtdGlobalStatusRegister) &
			VtdGlobalStatusCompatibilityInterrupts) != 0;
		const uint8 queue_shift = scalable_queue ? 5 : 4;
		const stduint queue_length = VtdInvalidationQueueLength << queue_shift;
		auto* queue = mempool.allocate(queue_length, 12);
		auto* completion = reinterpret_cast<uint32*>(
			mempool.allocate(PAGE_SIZE, 12));
		auto* table = reinterpret_cast<VtdInterruptRemappingEntry*>(
			mempool.allocate(PAGE_SIZE, 12));
		if (!queue || !completion || !table ||
			!PhysicalRangeFits(uint64(stduint(queue)), queue_length,
				vtd_state.host_address_width) ||
			!PhysicalRangeFits(uint64(stduint(completion)), PAGE_SIZE,
				vtd_state.host_address_width) ||
			!PhysicalRangeFits(uint64(stduint(table)), PAGE_SIZE,
				vtd_state.host_address_width)) {
			if (queue) mempool.deallocate(queue, queue_length);
			if (completion) mempool.deallocate(completion, PAGE_SIZE);
			if (table) mempool.deallocate(table, PAGE_SIZE);
			plogerro("[IOMMU] Failed to allocate interrupt-remapping storage");
			return false;
		}
		MemSet(queue, 0, queue_length);
		MemSet(completion, 0, PAGE_SIZE);
		MemSet(table, 0, PAGE_SIZE);
		StoreFence();

		vtd_state.invalidation_queue = queue;
		vtd_state.invalidation_completion = completion;
		vtd_state.invalidation_queue_length = queue_length;
		vtd_state.invalidation_queue_shift = queue_shift;
		vtd_state.interrupt_remapping_table = table;
		WriteRegister32(vtd_state.registers,
			VtdInvalidationQueueTailRegister, 0);
		uint64 queue_address = uint64(stduint(queue));
		if (scalable_queue) queue_address |= uint64(1) << 11;
		WriteRegister64(vtd_state.registers,
			VtdInvalidationQueueAddressRegister, queue_address);
		if (!SetVtdFeature(VtdGlobalCommandEnableQueuedInvalidation,
			VtdGlobalStatusQueuedInvalidation, true)) {
			plogerro("[IOMMU] Queued invalidation enable timed out");
			if (DisableInterruptRemappingForRollback(
				compatibility_enabled_before))
				ReleaseInterruptRemappingStorage();
			return false;
		}

		uint64 table_address = uint64(stduint(table)) |
			VtdInterruptRemappingTableSize;
		if (extended_mode) table_address |= VtdInterruptRemappingExtendedMode;
		WriteRegister64(vtd_state.registers,
			VtdInterruptRemappingTableAddressRegister, table_address);
		uint32 command = VtdPersistentCommand(ReadRegister32(vtd_state.registers,
			kVtdGlobalStatusRegister));
		command |= VtdGlobalCommandSetInterruptRemappingTable |
			VtdGlobalCommandCompatibilityInterrupts;
		WriteRegister32(vtd_state.registers, VtdGlobalCommandRegister, command);
		if (!WaitRegister32(vtd_state.registers, kVtdGlobalStatusRegister,
			VtdGlobalStatusInterruptRemappingTableSet |
			VtdGlobalStatusCompatibilityInterrupts,
			VtdGlobalStatusInterruptRemappingTableSet |
			VtdGlobalStatusCompatibilityInterrupts) ||
			!SubmitInterruptCacheInvalidation()) {
			plogerro("[IOMMU] Interrupt-remapping table activation failed");
			if (DisableInterruptRemappingForRollback(
				compatibility_enabled_before))
				ReleaseInterruptRemappingStorage();
			return false;
		}
		if (!SetVtdFeature(VtdGlobalCommandEnableInterruptRemapping,
			VtdGlobalStatusInterruptRemapping, true)) {
			plogerro("[IOMMU] Interrupt remapping enable timed out");
			if (DisableInterruptRemappingForRollback(
				compatibility_enabled_before))
				ReleaseInterruptRemappingStorage();
			return false;
		}
		vtd_state.extended_interrupt_mode = extended_mode;
		vtd_state.interrupt_remapping_enabled = true;
		ploginfo("[IOMMU] Interrupt remapping enabled entries=%u mode=%s",
			VtdInterruptRemappingTableCapacity,
			extended_mode ? "x2APIC" : "xAPIC");
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

	bool ProbeHardwareUnit(const DmarHardwareUnit& unit, stduint index) {
		ploginfo("[IOMMU] DRHD%u segment=%u include-all=%u base=%[64H] length=%u",
			index, unit.segment, unsigned((unit.flags & kDmarIncludeAll) != 0),
			unit.register_base, unit.header.length);
		const uint8* entry_begin = reinterpret_cast<const uint8*>(&unit);
		if (!LogDeviceScopes(entry_begin + sizeof(DmarHardwareUnit),
			entry_begin + unit.header.length, unit.segment, "DRHD", index))
			return false;
		if (!MapRegisterWindow(unit.register_base)) return false;

		auto registers = reinterpret_cast<volatile uint8*>(
			stduint(unit.register_base));
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
		if (!index) {
			vtd_state.include_all = (unit.flags & kDmarIncludeAll) != 0;
			vtd_state.segment = unit.segment;
			vtd_state.registers = registers;
			vtd_state.capability = capability;
			vtd_state.extended_capability = extended;
		}
		return true;
	}

	bool ProbeReservedMemory(const DmarReservedMemory& range, stduint index) {
		ploginfo("[IOMMU] RMRR%u segment=%u base=%[64H] end=%[64H] length=%u",
			index, range.segment, range.base_address, range.end_address,
			range.header.length);
		if (range.base_address > range.end_address) {
			plogwarn("[IOMMU] RMRR%u has an invalid address range", index);
		}
		const uint8* entry_begin = reinterpret_cast<const uint8*>(&range);
		return LogDeviceScopes(entry_begin + sizeof(DmarReservedMemory),
			entry_begin + range.header.length, range.segment, "RMRR", index);
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

	bool PopulatePassthroughContexts(DeviceNode* node,
		VtdRootEntry* root_table, stduint& table_count,
		stduint& device_count) {
		if (!node) return true;
		if (node->fields.node_type == uint16(DeviceNodeType::PciDevice)) {
			if (node->fields.pci_segment != vtd_state.segment) {
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
		}
		for (Nnode* child = node->link.subf; child; child = child->next) {
			if (!PopulatePassthroughContexts(
				reinterpret_cast<DeviceNode*>(child), root_table,
				table_count, device_count)) return false;
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

	bool InvalidateVtdCaches() {
		auto registers = vtd_state.registers;
		if (ReadRegister32(registers, kVtdGlobalStatusRegister) &
			VtdGlobalStatusQueuedInvalidation) {
			const uint64 context_descriptor = 1u | (uint64(1) << 4);
			uint64 iotlb_descriptor = 2u | (uint64(1) << 4);
			if ((vtd_state.capability >> 55) & 1)
				iotlb_descriptor |= uint64(1) << 7;
			if ((vtd_state.capability >> 54) & 1)
				iotlb_descriptor |= uint64(1) << 6;
			if (!SubmitQueuedInvalidation(context_descriptor) ||
				!SubmitQueuedInvalidation(iotlb_descriptor)) {
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
			(vtd_state.extended_capability >> 8) & 0x3FF) * 16;
		if (iotlb_offset + 16 > PAGE_SIZE) {
			plogerro("[IOMMU] IOTLB register outside mapped window offset=%X",
				iotlb_offset);
			return false;
		}
		uint64 command = VtdIotlbCommandInvalidate | VtdIotlbCommandGlobal;
		if ((vtd_state.capability >> 55) & 1) command |= VtdIotlbReadDrain;
		if ((vtd_state.capability >> 54) & 1) command |= VtdIotlbWriteDrain;
		WriteRegister64(registers, iotlb_offset + 8, command);
		if (!WaitRegister64Clear(registers, iotlb_offset + 8,
			VtdIotlbCommandInvalidate)) {
			plogerro("[IOMMU] IOTLB invalidation timed out");
			return false;
		}
		return true;
	}

	void LogVtdFaultState(const char* operation, DeviceNode* device,
		uint64 device_address) {
		const uint32 status = ReadRegister32(vtd_state.registers,
			kVtdFaultStatusRegister);
		if (!status) return;
		const stduint record_index = (status >> 8) & 0xFF;
		const stduint record_offset =
			stduint((vtd_state.capability >> 24) & 0x3FF) * 16 +
			record_index * 16;
		if (record_offset + 16 > PAGE_SIZE) {
			plogerro("[IOMMU] %s fault pci=%02X:%02X.%u iova=%[64H] fsts=%08X record=%X unavailable",
				operation, device->fields.pci_bus, device->fields.pci_device,
				device->fields.pci_function, device_address, status,
				record_offset);
			return;
		}
		const uint64 record_low = ReadRegister64(vtd_state.registers,
			record_offset);
		const uint64 record_high = ReadRegister64(vtd_state.registers,
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

	IovaDomain* CreateDomain(DeviceNode* device) {
		for0(i, IovaDomainCapacity) {
			auto& domain = iova_domains[i];
			if (domain.active) continue;
			domain = {};
			domain.active = true;
			domain.device = device;
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

	void ClearIovaPages(IovaDomain& domain, uint64 device_address,
		stduint length) {
		for (stduint offset = 0; offset < length; offset += PAGE_SIZE) {
			auto entry = GetLeafEntry(domain, device_address + offset, false);
			if (entry) *entry = 0;
		}
		StoreFence();
	}

	bool RestorePassthroughContext(VtdContextEntry& context) {
		context.low = 0;
		StoreFence();
		(void)InvalidateVtdCaches();
		context.high = uint64(VtdPassthroughDomain) << 8 |
			VtdAddressWidth48;
		StoreFence();
		context.low = VtdContextPassthrough | VtdContextPresent;
		StoreFence();
		return InvalidateVtdCaches();
	}

	bool AttachDomain(IovaDomain& domain) {
		auto device = domain.device;
		if (!device || device->fields.node_type !=
			uint16(DeviceNodeType::PciDevice) ||
			device->fields.pci_segment != vtd_state.segment ||
			device->fields.pci_device >= 32 ||
			device->fields.pci_function >= 8) return false;
		auto context_table = GetContextTable(vtd_state.root_table,
			device->fields.pci_bus);
		if (!context_table) return false;
		const stduint requester = stduint(device->fields.pci_device) * 8 +
			device->fields.pci_function;
		auto& context = context_table[requester];
		if (!(context.low & VtdContextPresent)) return false;

		context.low = 0;
		StoreFence();
		if (!InvalidateVtdCaches()) {
			(void)RestorePassthroughContext(context);
			return false;
		}
		context.high = uint64(domain.domain_id) << 8 | VtdAddressWidth48;
		StoreFence();
		context.low = uint64(stduint(domain.root)) | VtdContextPresent;
		StoreFence();
		if (!InvalidateVtdCaches()) {
			(void)RestorePassthroughContext(context);
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
	vtd_state.dmar_flags = table->flags;
	vtd_state.host_address_width = table->host_address_width + 1;

	const uint8* cursor = reinterpret_cast<const uint8*>(table) +
		sizeof(DmarTable);
	const uint8* end = reinterpret_cast<const uint8*>(table) +
		table->header.length;
	stduint hardware_unit_count = 0;
	stduint reserved_memory_count = 0;
	bool probe_ok = true;
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
			if (!ProbeHardwareUnit(
				*reinterpret_cast<const DmarHardwareUnit*>(cursor),
				hardware_unit_count++)) probe_ok = false;
		}
		else if (header->type == kDmarReservedMemory) {
			if (header->length < sizeof(DmarReservedMemory)) {
				plogwarn("[IOMMU] RMRR%u is truncated length=%u",
					reserved_memory_count, header->length);
				return false;
			}
			if (!ProbeReservedMemory(
				*reinterpret_cast<const DmarReservedMemory*>(cursor),
				reserved_memory_count++)) probe_ok = false;
		}
		else {
			ploginfo("[IOMMU] DMAR entry type=%u length=%u",
				header->type, header->length);
		}
		cursor += header->length;
	}
	if (!hardware_unit_count) {
		plogwarn("[IOMMU] DMAR contains no DRHD units");
		return false;
	}
	if (!probe_ok) {
		plogwarn("[IOMMU] Read-only VT-d probe incomplete: drhd=%u rmrr=%u",
			hardware_unit_count, reserved_memory_count);
		return false;
	}
	vtd_state.hardware_unit_count = hardware_unit_count;
	vtd_state.probe_complete = true;
	plogtrac("[IOMMU] Read-only VT-d probe complete: drhd=%u rmrr=%u",
		hardware_unit_count, reserved_memory_count);
	return true;
}

bool Iova::EnableVtdPassthrough(DeviceNode* pci_root) {
	plogtrac("[IOMMU] Pass-through request probed=%u enabled=%u drhd=%u root=%[x]",
		unsigned(vtd_state.probe_complete),
		unsigned(vtd_state.passthrough_enabled),
		vtd_state.hardware_unit_count, stduint(pci_root));
	if (!vtd_state.probe_complete) return true;
	if (vtd_state.passthrough_enabled) return true;
	if (vtd_state.root_table) {
		plogwarn("[IOMMU] Previous pass-through activation did not complete");
		return false;
	}
	if (!pci_root) {
		plogerro("[IOMMU] Cannot enable pass-through without PCI topology");
		return false;
	}
	if (vtd_state.hardware_unit_count != 1 || !vtd_state.include_all) {
		plogwarn("[IOMMU] Pass-through startup supports one include-all DRHD; drhd=%u include-all=%u",
			vtd_state.hardware_unit_count, unsigned(vtd_state.include_all));
		return false;
	}
	if (!(vtd_state.extended_capability & (uint64(1) << 6))) {
		plogwarn("[IOMMU] DRHD does not support pass-through translation");
		return false;
	}
	if (!(vtd_state.extended_capability & 1)) {
		plogwarn("[IOMMU] Non-coherent page-table access is not supported yet");
		return false;
	}
	if (!(vtd_state.capability & (uint64(1) << (8 + VtdAddressWidth48)))) {
		plogwarn("[IOMMU] DRHD does not support 48-bit adjusted address width");
		return false;
	}
	const stduint iotlb_offset = stduint(
		(vtd_state.extended_capability >> 8) & 0x3FF) * 16;
	if (iotlb_offset + 16 > PAGE_SIZE) {
		plogwarn("[IOMMU] IOTLB register outside mapped window offset=%X",
			iotlb_offset);
		return false;
	}

	auto registers = vtd_state.registers;
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
	if (!PopulatePassthroughContexts(pci_root, root_table,
		context_table_count, device_count) || !device_count) {
		plogerro("[IOMMU] Failed to build pass-through contexts devices=%u",
			device_count);
		ReleasePassthroughTables(root_table);
		return false;
	}
	StoreFence();

	WriteRegister64(registers, kVtdRootTableAddressRegister, root_physical);
	WriteRegister32(registers, VtdGlobalCommandRegister,
		VtdGlobalCommandSetRoot);
	if (!WaitRegister32(registers, kVtdGlobalStatusRegister,
		VtdGlobalStatusRootSet, VtdGlobalStatusRootSet)) {
		plogerro("[IOMMU] Set-root-table command timed out gsts=%08X",
			ReadRegister32(registers, kVtdGlobalStatusRegister));
		vtd_state.root_table = root_table;
		return false;
	}
	const uint64 loaded_root = ReadRegister64(registers,
		kVtdRootTableAddressRegister) & VtdPageAddressMask;
	if (loaded_root != root_physical) {
		plogerro("[IOMMU] Root-table readback mismatch expected=%[64H] actual=%[64H]",
			root_physical, loaded_root);
		vtd_state.root_table = root_table;
		return false;
	}
	if (!InvalidateVtdCaches()) {
		vtd_state.root_table = root_table;
		return false;
	}
	const uint32 invalidation_faults = ReadRegister32(registers,
		kVtdFaultStatusRegister);
	if (invalidation_faults) {
		plogerro("[IOMMU] VT-d fault during invalidation fsts=%08X",
			invalidation_faults);
		vtd_state.root_table = root_table;
		return false;
	}
	WriteRegister32(registers, VtdGlobalCommandRegister,
		VtdGlobalCommandEnableTranslation);
	if (!WaitRegister32(registers, kVtdGlobalStatusRegister,
		VtdGlobalStatusTranslationEnabled,
		VtdGlobalStatusTranslationEnabled)) {
		plogerro("[IOMMU] Translation-enable command timed out gsts=%08X",
			ReadRegister32(registers, kVtdGlobalStatusRegister));
		vtd_state.root_table = root_table;
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
		vtd_state.root_table = root_table;
		return false;
	}

	vtd_state.root_table = root_table;
	vtd_state.context_table_count = context_table_count;
	vtd_state.device_count = device_count;
	vtd_state.passthrough_enabled = true;
	ploginfo("[IOMMU] Pass-through enabled root=%[64H] buses=%u devices=%u bytes=%u gsts=%08X fsts=%08X",
		root_physical, context_table_count, device_count,
		(context_table_count + 1) * PAGE_SIZE,
		ReadRegister32(registers, kVtdGlobalStatusRegister),
		ReadRegister32(registers, kVtdFaultStatusRegister));
	(void)EnableVtdInterruptRemapping();
	return true;
}

bool Iova::InterruptRemappingAvailable() {
	return vtd_state.interrupt_remapping_enabled;
}

bool Iova::AllocatePciInterrupt(DeviceNode* device, uint32 target_lapic_id,
	uint8 vector, InterruptMessage& message) {
	message = {};
	if (!vtd_state.interrupt_remapping_enabled || !device || !vector ||
		DeviceNodeType(device->fields.node_type) != DeviceNodeType::PciDevice ||
		device->fields.pci_segment != vtd_state.segment ||
		(!vtd_state.extended_interrupt_mode && target_lapic_id > 0xFFu))
		return false;
	SpinlockLocal guard(IovaLock());
	stduint index = VtdInterruptRemappingTableCapacity;
	for0(candidate, VtdInterruptRemappingTableCapacity) {
		if (!vtd_state.interrupt_remapping_allocated[candidate]) {
			index = candidate;
			break;
		}
	}
	if (index == VtdInterruptRemappingTableCapacity) return false;
	const uint16 source_id = uint16(uint16(device->fields.pci_bus) << 8) |
		uint16(uint16(device->fields.pci_device) << 3) |
		device->fields.pci_function;
	auto& entry = vtd_state.interrupt_remapping_table[index];
	entry.high = uint64(source_id) | (uint64(1) << 18);
	uint64 destination = target_lapic_id;
	if (!vtd_state.extended_interrupt_mode) destination <<= 8;
	entry.low = 1u | (uint64(1) << 3) | (uint64(vector) << 16) |
		(destination << 32);
	StoreFence();
	if (!SubmitInterruptCacheInvalidation(uint16(index), 0, true)) {
		entry.low = 0;
		entry.high = 0;
		StoreFence();
		(void)SubmitInterruptCacheInvalidation(uint16(index), 0, true);
		plogerro("[IOMMU] Interrupt-entry invalidation failed index=%u",
			index);
		return false;
	}
	vtd_state.interrupt_remapping_allocated[index] = true;
	message.address = 0xFEE00000u | (uint64(index & 0x7FFFu) << 5) |
		(uint64(index & 0x8000u) >> 13) | 0x18u;
	message.data = 0;
	message.handle = uint16(index + 1);
	return true;
}

void Iova::ReleaseInterrupt(uint16 handle) {
	if (!handle || !vtd_state.interrupt_remapping_enabled) return;
	const stduint index = handle - 1u;
	if (index >= VtdInterruptRemappingTableCapacity) return;
	SpinlockLocal guard(IovaLock());
	if (!vtd_state.interrupt_remapping_allocated[index]) return;
	auto& entry = vtd_state.interrupt_remapping_table[index];
	entry.low = 0;
	entry.high = 0;
	StoreFence();
	if (!SubmitInterruptCacheInvalidation(uint16(index), 0, true)) {
		plogerro("[IOMMU] Interrupt-entry release invalidation failed index=%u",
			index);
		return;
	}
	vtd_state.interrupt_remapping_allocated[index] = false;
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
	if (!vtd_state.passthrough_enabled) return true;
	if (device->fields.node_type != uint16(DeviceNodeType::PciDevice))
		return true;
	if (!PhysicalRangeFits(uint64(physical), length,
		vtd_state.host_address_width)) return false;

	SpinlockLocal guard(IovaLock());
	auto domain = FindDomain(device);
	const bool new_domain = !domain;
	if (!domain) domain = CreateDomain(device);
	if (!domain) {
		plogerro("[IOMMU] No translated domain available pci=%02X:%02X.%u",
			device->fields.pci_bus, device->fields.pci_device,
			device->fields.pci_function);
		return false;
	}
	IovaRange* range = nullptr;
	uint64 device_address = 0;
	const uint64 preferred_address = (_IMM(flags) &
		_IMM(DmaMappingFlag::Identity)) ? uint64(physical) : 0;
	if (!AllocateIovaRange(*domain, length, preferred_address,
		device_address, range) ||
		!PopulateIovaPages(*domain, device_address, physical, length)) {
		plogerro("[IOMMU] Failed to map DMA pci=%02X:%02X.%u phys=%[64H] bytes=%u",
			device->fields.pci_bus, device->fields.pci_device,
			device->fields.pci_function, uint64(physical), length);
		if (new_domain) ReleaseDomainTables(*domain);
		return false;
	}
	if (!domain->attached && !AttachDomain(*domain)) {
		ClearIovaPages(*domain, device_address, length);
		plogerro("[IOMMU] Failed to attach translated domain pci=%02X:%02X.%u",
			device->fields.pci_bus, device->fields.pci_device,
			device->fields.pci_function);
		if (new_domain) ReleaseDomainTables(*domain);
		return false;
	}
	if (!new_domain && !InvalidateVtdCaches()) {
		ClearIovaPages(*domain, device_address, length);
		return false;
	}
	const uint32 faults = ReadRegister32(vtd_state.registers,
		kVtdFaultStatusRegister);
	if (faults) {
		LogVtdFaultState("DMA map", device, device_address);
		ClearIovaPages(*domain, device_address, length);
		InvalidateVtdCaches();
		plogerro("[IOMMU] Fault after DMA map pci=%02X:%02X.%u fsts=%08X",
			device->fields.pci_bus, device->fields.pci_device,
			device->fields.pci_function, faults);
		return false;
	}
	range->active = true;
	range->device_address = device_address;
	range->physical = physical;
	range->length = length;
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
	if (!vtd_state.passthrough_enabled) return true;

	SpinlockLocal guard(IovaLock());
	auto domain = FindDomain(device);
	if (!domain || !domain->attached) return false;
	IovaRange* range = nullptr;
	for0(i, IovaMappingCapacity) {
		auto& current = domain->mappings[i];
		if (current.active && current.device_address == device_address &&
			current.length == length) {
			range = &current;
			break;
		}
	}
	if (!range) return false;
	LogVtdFaultState("DMA unmap", device, device_address);
	ClearIovaPages(*domain, device_address, length);
	if (!InvalidateVtdCaches()) {
		PopulateIovaPages(*domain, device_address, range->physical, length);
		InvalidateVtdCaches();
		return false;
	}
	*range = {};
	return true;
}

#endif
