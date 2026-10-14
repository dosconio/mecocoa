// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#if CONFIG_ENABLE_MMU
#if _MCCA == 0x8632
static constexpr stduint kAcpiPhysMapTop = 0x10000000;
static constexpr stduint kAcpiHighWindowBase = 0x3FC00000;
static constexpr stduint kAcpiHighWindowSize = 0x00400000;
#endif

namespace {
	struct PhysicalRange {
		stduint begin;
		stduint end;
	};

	struct PhysicalRangeRemainder {
		PhysicalRange ranges[2];
		stduint count;
	};

	// Build a non-empty half-open range while rejecting address overflow.
	[[maybe_unused]] static bool MakePhysicalRange(stduint begin,
		stduint length, PhysicalRange& range) {
		if (!length || length > ~_IMM0 - begin) return false;
		range.begin = begin;
		range.end = begin + length;
		return true;
	}

	// Check whether two half-open ranges have identical bounds.
	[[maybe_unused]] static bool PhysicalRangesEqual(const PhysicalRange& left,
		const PhysicalRange& right) {
		return left.begin == right.begin && left.end == right.end;
	}

	// Check whether two half-open ranges share at least one address.
	[[maybe_unused]] static bool PhysicalRangesOverlap(const PhysicalRange& left,
		const PhysicalRange& right) {
		return left.begin < right.end && right.begin < left.end;
	}

	// Check whether the outer range fully contains the inner range.
	[[maybe_unused]] static bool PhysicalRangeContains(const PhysicalRange& outer,
		const PhysicalRange& inner) {
		return outer.begin <= inner.begin && inner.end <= outer.end;
	}

	// Check whether two ranges overlap without either range containing the other.
	[[maybe_unused]] static bool PhysicalRangesPartiallyOverlap(
		const PhysicalRange& left, const PhysicalRange& right) {
		return PhysicalRangesOverlap(left, right) &&
			!PhysicalRangeContains(left, right) &&
			!PhysicalRangeContains(right, left);
	}

	// Check whether two non-overlapping ranges touch at one boundary.
	[[maybe_unused]] static bool PhysicalRangesAdjacent(const PhysicalRange& left,
		const PhysicalRange& right) {
		return left.end == right.begin || right.end == left.begin;
	}

	// Subtract one range from another and return up to two retained fragments.
	[[maybe_unused]] static bool SubtractPhysicalRange(const PhysicalRange& source,
		const PhysicalRange& removed, PhysicalRangeRemainder& remainder) {
		if (source.begin >= source.end || removed.begin >= removed.end) return false;
		remainder = {};
		if (!PhysicalRangesOverlap(source, removed)) {
			remainder.ranges[remainder.count++] = source;
			return true;
		}
		if (source.begin < removed.begin) {
			remainder.ranges[remainder.count++] = {source.begin, removed.begin};
		}
		if (removed.end < source.end) {
			remainder.ranges[remainder.count++] = {removed.end, source.end};
		}
		return true;
	}

	// Decide whether two explicit page memory types may alias the same PA range.
	[[maybe_unused]] static bool PhysicalPageTypesCompatible(stduint existing_type,
		stduint requested_type) {
		switch (existing_type) {
		case PGTYPE_default:
		case PGTYPE_writeback:
		case PGTYPE_writethrough:
		case PGTYPE_uncacheable:
		case PGTYPE_writecombine:
		case PGTYPE_device:
		case PGTYPE_noncacheable:
			break;
		default:
			return false;
		}
		switch (requested_type) {
		case PGTYPE_default:
		case PGTYPE_writeback:
		case PGTYPE_writethrough:
		case PGTYPE_uncacheable:
		case PGTYPE_writecombine:
		case PGTYPE_device:
		case PGTYPE_noncacheable:
			break;
		default:
			return false;
		}
		if (existing_type == PGTYPE_default || requested_type == PGTYPE_default)
			return false;
		#if defined(_ARC_x86) || defined(_ARC_x64)
		if (existing_type == PGTYPE_device) existing_type = PGTYPE_uncacheable;
		if (requested_type == PGTYPE_device) requested_type = PGTYPE_uncacheable;
		#endif
		return existing_type == requested_type;
	}

	struct PhysicalMappingRecord {
		Paging* paging;
		PhysicalRange virtual_range;
		PhysicalRange physical_range;
		stduint type;
		stduint permissions;
		PhysicalMappingRecord* next;
	};

	static PhysicalMappingRecord* physical_mapping_records;
	static Mutex physical_mapping_records_mutex;

	// Allocate one internal physical mapping record.
	static PhysicalMappingRecord* AllocatePhysicalMappingRecord() {
		void* memory = mempool.allocate(sizeof(PhysicalMappingRecord), 4);
		if (!memory || memory == (void*)~_IMM0) return nullptr;
		return (PhysicalMappingRecord*)memory;
	}

	// Release every record in an internal temporary record chain.
	static void ReleasePhysicalMappingRecords(PhysicalMappingRecord* records) {
		while (records) {
			PhysicalMappingRecord* next = records->next;
			mempool.deallocate(records, sizeof(PhysicalMappingRecord));
			records = next;
		}
	}

	// Preallocate all record nodes required by a later allocation-free commit.
	static bool ReservePhysicalMappingRecords(stduint count,
		PhysicalMappingRecord*& records) {
		records = nullptr;
		while (count--) {
			PhysicalMappingRecord* record = AllocatePhysicalMappingRecord();
			if (!record) {
				ReleasePhysicalMappingRecords(records);
				records = nullptr;
				return false;
			}
			record->next = records;
			records = record;
		}
		return true;
	}

