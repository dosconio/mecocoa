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
	static constexpr stduint kVtdGlobalStatusRegister = 0x1C;
	static constexpr stduint kVtdRootTableAddressRegister = 0x20;
	static constexpr stduint kVtdFaultStatusRegister = 0x34;
	static constexpr stduint kVtdRegisterWindowLength = 0x38;

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

	static_assert(sizeof(DmarTable) == 48);
	static_assert(sizeof(DmarHardwareUnit) == 16);
	static_assert(sizeof(DmarReservedMemory) == 24);
	static_assert(sizeof(DmarDeviceScope) == 6);
	static_assert(sizeof(DmarPciPath) == 2);

	uint32 ReadRegister32(volatile uint8* registers, stduint offset) {
		return *reinterpret_cast<volatile uint32*>(registers + offset);
	}

	uint64 ReadRegister64(volatile uint8* registers, stduint offset) {
		const uint32 low = ReadRegister32(registers, offset);
		const uint32 high = ReadRegister32(registers, offset + sizeof(uint32));
		return uint64(low) | (uint64(high) << 32);
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
}

bool Iova::ProbeVtd(stduint dmar_address) {
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
	ploginfo("[IOMMU] Read-only VT-d probe complete: drhd=%u rmrr=%u",
		hardware_unit_count, reserved_memory_count);
	return true;
}

#endif