	// Detach and return the first node from a reserved record chain.
	static PhysicalMappingRecord* TakePhysicalMappingRecord(
		PhysicalMappingRecord*& records) {
		PhysicalMappingRecord* record = records;
		if (record) records = record->next;
		return record;
	}

	// Validate and normalize a managed mapping request into one registry record.
	static bool NormalizeMapRequest(stduint linear_address,
		stduint physical_address, stduint length, PageSizeShift page_size,
		stduint properties, PhysicalMappingRecord& record) {
		stduint normalized;
		if (!NormalizePageProperties(properties, normalized) ||
			!(normalized & PGPROP_present) || (normalized & PGPROP_weak)) return false;
		const stduint type = normalized & PGPROP_MASK_TYPE;
		if (type == PGTYPE_default) return false;

		stduint shift = PAGESIZE_4KB;
		if (page_size != PAGESIZE_ANY) {
			if (!Paging::isSupportedSize(page_size)) return false;
			shift = page_size;
		}
		if (shift >= sizeof(stduint) * 8) return false;
		const stduint mask = (_IMM1 << shift) - 1;
		const stduint virtual_offset = linear_address & mask;
		if (page_size == PAGESIZE_ANY &&
			virtual_offset != (physical_address & mask)) return false;
		linear_address -= virtual_offset;
		physical_address &= ~mask;
		if (length > ~_IMM0 - virtual_offset) return false;
		length += virtual_offset;
		if (length > ~_IMM0 - mask) return false;
		length = (length + mask) & ~mask;
		if (!MakePhysicalRange(linear_address, length, record.virtual_range) ||
			!MakePhysicalRange(physical_address, length, record.physical_range)) return false;
		record.type = type;
		record.permissions = normalized & (PGPROP_writable | PGPROP_user_access |
			PGPROP_global | PGPROP_nonexecutable);
		record.next = nullptr;
		return true;
	}

	// Normalize an unmap request to its effective 4 KiB-aligned VA range.
	static bool NormalizeUnmapRequest(stduint linear_address, stduint length,
		PhysicalRange& virtual_range) {
		if (!length) return false;
		const stduint mask = (_IMM1 << PAGESIZE_4KB) - 1;
		const stduint offset = linear_address & mask;
		linear_address -= offset;
		if (length > ~_IMM0 - offset) return false;
		length += offset;
		if (length > ~_IMM0 - mask) return false;
		length = (length + mask) & ~mask;
		return MakePhysicalRange(linear_address, length, virtual_range);
	}

	// Remove a VA range from one record and preserve matching VA-to-PA fragments.
	static bool SplitPhysicalMappingRecord(const PhysicalMappingRecord& source,
		const PhysicalRange& removed, PhysicalMappingRecord fragments[2],
		stduint& count) {
		PhysicalRangeRemainder remainder;
		if (!SubtractPhysicalRange(source.virtual_range, removed, remainder))
			return false;
		count = remainder.count;
		for (stduint index = 0; index < count; index++) {
			fragments[index] = source;
			fragments[index].virtual_range = remainder.ranges[index];
			const stduint offset = remainder.ranges[index].begin -
				source.virtual_range.begin;
			const stduint length = remainder.ranges[index].end -
				remainder.ranges[index].begin;
			if (!MakePhysicalRange(source.physical_range.begin + offset,
				length, fragments[index].physical_range)) return false;
			fragments[index].next = nullptr;
		}
		return true;
	}

	// Detect an incompatible memory-type alias across overlapping PA ranges.
	static bool MappingTypeConflicts(const PhysicalMappingRecord& existing,
		const PhysicalMappingRecord& requested) {
		return PhysicalRangesOverlap(existing.physical_range,
			requested.physical_range) &&
			!PhysicalPageTypesCompatible(existing.type, requested.type);
	}

	// Validate a map against the registry and reserve every required record node.
	static bool PrepareMapRecords(const PhysicalMappingRecord& requested,
		PhysicalMappingRecord*& reserved_records) {
		stduint reserve_count = 1;
		for (PhysicalMappingRecord* existing = physical_mapping_records;
			existing; existing = existing->next) {
			if (existing->paging == requested.paging &&
				PhysicalRangesOverlap(existing->virtual_range,
					requested.virtual_range)) {
				PhysicalMappingRecord fragments[2];
				stduint count;
				if (!SplitPhysicalMappingRecord(*existing,
					requested.virtual_range, fragments, count)) return false;
				if (count == 2) reserve_count++;
				for (stduint index = 0; index < count; index++) {
					if (MappingTypeConflicts(fragments[index], requested))
						return false;
				}
			}
			else if (MappingTypeConflicts(*existing, requested)) return false;
		}
		return ReservePhysicalMappingRecords(reserve_count, reserved_records);
	}

	// Commit a successful map to the registry without allocating memory.
	static void CommitMapRecord(const PhysicalMappingRecord& requested,
		PhysicalMappingRecord*& reserved_records) {
		PhysicalMappingRecord** link = &physical_mapping_records;
		while (*link) {
			PhysicalMappingRecord* existing = *link;
			if (existing->paging != requested.paging ||
				!PhysicalRangesOverlap(existing->virtual_range,
					requested.virtual_range)) {
				link = &existing->next;
				continue;
			}

			PhysicalMappingRecord fragments[2];
			stduint count;
			bool valid = SplitPhysicalMappingRecord(*existing,
				requested.virtual_range, fragments, count);
			KASSERT(valid);
			if (!count) {
				*link = existing->next;
				mempool.deallocate(existing, sizeof(PhysicalMappingRecord));
				continue;
			}
			PhysicalMappingRecord* next = existing->next;
			*existing = fragments[0];
			existing->next = next;
			if (count == 2) {
				PhysicalMappingRecord* split =
					TakePhysicalMappingRecord(reserved_records);
				KASSERT(split);
				*split = fragments[1];
				split->next = existing->next;
				existing->next = split;
				link = &split->next;
			}
			else link = &existing->next;
		}

		PhysicalMappingRecord* added = TakePhysicalMappingRecord(reserved_records);
		KASSERT(added);
		*added = requested;
		added->next = physical_mapping_records;
		physical_mapping_records = added;
		KASSERT(!reserved_records);
	}

	// Reserve record nodes needed to represent the survivors of an unmap.
	static bool PrepareUnmapRecords(Paging& paging,
		const PhysicalRange& removed, PhysicalMappingRecord*& reserved_records) {
		stduint reserve_count = 0;
		for (PhysicalMappingRecord* existing = physical_mapping_records;
			existing; existing = existing->next) {
			if (existing->paging != &paging ||
				!PhysicalRangesOverlap(existing->virtual_range, removed)) continue;
			PhysicalMappingRecord fragments[2];
			stduint count;
			if (!SplitPhysicalMappingRecord(*existing, removed,
				fragments, count)) return false;
			if (count == 2) reserve_count++;
		}
		return ReservePhysicalMappingRecords(reserve_count, reserved_records);
	}

	// Commit a successful unmap to the registry without allocating memory.
	static void CommitUnmapRecords(Paging& paging, const PhysicalRange& removed,
		PhysicalMappingRecord*& reserved_records) {
		PhysicalMappingRecord** link = &physical_mapping_records;
		while (*link) {
			PhysicalMappingRecord* existing = *link;
			if (existing->paging != &paging ||
				!PhysicalRangesOverlap(existing->virtual_range, removed)) {
				link = &existing->next;
				continue;
			}

			PhysicalMappingRecord fragments[2];
			stduint count;
			bool valid = SplitPhysicalMappingRecord(*existing, removed,
				fragments, count);
			KASSERT(valid);
			if (!count) {
				*link = existing->next;
				mempool.deallocate(existing, sizeof(PhysicalMappingRecord));
				continue;
			}
			PhysicalMappingRecord* next = existing->next;
			*existing = fragments[0];
			existing->next = next;
			if (count == 2) {
				PhysicalMappingRecord* split =
					TakePhysicalMappingRecord(reserved_records);
				KASSERT(split);
				*split = fragments[1];
				split->next = existing->next;
				existing->next = split;
				link = &split->next;
			}
			else link = &existing->next;
		}
		KASSERT(!reserved_records);
	}

	// Validate one page-aligned initial mapping region and its explicit type.
	static bool NormalizeInitialPageRegion(const InitialPageRegion& source, InitialPageRegion& normalized) {
		const stduint page_mask = (_IMM1 << PAGESIZE_4KB) - 1;
		const stduint permission_mask = PGPROP_present | PGPROP_writable |
			PGPROP_user_access | PGPROP_global | PGPROP_nonexecutable;
		if (source.begin >= source.end || (source.begin & page_mask) ||
			(source.end & page_mask) || source.type == PGTYPE_default ||
			!PageTypeSupported(source.type) ||
			(source.permissions & ~permission_mask)) return false;
		stduint properties;
		if (!NormalizePageProperties(source.permissions | source.type, properties) ||
			!(properties & PGPROP_present) || (properties & PGPROP_weak) ||
			(properties & PGPROP_MASK_TYPE) != source.type) return false;
		normalized = source;
		normalized.permissions = properties & ~PGPROP_MASK_TYPE;
		return true;
	}

	// Check whether two initial regions have identical mapping attributes.
	static bool InitialPageRegionAttributesEqual(const InitialPageRegion& left,
		const InitialPageRegion& right) {
		return left.type == right.type && left.permissions == right.permissions;
	}

	// Overlay one region on an ordered plan and merge equal adjacent regions.
	static bool OverlayInitialPageRegion(InitialPageRegion* plan, stduint plan_capacity, stduint& plan_count, const InitialPageRegion& added) {
		stduint required = 1;
		for0(i, plan_count) {
			if (plan[i].end <= added.begin || added.end <= plan[i].begin) required++;
			else {
				if (plan[i].begin < added.begin) required++;
				if (added.end < plan[i].end) required++;
			}
		}
		if (required > plan_capacity) return false;

		for (stduint index = 0; index < plan_count;) {
			const InitialPageRegion existing = plan[index];
			if (existing.end <= added.begin || added.end <= existing.begin) {
				index++;
				continue;
			}
			const bool keep_left = existing.begin < added.begin;
			const bool keep_right = added.end < existing.end;
			if (keep_left && keep_right) {
				for (stduint move = plan_count; move > index + 1; move--)
					plan[move] = plan[move - 1];
				plan_count++;
				plan[index].end = added.begin;
				plan[index + 1] = existing;
				plan[index + 1].begin = added.end;
				index += 2;
			}
			else if (keep_left) {
				plan[index].end = added.begin;
				index++;
			}
			else if (keep_right) {
				plan[index].begin = added.end;
				index++;
			}
			else {
				for (stduint move = index + 1; move < plan_count; move++)
					plan[move - 1] = plan[move];
				plan_count--;
			}
		}

		stduint insert = 0;
		while (insert < plan_count && plan[insert].begin < added.begin) insert++;
		for (stduint move = plan_count; move > insert; move--)
			plan[move] = plan[move - 1];
		plan[insert] = added;
		plan_count++;

		stduint output = 0;
		for0(input, plan_count) {
			if (output && plan[output - 1].end == plan[input].begin &&
				InitialPageRegionAttributesEqual(plan[output - 1], plan[input])) {
				plan[output - 1].end = plan[input].end;
			}
			else plan[output++] = plan[input];
		}
		plan_count = output;
		return true;
	}
}

#if defined(_ARC_x86) || defined(_ARC_x64)
// Verify that PAT WC can remain WC after combining with the current MTRR type.
static bool framebuffer_range_supports_wc(stduint begin, stduint end) {
	if (begin >= end || !uni::PagingPATEnabled()) return false;
	for (stduint address = begin; address < end;) {
		stduint mtrr_type;
		if (!uni::ReadX86MTRRType(address, mtrr_type) ||
			(mtrr_type != PGTYPE_writeback &&
				mtrr_type != PGTYPE_writecombine)) return false;
		if (end - address <= PAGE_SIZE) break;
		address += PAGE_SIZE;
	}
	return true;
}
#endif

bool Memory::MapPages(Paging& paging, stduint linear_address,
	stduint physical_address, stduint length,
	PageSizeShift page_size, stduint properties,
	Spinlock* page_table_lock) {
	if (!length) return true;
	PhysicalMappingRecord requested = {};
	requested.paging = &paging;
	if (!NormalizeMapRequest(linear_address, physical_address, length,
		page_size, properties, requested)) return false;

	MutexLocal guard(&physical_mapping_records_mutex);
	PhysicalMappingRecord* reserved_records;
	if (!PrepareMapRecords(requested, reserved_records)) return false;
	bool mapped;
	if (page_table_lock) {
		SpinlockLocal page_guard(page_table_lock);
		mapped = paging.Map(linear_address, physical_address, length,
			page_size, properties);
	}
	else {
		mapped = paging.Map(linear_address, physical_address, length,
			page_size, properties);
	}
	if (!mapped) {
		ReleasePhysicalMappingRecords(reserved_records);
		return false;
	}
	CommitMapRecord(requested, reserved_records);
	return true;
}

#if defined(_ARC_x86) || defined(_ARC_x64)
bool Memory::MapFramebufferPages(stduint physical_address, stduint length) {
	if (!physical_address || !length ||
		length > ~_IMM0 - physical_address ||
		physical_address + length > ~_IMM0 - (PAGE_SIZE - 1)) return false;
	const stduint page_mask = PAGE_SIZE - 1;
	const stduint begin = physical_address & ~page_mask;
	const stduint end = (physical_address + length + PAGE_SIZE - 1) &
		~page_mask;
	if (end <= begin) return false;
	const bool wc_supported = framebuffer_range_supports_wc(begin, end);
	#if defined(_ARC_x86)
	const stduint high_mapping_base = 0x80000000;
	const stduint high_mapping_limit = 0x10000000;
	#else
	const stduint high_mapping_base = 0x0000FFFFC0000000ull;
	const stduint high_mapping_limit = 0x40000000ull -
		_IMM1S(PAGESIZE_2MB);
	#endif

	auto read_type = [&](stduint virtual_address, stduint physical,
		stduint& type) -> bool {
		type = PGTYPE_default;
		pageint page = virtual_address;
		PageEntry* entry = kernel_paging.getEntry(page);
		if (_IMM(entry) == ~_IMM0) return true;
		if (_IMM(kernel_paging[virtual_address]) != physical) return false;
		stduint properties;
		if (!entry->DecodeLeafProperties(page.crt_level, properties))
			return false;
		type = properties & PGPROP_MASK_TYPE;
		return true;
	};

	auto select_type = [&](stduint address, stduint& type) -> bool {
		stduint existing_type;
		if (!read_type(address, address, existing_type)) return false;
		const bool full_resource_page = address >= physical_address &&
			physical_address + length - address >= PAGE_SIZE;
		const bool may_use_wc = wc_supported &&
			((existing_type == PGTYPE_default && full_resource_page) ||
				existing_type == PGTYPE_writecombine);
		type = may_use_wc ? PGTYPE_writecombine : PGTYPE_device;

		if (address < high_mapping_limit) {
			stduint alias_type;
			if (!read_type(high_mapping_base + address, address,
				alias_type) ||
				(alias_type != PGTYPE_default &&
					alias_type != type &&
					!(type == PGTYPE_device &&
						alias_type == PGTYPE_uncacheable))) return false;
		}
		return true;
	};
	for (stduint address = begin; address < end; address += PAGE_SIZE) {
		stduint type;
		if (!select_type(address, type)) {
			plogerro("[PAGING] Framebuffer alias or page type conflict at PA=%[x]",
				address);
			return false;
		}
	}

	stduint run_begin = begin;
	stduint run_type = PGTYPE_default;
	for (stduint address = begin; address < end; address += PAGE_SIZE) {
		stduint type;
		if (!select_type(address, type)) return false;
		if (run_type == PGTYPE_default) run_type = type;
		if (type == run_type) continue;
		if (!MapPages(kernel_paging, run_begin, run_begin,
			address - run_begin, PAGESIZE_ANY,
			PGPROP_present | PGPROP_writable | run_type |
			PGOPTS_omit_warnings)) {
			plogerro("[PAGING] Failed framebuffer mapping: %[x]..%[x] type=%[x]",
				run_begin, address, run_type);
			return false;
		}
		run_begin = address;
		run_type = type;
	}
	if (!MapPages(kernel_paging, run_begin, run_begin,
		end - run_begin, PAGESIZE_ANY,
		PGPROP_present | PGPROP_writable | run_type |
		PGOPTS_omit_warnings)) {
		plogerro("[PAGING] Failed framebuffer mapping: %[x]..%[x] type=%[x]",
			run_begin, end, run_type);
		return false;
	}
	return true;
}
#endif

bool Memory::UnmapPages(Paging& paging, stduint linear_address,
	stduint length, Spinlock* page_table_lock) {
	if (!length) return true;
	PhysicalRange removed;
	if (!NormalizeUnmapRequest(linear_address, length, removed)) return false;

	MutexLocal guard(&physical_mapping_records_mutex);
	PhysicalMappingRecord* reserved_records;
	if (!PrepareUnmapRecords(paging, removed, reserved_records)) return false;
	bool unmapped;
	if (page_table_lock) {
		SpinlockLocal page_guard(page_table_lock);
		unmapped = paging.Unmap(removed.begin, removed.end - removed.begin);
	}
	else {
		unmapped = paging.Unmap(removed.begin, removed.end - removed.begin);
	}
	if (!unmapped) {
		ReleasePhysicalMappingRecords(reserved_records);
		return false;
	}
	CommitUnmapRecords(paging, removed, reserved_records);
	return true;
}

void Memory::ReleasePagingMappings(Paging& paging) {
	MutexLocal guard(&physical_mapping_records_mutex);
	PhysicalMappingRecord** link = &physical_mapping_records;
	while (*link) {
		PhysicalMappingRecord* record = *link;
		if (record->paging != &paging) {
			link = &record->next;
			continue;
		}
		*link = record->next;
		mempool.deallocate(record, sizeof(PhysicalMappingRecord));
	}
}

static bool build_initial_page_plan(const InitialPageRegion* source, stduint source_count, InitialPageRegion* plan, stduint plan_capacity, stduint& plan_count) {
	plan_count = 0;
	if ((source_count && !source) || (plan_capacity && !plan)) return false;
	for0(i, source_count) {
		InitialPageRegion normalized;
		if (!NormalizeInitialPageRegion(source[i], normalized)) {
			plogerro("[PAGING] Invalid source region %u: %[x]..%[x] type=%[x] permissions=%[x]",
				i, source[i].begin, source[i].end, source[i].type,
				source[i].permissions);
			plan_count = 0;
			return false;
		}
		if (!OverlayInitialPageRegion(plan, plan_capacity,
			plan_count, normalized)) {
			plogerro("[PAGING] Initial region plan capacity exhausted at source %u: count=%u capacity=%u",
				i, plan_count, plan_capacity);
			plan_count = 0;
			return false;
		}
	}
	return true;
}

static bool map_initial_page_regions(Paging& paging, const InitialPageRegion* plan, stduint plan_count) {
	if (plan_count && !plan) return false;
	for0(i, plan_count) {
		InitialPageRegion normalized;
		if (!NormalizeInitialPageRegion(plan[i], normalized) ||
			normalized.begin != plan[i].begin || normalized.end != plan[i].end ||
			normalized.type != plan[i].type ||
			normalized.permissions != plan[i].permissions) {
			plogerro("[PAGING] Invalid initial region %u: %[x]..%[x] type=%[x] permissions=%[x]",
				i, plan[i].begin, plan[i].end, plan[i].type,
				plan[i].permissions);
			return false;
		}
		if (i && (plan[i - 1].end > plan[i].begin ||
			(plan[i - 1].end == plan[i].begin &&
				InitialPageRegionAttributesEqual(plan[i - 1], plan[i])))) {
			plogerro("[PAGING] Invalid initial region ordering at %u: previous-end=%[x] begin=%[x]",
				i, plan[i - 1].end, plan[i].begin);
			return false;
		}
	}
	for0(i, plan_count) {
		if (!Memory::MapPages(paging, plan[i].begin, plan[i].begin,
			plan[i].end - plan[i].begin, PAGESIZE_ANY,
			plan[i].permissions | plan[i].type | PGOPTS_omit_warnings)) {
			plogerro("[PAGING] Failed initial region %u: %[x]..%[x] type=%[x] permissions=%[x]",
				i, plan[i].begin, plan[i].end, plan[i].type,
				plan[i].permissions);
			return false;
		}
	}
	return true;
}







#if _MCCA == 0x8632
_PACKED(struct) X86BootMemoryEntry {
	uint64 address;
	uint64 length;
	uint32 type;
};

// Convert the active x86 boot memory map and device windows into a page plan.
static bool build_x86_initial_page_plan(stduint boot_magic, const byte* boot_info, InitialPageRegion*& plan, stduint& plan_count, stduint& plan_storage_bytes) {
	plan = nullptr;
	plan_count = 0;
	plan_storage_bytes = 0;
	constexpr stduint boot_entry_capacity = 20;
	constexpr stduint source_capacity = boot_entry_capacity + 5;
	constexpr stduint plan_capacity = source_capacity * 2;
	constexpr stduint source_bytes =
		source_capacity * sizeof(InitialPageRegion);
	constexpr stduint plan_bytes =
		plan_capacity * sizeof(InitialPageRegion);
	auto source = (InitialPageRegion*)mempool.allocate(source_bytes, 4);
	plan = (InitialPageRegion*)mempool.allocate(plan_bytes, 4);
	if (!source || source == (InitialPageRegion*)~_IMM0 ||
		!plan || plan == (InitialPageRegion*)~_IMM0) {
		plogerro("[PAGING] Failed to allocate x86 initial plan storage: source=%[x] plan=%[x]",
			source, plan);
		if (source && source != (InitialPageRegion*)~_IMM0)
			mempool.deallocate(source, source_bytes);
		if (plan && plan != (InitialPageRegion*)~_IMM0)
			mempool.deallocate(plan, plan_bytes);
		plan = nullptr;
		return false;
	}
	auto fail = [&]() -> bool {
		mempool.deallocate(source, source_bytes);
		mempool.deallocate(plan, plan_bytes);
		plan = nullptr;
		plan_count = 0;
		return false;
	};

	stduint source_count = 0;
	const stduint permissions = PGPROP_present | PGPROP_writable;
	auto append = [&](stduint begin, stduint end, stduint type) -> bool {
		if (source_count >= source_capacity || begin >= end) return false;
		source[source_count++] = {begin, end, type, permissions};
		return true;
	};
	auto append_entry = [&](uint64 address, uint64 length,
		uint32 memory_type) -> bool {
		if (!length || address > uint64(~_IMM0) ||
			length > uint64(~_IMM0) - address) return true;
		const uint64 end64 = address + length;
		if (end64 > uint64(~_IMM0)) return true;
		stduint type;
		if (memory_type == 1) type = PGTYPE_writeback;
		else if (memory_type == 3 || memory_type == 4)
			type = PGTYPE_device;
		else return true;
		const stduint begin = ceilAlign(PAGE_SIZE, stduint(address));
		const stduint end = floorAlign(PAGE_SIZE, stduint(end64));
		return begin >= end || append(begin, end, type);
	};
	if (boot_magic == MULTIBOOT2_BOOTLOADER_MAGIC) {
		if (!boot_info) {
			plogerro("[PAGING] Missing x86 Multiboot2 information");
			return fail();
		}
		const stduint info_begin = _IMM(boot_info);
		const stduint info_size = *(const uint32*)boot_info;
		if (info_size < 16 || info_size > ~_IMM0 - info_begin) {
			plogerro("[PAGING] Invalid x86 Multiboot2 information: address=%[x] size=%u",
				info_begin, info_size);
			return fail();
		}
		const stduint info_end = info_begin + info_size;
		const multiboot_tag* tag = (const multiboot_tag*)(info_begin + 8);
		const multiboot_tag_mmap* memory_map = nullptr;
		while (_IMM(tag) + sizeof(multiboot_tag) <= info_end &&
			tag->type != MULTIBOOT_TAG_TYPE_END) {
			if (tag->size < sizeof(multiboot_tag) ||
				tag->size > info_end - _IMM(tag)) {
				plogerro("[PAGING] Invalid x86 Multiboot2 tag: address=%[x] type=%u size=%u",
					_IMM(tag), tag->type, tag->size);
				return fail();
			}
			if (tag->type == MULTIBOOT_TAG_TYPE_MMAP)
				memory_map = (const multiboot_tag_mmap*)tag;
			const stduint next = _IMM(tag) + ((tag->size + 7) & ~7);
			if (next <= _IMM(tag) || next > info_end) {
				plogerro("[PAGING] Invalid next x86 Multiboot2 tag: current=%[x] next=%[x] end=%[x]",
					_IMM(tag), next, info_end);
				return fail();
			}
			tag = (const multiboot_tag*)next;
		}
		if (!memory_map ||
			memory_map->entry_size < sizeof(multiboot_mmap_entry) ||
			memory_map->size < sizeof(multiboot_tag_mmap)) {
			plogerro("[PAGING] Invalid x86 Multiboot2 memory map: map=%[x] entry-size=%u size=%u",
				memory_map, memory_map ? memory_map->entry_size : 0,
				memory_map ? memory_map->size : 0);
			return fail();
		}
		const stduint map_end = _IMM(memory_map) + memory_map->size;
		for (stduint iter = _IMM(memory_map->entries);
			iter + sizeof(multiboot_mmap_entry) <= map_end;
			iter += memory_map->entry_size) {
			auto entry = (const multiboot_mmap_entry*)iter;
			if (!append_entry(entry->addr, entry->len, entry->type)) {
				plogerro("[PAGING] Failed to append x86 Multiboot2 memory entry: address=%llX length=%llX type=%u",
					entry->addr, entry->len, entry->type);
				return fail();
			}
		}
	}
	else {
		if (!boot_info) {
			plogerro("[PAGING] Missing x86 BIOS memory information");
			return fail();
		}
		auto entries = (const X86BootMemoryEntry*)boot_info;
		for0(i, boot_entry_capacity) {
			const X86BootMemoryEntry& entry = entries[i];
			if (!entry.address && !entry.length) break;
			if (!append_entry(entry.address, entry.length, entry.type)) {
				plogerro("[PAGING] Failed to append x86 BIOS memory entry %u: address=%llX length=%llX type=%u",
					i, entry.address, entry.length, entry.type);
				return fail();
			}
		}
	}
	if (!append(0, 0x100000, PGTYPE_device) ||
		!append(kAcpiHighWindowBase,
			kAcpiHighWindowBase + kAcpiHighWindowSize, PGTYPE_device) ||
		!append(0xFEC00000, 0xFEC01000, PGTYPE_device) ||
		!append(0xFEE00000, 0xFEE01000, PGTYPE_device)) {
		plogerro("[PAGING] Failed to append fixed x86 device regions: source-count=%u capacity=%u",
			source_count, source_capacity);
		return fail();
	}
	if (acpi_pcie_ecam_available) {
		const uint64 bus_count = uint64(acpi_pcie_end_bus) -
			acpi_pcie_start_bus + 1;
		const uint64 ecam_end = acpi_pcie_ecam_base + (bus_count << 20);
		if (ecam_end < acpi_pcie_ecam_base ||
			ecam_end > uint64(~_IMM0) ||
			!append(stduint(acpi_pcie_ecam_base), stduint(ecam_end),
				PGTYPE_device)) {
			plogerro("[PAGING] Failed to append x86 ECAM region: begin=%llX end=%llX",
				acpi_pcie_ecam_base, ecam_end);
			return fail();
		}
	}
	if (!build_initial_page_plan(source, source_count, plan,
		plan_capacity, plan_count)) {
		plogerro("[PAGING] Failed to merge x86 initial regions: source-count=%u capacity=%u",
			source_count, plan_capacity);
		return fail();
	}
	mempool.deallocate(source, source_bytes);
	plan_storage_bytes = plan_bytes;
	return true;
}

#endif

#if _MCCA == 0x8664 && defined(_UEFI)
extern UefiData uefi_data;

// Append one normalized platform range before the common plan builder resolves overlaps.
static bool append_initial_page_source(InitialPageRegion* source, stduint capacity, stduint& count, stduint begin, stduint end, stduint type, stduint permissions) {
	if (count >= capacity || begin >= end) return false;
	source[count++] = {begin, end, type, permissions};
	return true;
}

// Convert the UEFI map and explicit platform devices into a complete mapping plan.
static bool build_uefi_initial_page_plan(const MemoryMap& memory_map, InitialPageRegion*& plan, stduint& plan_count, stduint& plan_storage_bytes) {
	plan = nullptr;
	plan_count = 0;
	plan_storage_bytes = 0;
	if (!memory_map.buffer || memory_map.descriptor_size < sizeof(MemoryDescriptor) ||
		!memory_map.map_size || memory_map.map_size % memory_map.descriptor_size)
		return false;
	const stduint descriptor_count = memory_map.map_size /
		memory_map.descriptor_size;
	if (descriptor_count > (~_IMM0 - 4) ||
		descriptor_count + 4 > ~_IMM0 / 2) return false;
	const stduint source_capacity = descriptor_count + 4;
	const stduint plan_capacity = source_capacity * 2;
	if (source_capacity > ~_IMM0 / sizeof(InitialPageRegion) ||
		plan_capacity > ~_IMM0 / sizeof(InitialPageRegion)) return false;
	const stduint source_bytes = source_capacity * sizeof(InitialPageRegion);
	const stduint plan_bytes = plan_capacity * sizeof(InitialPageRegion);
	auto source = (InitialPageRegion*)mempool.allocate(source_bytes, 4);
	plan = (InitialPageRegion*)mempool.allocate(plan_bytes, 4);
	if (!source || source == (InitialPageRegion*)~_IMM0 ||
		!plan || plan == (InitialPageRegion*)~_IMM0) {
		if (source && source != (InitialPageRegion*)~_IMM0)
			mempool.deallocate(source, source_bytes);
		if (plan && plan != (InitialPageRegion*)~_IMM0)
			mempool.deallocate(plan, plan_bytes);
		plan = nullptr;
		return false;
	}
	auto fail = [&]() -> bool {
		mempool.deallocate(source, source_bytes);
		mempool.deallocate(plan, plan_bytes);
		plan = nullptr;
		plan_count = 0;
		plan_storage_bytes = 0;
		return false;
	};

	const stduint permissions = PGPROP_present | PGPROP_writable;
	stduint source_count = 0;
	const stduint map_begin = _IMM(memory_map.buffer);
	if (memory_map.map_size > ~_IMM0 - map_begin) return fail();
	const stduint map_end = map_begin + memory_map.map_size;
	for (stduint iter = map_begin; iter < map_end;
		iter += memory_map.descriptor_size) {
		auto desc = reinterpret_cast<const MemoryDescriptor*>(iter);
		if (desc->number_of_pages > uint64(~_IMM0) / UEFIPageSize) continue;
		const stduint begin = desc->physical_start;
		const stduint length = stduint(desc->number_of_pages * UEFIPageSize);
		if (!length || length > ~_IMM0 - begin) continue;
		const stduint end = begin + length;
		stduint type;
		switch (static_cast<MemoryType>(desc->type)) {
		case MemoryType::EfiLoaderCode:
		case MemoryType::EfiLoaderData:
		case MemoryType::EfiBootServicesCode:
		case MemoryType::EfiBootServicesData:
		case MemoryType::EfiConventionalMemory:
			type = PGTYPE_writeback;
			break;
		case MemoryType::EfiACPIReclaimMemory:
		case MemoryType::EfiACPIMemoryNVS:
		case MemoryType::EfiMemoryMappedIO:
		case MemoryType::EfiMemoryMappedIOPortSpace:
			type = PGTYPE_device;
			break;
		default:
			continue;
		}
		if (!append_initial_page_source(source, source_capacity, source_count,
			begin, end, type, permissions)) return fail();
	}

	if (!append_initial_page_source(source, source_capacity, source_count,
		0xFEC00000, 0xFEC01000, PGTYPE_device, permissions) ||
		!append_initial_page_source(source, source_capacity, source_count,
			0xFEE00000, 0xFEE01000, PGTYPE_device, permissions)) return fail();
	if (acpi_pcie_ecam_available) {
		const uint64 bus_count = uint64(acpi_pcie_end_bus) -
			acpi_pcie_start_bus + 1;
		const uint64 ecam_length = bus_count << 20;
		const uint64 ecam_end = acpi_pcie_ecam_base + ecam_length;
		if (ecam_end < acpi_pcie_ecam_base ||
			ecam_end > uint64(~_IMM0) ||
			!append_initial_page_source(source, source_capacity, source_count,
				stduint(acpi_pcie_ecam_base), stduint(ecam_end),
				PGTYPE_device, permissions)) return fail();
	}

	const uint64 framebuffer_begin = uint64(
		_IMM(uefi_data.frame_buffer_config.frame_buffer));
	const uint64 framebuffer_pitch = uint64(
		uefi_data.frame_buffer_config.pixels_per_scan_line) * 4;
	const uint64 framebuffer_height =
		uefi_data.frame_buffer_config.vertical_resolution;
	if (!framebuffer_begin || !framebuffer_pitch || !framebuffer_height ||
		framebuffer_pitch > ~uint64(0) / framebuffer_height) return fail();
	const uint64 framebuffer_length = framebuffer_pitch * framebuffer_height;
	if (framebuffer_length > ~uint64(0) - framebuffer_begin ||
		framebuffer_begin > uint64(~_IMM0)) return fail();
	const uint64 framebuffer_end_raw = framebuffer_begin + framebuffer_length;
	if (framebuffer_end_raw > uint64(~_IMM0) - (PAGE_SIZE - 1) ||
		(framebuffer_begin & (PAGE_SIZE - 1))) return fail();
	const stduint framebuffer_end = stduint(
		(framebuffer_end_raw + PAGE_SIZE - 1) & ~uint64(PAGE_SIZE - 1));
	const stduint framebuffer_type = framebuffer_range_supports_wc(
		stduint(framebuffer_begin), framebuffer_end) ?
		PGTYPE_writecombine : PGTYPE_device;
	if (!append_initial_page_source(source, source_capacity, source_count,
		stduint(framebuffer_begin), framebuffer_end,
		framebuffer_type, permissions)) return fail();

	if (!build_initial_page_plan(source, source_count, plan,
		plan_capacity, plan_count)) return fail();
	mempool.deallocate(source, source_bytes);
	plan_storage_bytes = plan_bytes;
	return true;
}



#endif

bool Memory::BuildInitialPagePlan(stduint boot_magic, const void* boot_info,
	InitialPageRegion*& plan, stduint& plan_count, stduint& plan_storage_bytes) {
	plan = nullptr;
	plan_count = 0;
	plan_storage_bytes = 0;
	#if _MCCA == 0x8632
	return build_x86_initial_page_plan(boot_magic, (const byte*)boot_info,
		plan, plan_count, plan_storage_bytes);
	#elif _MCCA == 0x8664 && defined(_UEFI)
	if (boot_magic != 'UEFI' || !boot_info) return false;
	return build_uefi_initial_page_plan(*(const MemoryMap*)boot_info,
		plan, plan_count, plan_storage_bytes);
	#else
	(void)boot_magic;
	(void)boot_info;
	return false;
	#endif
}

bool Memory::MapInitialPagePlan(Paging& paging,
	const InitialPageRegion* plan, stduint plan_count) {
	if (!paging.root_level_page ||
		!map_initial_page_regions(paging, plan, plan_count)) return false;
	#if _MCCA == 0x8632
	const stduint high_mapping_base = 0x80000000;
	const stduint high_mapping_limit = 0x10000000;
	#elif _MCCA == 0x8664 && defined(_UEFI)
	const stduint high_mapping_base = 0x0000FFFFC0000000ull;
	const stduint high_mapping_limit = 0x40000000ull -
		_IMM1S(PAGESIZE_2MB);
	#endif
	#if _MCCA == 0x8632 || (_MCCA == 0x8664 && defined(_UEFI))
	for0(i, plan_count) {
		if (plan[i].begin >= high_mapping_limit) continue;
		const stduint end = minof(plan[i].end, high_mapping_limit);
		if (!MapPages(paging, high_mapping_base + plan[i].begin,
			plan[i].begin, end - plan[i].begin, PAGESIZE_ANY,
			plan[i].permissions | plan[i].type | PGOPTS_omit_warnings)) {
			plogerro("[PAGING] Failed initial high alias %u: VA=%[x] PA=%[x] length=%[u] type=%[x]",
				i, high_mapping_base + plan[i].begin, plan[i].begin,
				end - plan[i].begin, plan[i].type);
			return false;
		}
	}
	#endif
	return true;
}

#endif
