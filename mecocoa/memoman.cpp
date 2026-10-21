// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include "c/arith.h"
// for x86, one slice should begin with 0x100000 (above F000:FFFF)

_ESYM_C Handler_t FILE_ENTO, FILE_ENDO;

Memory mem;
stduint Memory::total_memsize = 0;
bool map_ready = false;


Mempool mempool0 = {};
LockedAllocator mempool(&mempool0);

#if 0 // for small flash board
#define mempool (*pmempool)
#endif

// - Memory::clear_bss
void Memory::clear_bss() {
	MemSet(&BSS_ENTO, &BSS_ENDO - &BSS_ENTO, 0);
}



// ---- x86 ----
#ifdef _ARC_x86 // x86:
Letvar(Memory::p_basic, byte*, 0x1000); //.. 0x7000
Letvar(Memory::p_ext, byte*, mem_area_exten_beg);
stduint Memory::total_mem = 0;
usize Memory::areax_size = 0;

// Allocate Physical-Continuous Memory
// unit: x86 Page 0x1000
void* Memory::physical_allocate(usize siz) {
	if (siz & 0xFFF) siz = (siz & ~_IMM(0xFFF)) + 0x1000;
	void* ret = nil;
	if (map_ready) {
		ret = mem.allocate(siz, 12);
	}
	else { // not support pg-mapping
		void* ret = p_ext;
		p_ext += siz;
		return ret;
	}
	return ret;
}

#endif


// ---- LOCKED ----

Spinlock mempool_lock;

#if (_MCCA & 0xFFFF) == 0x2032
// Cortex-M: the slice left after a block starts at header+size, keep it 4-aligned
#define MEMPOOL_BLOCK_ALIGN 4
#else
#define MEMPOOL_BLOCK_ALIGN 1
#endif


// ---------------------------------------------------------
// VirtualMemory Implementation
// ---------------------------------------------------------

#if CONFIG_ENABLE_MMU
#if _MCCA == 0x8632
struct TLBShootdownMailbox {
	stduint address = 0;
	uni::Atomic<stduint> requested = 0;
	uni::Atomic<stduint> acknowledged = 0;
};

static TLBShootdownMailbox tlb_shootdown_mailboxes[PCU_CORES_MAX];
// One owner publishes requests; waiters service their local mailbox before retrying.
static uni::Atomic<byte> tlb_shootdown_active = 0;
static stduint tlb_shootdown_sequence = 0;

static void ServiceLocalTLBShootdown() {
	const stduint core_id = Taskman::getID();
	if (core_id >= PCU_CORES_MAX) return;
	auto& mailbox = tlb_shootdown_mailboxes[core_id];
	const stduint requested = mailbox.requested.load(uni::MemoryOrder_Acquire);
	if (!requested ||
		mailbox.acknowledged.load(uni::MemoryOrder_Relaxed) == requested) return;
	RefreshVirtualAddress(mailbox.address);
	// User execution resumes only after the interrupt return or context switch reloads CR3.
	mailbox.acknowledged.store(requested, uni::MemoryOrder_Release);
}
#endif

void Memory::InvalidateProcessPage(ProcessBlock* pb, stduint address) {
	address &= ~_IMM(PAGE_SIZE - 1);
	RefreshVirtualAddress(address);
	#if _MCCA == 0x8632 && _SYS_MULTICORE
	if (!pb || !pb->paging.root_level_page || Taskman::PCU_CORES <= 1 ||
		!IC.getType()) return;

	byte expected = 0;
	while (!tlb_shootdown_active.compare_exchange(expected, 1,
		uni::MemoryOrder_Acquire, uni::MemoryOrder_Relaxed)) {
		ServiceLocalTLBShootdown();
		expected = 0;
		_ASM volatile("pause" ::: "memory");
	}

	if (!(++tlb_shootdown_sequence)) ++tlb_shootdown_sequence;
	const stduint sequence = tlb_shootdown_sequence;
	const stduint current_core = Taskman::getID();
	bool targets[PCU_CORES_MAX] = {};
	for (stduint core_id = 0; core_id < PCU_CORES_MAX; core_id++) {
		if (core_id == current_core) continue;
		auto percore = Taskman::PCU_CORES_PERCORE[core_id];
		if (!percore || percore->state != CoreState::Online ||
			percore->lapic_id == CORE_ID_INVALID) continue;
		auto& mailbox = tlb_shootdown_mailboxes[core_id];
		mailbox.address = address;
		mailbox.requested.store(sequence, uni::MemoryOrder_Release);
		targets[core_id] = true;
		Taskman::SendTLBShootdownIPI(core_id);
	}
	for (stduint core_id = 0; core_id < PCU_CORES_MAX; core_id++) {
		if (!targets[core_id]) continue;
		while (tlb_shootdown_mailboxes[core_id].acknowledged.load(
			uni::MemoryOrder_Acquire) != sequence) {
			ServiceLocalTLBShootdown();
			_ASM volatile("pause" ::: "memory");
		}
	}
	tlb_shootdown_active.store(0, uni::MemoryOrder_Release);
	#else
	(void)pb;
	#endif
}

#if _MCCA == 0x8632
void Memory::HandleTLBShootdownIPI() {
	ServiceLocalTLBShootdown();
}
#endif

static void* ResolveAnonymousCowPage(ProcessBlock* pb, stduint address);
#endif

bool PrepareUserBuffer(ProcessBlock* pb, stduint address,
	size_t length, bool write, bool mapping_lock_held) {
	if (!pb || (!address && length) || length > ~_IMM0 - address) return false;
	if (!length) return true;
	#if CONFIG_ENABLE_MMU
	const stduint first_page = address & ~_IMM(0xFFF);
	const stduint last_page = (address + length - 1) & ~_IMM(0xFFF);
	for (stduint page = first_page;; page += 0x1000) {
		if (_IMM(pb->paging[page]) == ~_IMM0 &&
			!pb->EnsureVmaPage(page, write ? VirtualMemoryAccess::Write :
				VirtualMemoryAccess::Read)) return false;
		pageint page_index = page;
		PageEntry* entry = pb->paging.getEntry(page_index);
		stduint properties;
		if (!entry || _IMM(entry) == ~_IMM0 ||
			!entry->DecodeLeafProperties(page_index.crt_level, properties) ||
			!(properties & PGPROP_present) ||
			!(properties & PGPROP_user_access)) return false;
		if (write && !(properties & PGPROP_writable)) {
			void* physical = nullptr;
			if (mapping_lock_held) {
				physical = ResolveAnonymousCowPage(pb, page);
			}
			else {
				MutexLocal mapping_guard(&pb->mutex_mapping);
				physical = ResolveAnonymousCowPage(pb, page);
			}
			if (!physical) return false;
			page_index = page;
			entry = pb->paging.getEntry(page_index);
			if (!entry || _IMM(entry) == ~_IMM0 ||
				!entry->DecodeLeafProperties(page_index.crt_level, properties) ||
				!(properties & PGPROP_present) ||
				!(properties & PGPROP_user_access) ||
				!(properties & PGPROP_writable)) return false;
		}
		if (page == last_page) break;
	}
	#else
	(void)write;
	(void)mapping_lock_held;
	#endif
	return true;
}

#if CONFIG_ENABLE_MMU
bool ProcessBlock::FindFreeVmaRange(stduint length, stduint& address) {
	#if _MCCA == 0x8664
	const stduint user_end = 0x0000800000000000ull;
	#else
	const stduint user_end = 0x80000000;
	#endif
	if (!length || (length & 0xFFF) ||
		heapbtm < 0x1000 || heapbtm > user_end - 0xFFF) return false;
	stduint candidate = (heapbtm + 0xFFF) & ~_IMM(0xFFF);
	while (candidate < user_end && length <= user_end - candidate) {
		bool occupied = false;
		for (stduint i = 0; i < vmas.Count(); i++) {
			const auto& existing = vmas[i];
			if (candidate < existing.vm_end && existing.vm_start < candidate + length) {
				candidate = existing.vm_end;
				occupied = true;
				break;
			}
		}
		if (occupied) continue;
		for (stduint page = candidate; page < candidate + length; page += 0x1000) {
			if (_IMM(paging[page]) != ~_IMM0) {
				candidate = page + 0x1000;
				occupied = true;
				break;
			}
		}
		if (!occupied) {
			address = candidate;
			return true;
		}
	}
	return false;
}

bool ProcessBlock::InsertVmaSorted(const VirtualMemoryArea& vma) {
	if (vma.vm_start >= vma.vm_end || ((vma.vm_start | vma.vm_end) & 0xFFF)) return false;
	for (stduint i = 0; i < vmas.Count(); i++) {
		const auto& existing = vmas[i];
		if (vma.vm_start < existing.vm_end && existing.vm_start < vma.vm_end) return false;
	}
	if (!vmas.Append(vma)) return false;
	for (stduint i = vmas.Count() - 1; i &&
		vmas[i].vm_start < vmas[i - 1].vm_start; --i) {
		vmas.Exchange(i, i - 1);
	}
	return true;
}

struct VmaFileDeleter {
	void operator()(uni::vfs_file* file) const {
		if (!file) return;
		if (file->f_inode) {
			Filesys::Close(file);
		}
		else delete file;
	}
};

using VmaFileOwner = uni::UniquePtr<uni::vfs_file, VmaFileDeleter>;

struct VmaPageReleasePlan {
	stduint address = 0;
	void* physical = nullptr;
	stduint properties = 0;
	stduint writeback_index = ~_IMM0;
	stduint file_offset = 0;
	bool release_physical = false;
};

struct VmaWritebackPlan {
	VmaFileOwner file;
};

struct VmaDeviceUnmapPlan {
	stduint address = 0;
	stduint length = 0;
};

static uni::vfs_file* CloneVmaFile(uni::vfs_file* source) {
	if (!source) return nullptr;
	auto* result = new uni::vfs_file(*source);
	if (!result) return nullptr;
	if (result->f_inode) result->f_inode->ref_count++;
	return result;
}

// Caller holds vma_lock.
static void MergeAdjacentAnonymousVmas(ProcessBlock* pb) {
	for (stduint i = 0; i + 1 < pb->vmas.Count();) {
		auto& left = pb->vmas[i];
		const auto& right = pb->vmas[i + 1];
		if (left.vm_type == VirtualMemoryAreaType::Anonymous &&
			right.vm_type == VirtualMemoryAreaType::Anonymous &&
			left.vm_end == right.vm_start && left.vm_flags == right.vm_flags &&
			left.vm_mode == right.vm_mode) {
			left.vm_end = right.vm_end;
			pb->vmas.Remove(i + 1, 1);
		}
		else i++;
	}
}

bool ProcessBlock::UnmapVmaRange(stduint address, stduint length) {
	if (!length || (address & (PAGE_SIZE - 1)) ||
		(length & (PAGE_SIZE - 1)) || length > ~_IMM0 - address) return false;
	const stduint end = address + length;
	stduint page_capacity = 0;
	stduint writeback_capacity = 0;
	stduint released_file_capacity = 0;
	stduint device_capacity = 0;
	bool split_file_required = false;
	{
		SpinlockLocal guard(&vma_lock);
		for (stduint i = 0; i < vmas.Count(); i++) {
			const auto& vma = vmas[i];
			const stduint overlap_begin = maxof(address, vma.vm_start);
			const stduint overlap_end = minof(end, vma.vm_end);
			if (overlap_begin >= overlap_end) continue;
			const stduint pages = (overlap_end - overlap_begin) / PAGE_SIZE;
			if (pages > ~_IMM0 - page_capacity) return false;
			page_capacity += pages;
			if (vma.vm_type == VirtualMemoryAreaType::Device) device_capacity++;
			if (vma.vm_type == VirtualMemoryAreaType::File && vma.vfile &&
				vma.vm_mode == VirtualMemoryAreaMode::Shared &&
				(vma.vm_flags & PGPROP_writable)) writeback_capacity++;
			if (vma.vm_start >= address && vma.vm_end <= end &&
				vma.vm_type == VirtualMemoryAreaType::File && vma.vfile)
				released_file_capacity++;
			if (vma.vm_start < address && vma.vm_end > end &&
				vma.vm_type == VirtualMemoryAreaType::File && vma.vfile)
				split_file_required = true;
		}
	}
	if (page_capacity > ~_IMM0 / sizeof(VmaPageReleasePlan) ||
		writeback_capacity > ~_IMM0 / sizeof(VmaWritebackPlan) ||
		released_file_capacity > ~_IMM0 / sizeof(VmaFileOwner) ||
		device_capacity > ~_IMM0 / sizeof(VmaDeviceUnmapPlan)) return false;

	auto pages = page_capacity ?
		uni::make_unique<VmaPageReleasePlan[]>(page_capacity) :
		uni::UniquePtr<VmaPageReleasePlan[]>();
	auto writebacks = writeback_capacity ?
		uni::make_unique<VmaWritebackPlan[]>(writeback_capacity) :
		uni::UniquePtr<VmaWritebackPlan[]>();
	auto released_files = released_file_capacity ?
		uni::make_unique<VmaFileOwner[]>(released_file_capacity) :
		uni::UniquePtr<VmaFileOwner[]>();
	auto devices = device_capacity ?
		uni::make_unique<VmaDeviceUnmapPlan[]>(device_capacity) :
		uni::UniquePtr<VmaDeviceUnmapPlan[]>();
	if ((page_capacity && !pages) || (writeback_capacity && !writebacks) ||
		(released_file_capacity && !released_files) ||
		(device_capacity && !devices)) return false;

	VmaFileOwner split_file;
	stduint page_count = 0;
	stduint writeback_count = 0;
	stduint device_count = 0;
	bool prepared = true;
	{
		SpinlockLocal guard(&vma_lock);
		for (stduint i = 0; i < vmas.Count() && prepared; i++) {
			const auto& vma = vmas[i];
			const stduint overlap_begin = maxof(address, vma.vm_start);
			const stduint overlap_end = minof(end, vma.vm_end);
			if (overlap_begin >= overlap_end) continue;
			stduint writeback_index = ~_IMM0;
			if (vma.vm_type == VirtualMemoryAreaType::File && vma.vfile &&
				vma.vm_mode == VirtualMemoryAreaMode::Shared &&
				(vma.vm_flags & PGPROP_writable)) {
				writebacks[writeback_count].file.reset(CloneVmaFile(vma.vfile));
				if (!writebacks[writeback_count].file) {
					prepared = false;
					break;
				}
				writeback_index = writeback_count++;
			}
			if (vma.vm_type == VirtualMemoryAreaType::Device) {
				devices[device_count].address = overlap_begin;
				devices[device_count].length = overlap_end - overlap_begin;
				device_count++;
			}
			for (stduint page = overlap_begin; page < overlap_end; page += PAGE_SIZE) {
				auto& item = pages[page_count++];
				item.address = page;
				item.properties = vma.vm_flags & (PGPROP_present |
					PGPROP_writable | PGPROP_user_access | PGPROP_nonexecutable);
				item.writeback_index = writeback_index;
				item.file_offset = vma.file_offset + page - vma.vm_start;
				item.release_physical = vma.vm_type != VirtualMemoryAreaType::Device;
			}
			if (split_file_required && !split_file &&
				vma.vm_start < address && vma.vm_end > end &&
				vma.vm_type == VirtualMemoryAreaType::File && vma.vfile) {
				split_file.reset(CloneVmaFile(vma.vfile));
				if (!split_file) prepared = false;
			}
		}
	}
	if (!prepared || page_count != page_capacity ||
		writeback_count != writeback_capacity || device_count != device_capacity ||
		(split_file_required && !split_file)) return false;

	for (stduint i = 0; i < device_count; i++) {
		if (!Memory::UnmapPages(paging, devices[i].address,
			devices[i].length, &vma_lock)) return false;
	}

	stduint released_file_count = 0;
	bool committed = true;
	{
		SpinlockLocal guard(&vma_lock);
		for (stduint i = 0; i < page_count; i++) {
			auto& item = pages[i];
			if (!item.release_physical) continue;
			item.physical = paging[item.address];
		}
		for (stduint i = 0; i < page_count; i++) {
			const auto& item = pages[i];
			if (!item.release_physical || item.physical == (void*)~_IMM0) continue;
			if (!paging.Unmap(item.address, PAGE_SIZE)) {
				committed = false;
				break;
			}
		}
		if (committed) {
			for (stduint i = 0; i < vmas.Count();) {
				auto& vma = vmas[i];
				if (vma.vm_start >= address && vma.vm_end <= end) {
					if (vma.vm_type == VirtualMemoryAreaType::File && vma.vfile) {
						released_files[released_file_count++].reset(vma.vfile);
						vma.vfile = nullptr;
					}
					vmas.Remove(i, 1);
				}
				else if (vma.vm_start < address && vma.vm_end > end) {
					VirtualMemoryArea right = vma;
					const stduint original_end = vma.vm_end;
					right.vm_start = end;
					if (vma.vm_type == VirtualMemoryAreaType::File && vma.vfile) {
						right.vfile = split_file.get();
						right.file_offset = vma.file_offset + end - vma.vm_start;
					}
					vma.vm_end = address;
					if (!InsertVmaSorted(right)) {
						vma.vm_end = original_end;
						committed = false;
						break;
					}
					if (right.vfile) split_file.release();
					i += 2;
				}
				else if (vma.vm_start < address && vma.vm_end > address) {
					vma.vm_end = address;
					i++;
				}
				else if (vma.vm_start < end && vma.vm_end > end) {
					if (vma.vm_type == VirtualMemoryAreaType::File && vma.vfile)
						vma.file_offset += end - vma.vm_start;
					vma.vm_start = end;
					i++;
				}
				else i++;
			}
		}
		if (committed) MergeAdjacentAnonymousVmas(this);
		else {
			for (stduint i = 0; i < page_count; i++) {
				const auto& item = pages[i];
				if (!item.release_physical || !item.physical ||
					item.physical == (void*)~_IMM0) continue;
				paging.Map(item.address, (stduint)item.physical, PAGE_SIZE,
					PAGESIZE_4KB, item.properties | PGOPTS_omit_warnings);
			}
		}
	}
	for (stduint i = 0; i < page_count; i++) RefreshVirtualAddress(pages[i].address);
	if (!committed) return false;

	for (stduint i = 0; i < page_count; i++) {
		const auto& item = pages[i];
		if (!item.physical || item.physical == (void*)~_IMM0) continue;
		if (item.writeback_index != ~_IMM0) {
			auto* file = writebacks[item.writeback_index].file.get();
			if (file && file->f_inode && item.file_offset < file->f_inode->i_size) {
				const stduint write_length = minof(_IMM(PAGE_SIZE),
					file->f_inode->i_size - item.file_offset);
				file->f_pos = item.file_offset;
				Filesys::Write(file, (const void*)mglb(item.physical), write_length);
			}
		}
		if (item.release_physical &&
			!Memory::ReleaseManagedPageMapping(_IMM(item.physical)))
			plogerro("Missing managed VMA page at physical address %[x]",
				item.physical);
	}
	KASSERT(released_file_count == released_file_capacity);
	return true;
}

bool ProcessBlock::ReleaseAllVmas() {
	stduint begin = 0;
	stduint end = 0;
	{
		SpinlockLocal guard(&vma_lock);
		if (!vmas.Count()) return true;
		begin = vmas[0].vm_start;
		end = vmas[0].vm_end;
		for (stduint i = 1; i < vmas.Count(); i++) {
			begin = minof(begin, vmas[i].vm_start);
			end = maxof(end, vmas[i].vm_end);
		}
	}
	return end > begin && UnmapVmaRange(begin, end - begin);
}

static bool VmaAllowsAccess(const VirtualMemoryArea& vma,
	VirtualMemoryAccess access) {
	if (!(vma.vm_flags & PGPROP_present) ||
		!(vma.vm_flags & PGPROP_user_access)) return false;
	if (access == VirtualMemoryAccess::Write)
		return !!(vma.vm_flags & PGPROP_writable);
	if (access == VirtualMemoryAccess::Execute)
		return !(vma.vm_flags & PGPROP_nonexecutable);
	return true;
}

static bool PageAllowsAccess(ProcessBlock* pb, stduint address,
	VirtualMemoryAccess access, void*& physical) {
	pageint page = address;
	PageEntry* entry = pb->paging.getEntry(page);
	stduint properties;
	if (!entry || _IMM(entry) == ~_IMM0 ||
		!entry->DecodeLeafProperties(page.crt_level, properties) ||
		!(properties & PGPROP_present) ||
		!(properties & PGPROP_user_access) ||
		(access == VirtualMemoryAccess::Write && !(properties & PGPROP_writable)) ||
		(access == VirtualMemoryAccess::Execute &&
			(properties & PGPROP_nonexecutable))) return false;
	physical = pb->paging[address];
	return physical != (void*)~_IMM0;
}

static void ReleaseVmaReadFile(uni::vfs_file* file) {
	if (!file) return;
	Filesys::Close(file);
}

void* ProcessBlock::EnsureVmaPage(stduint address, VirtualMemoryAccess access) {
	address &= ~_IMM(0xFFF);
	VirtualMemoryAreaType expected_type = VirtualMemoryAreaType::Anonymous;
	VirtualMemoryAreaMode expected_mode = VirtualMemoryAreaMode::Private;
	stduint expected_flags = 0;
	stduint expected_file_offset = 0;
	bool file_backed = false;
	bool found = false;
	{
		SpinlockLocal guard(&vma_lock);
		for (stduint i = 0; i < vmas.Count(); i++) {
			const auto& vma = vmas[i];
			if (address < vma.vm_start || address >= vma.vm_end) continue;
			if (vma.vm_type == VirtualMemoryAreaType::Device ||
				!VmaAllowsAccess(vma, access)) return nullptr;
			void* physical;
			if (_IMM(paging[address]) != ~_IMM0)
				return PageAllowsAccess(this, address, access, physical) ? physical : nullptr;
			expected_type = vma.vm_type;
			expected_mode = vma.vm_mode;
			expected_flags = vma.vm_flags;
			found = true;
			file_backed = vma.vm_type == VirtualMemoryAreaType::File;
			if (file_backed) {
				if (!vma.vfile) return nullptr;
				expected_file_offset = address - vma.vm_start + vma.file_offset;
			}
			break;
		}
	}
	if (!found) return nullptr;

	uni::vfs_file* read_file = nullptr;
	if (file_backed) {
		read_file = new uni::vfs_file();
		if (!read_file) return nullptr;
		bool retained = false;
		bool raced = false;
		bool raced_allowed = false;
		void* raced_physical = nullptr;
		{
			SpinlockLocal guard(&vma_lock);
			for (stduint i = 0; i < vmas.Count(); i++) {
				const auto& vma = vmas[i];
				if (address < vma.vm_start || address >= vma.vm_end) continue;
				void* physical = nullptr;
				if (_IMM(paging[address]) != ~_IMM0) {
					raced = true;
					raced_allowed = PageAllowsAccess(this, address, access, physical);
					raced_physical = physical;
					break;
				}
				if (vma.vm_type != expected_type || vma.vm_mode != expected_mode ||
					vma.vm_flags != expected_flags || !VmaAllowsAccess(vma, access) ||
					!vma.vfile || address - vma.vm_start + vma.file_offset !=
						expected_file_offset) break;
				*read_file = *vma.vfile;
				if (read_file->f_inode) read_file->f_inode->ref_count++;
				retained = true;
				break;
			}
		}
		if (raced) {
			delete read_file;
			return raced_allowed ? raced_physical : nullptr;
		}
		if (!retained) {
			delete read_file;
			return nullptr;
		}
	}

	void* physical = mempool.allocate(0x1000, 12);
	if (!physical || physical == (void*)~_IMM0) {
		ReleaseVmaReadFile(read_file);
		return nullptr;
	}
	MemSet((void*)mglb(physical), 0, 0x1000);
	if (read_file && read_file->f_inode &&
		expected_file_offset < read_file->f_inode->i_size) {
		stduint read_length = minof(_IMM(0x1000),
			read_file->f_inode->i_size - expected_file_offset);
		read_file->f_pos = expected_file_offset;
		if (Filesys::Read(read_file, (void*)mglb(physical), read_length) !=
			(stdsint)read_length) {
			mempool.deallocate(physical, 0x1000);
			ReleaseVmaReadFile(read_file);
			return nullptr;
		}
	}
	if (!Memory::AdoptManagedPage(_IMM(physical), file_backed ?
		ManagedPageSource::File : ManagedPageSource::Anonymous)) {
		ReleaseVmaReadFile(read_file);
		return nullptr;
	}

	void* result = nullptr;
	{
		SpinlockLocal guard(&vma_lock);
		for (stduint i = 0; i < vmas.Count(); i++) {
			const auto& vma = vmas[i];
			if (address < vma.vm_start || address >= vma.vm_end) continue;
			if (!VmaAllowsAccess(vma, access)) break;
			if (_IMM(paging[address]) != ~_IMM0) {
				PageAllowsAccess(this, address, access, result);
				break;
			}
			if (vma.vm_type != expected_type || vma.vm_mode != expected_mode ||
				vma.vm_flags != expected_flags) break;
			if (file_backed && (!vma.vfile || !read_file ||
				vma.vfile->f_inode != read_file->f_inode ||
				address - vma.vm_start + vma.file_offset != expected_file_offset)) break;
			stduint properties = vma.vm_flags & (PGPROP_present |
				PGPROP_writable | PGPROP_user_access | PGPROP_nonexecutable);
			if (paging.Map(address, (stduint)physical, 0x1000,
				PAGESIZE_4KB, properties)) {
				RefreshVirtualAddress(address);
				result = physical;
				physical = nullptr;
			}
			break;
		}
	}
	if (physical) Memory::ReleaseManagedPageMapping(_IMM(physical));
	ReleaseVmaReadFile(read_file);
	return result;
}

static bool ReadAnonymousCowPage(ProcessBlock* pb, stduint address,
	stduint& physical_address, stduint& properties) {
	SpinlockLocal guard(&pb->vma_lock);
	for (stduint i = 0; i < pb->vmas.Count(); i++) {
		const auto& vma = pb->vmas[i];
		if (address < vma.vm_start || address >= vma.vm_end) continue;
		if (vma.vm_type != VirtualMemoryAreaType::Anonymous ||
			vma.vm_mode != VirtualMemoryAreaMode::Private ||
			!VmaAllowsAccess(vma, VirtualMemoryAccess::Write)) return false;
		pageint page = address;
		PageEntry* entry = pb->paging.getEntry(page);
		if (_IMM(entry) == ~_IMM0 || page.crt_level != 0 ||
			!entry->DecodeLeafProperties(page.crt_level, properties) ||
			!(properties & PGPROP_present) ||
			!(properties & PGPROP_user_access) ||
			(properties & PGPROP_writable)) return false;
		physical_address = _IMM(pb->paging[address]);
		return physical_address != ~_IMM0;
	}
	return false;
}

static bool ReplaceAnonymousCowPage(ProcessBlock* pb, stduint address,
	stduint expected_physical, stduint expected_properties,
	stduint replacement_physical) {
	SpinlockLocal guard(&pb->vma_lock);
	for (stduint i = 0; i < pb->vmas.Count(); i++) {
		const auto& vma = pb->vmas[i];
		if (address < vma.vm_start || address >= vma.vm_end) continue;
		if (vma.vm_type != VirtualMemoryAreaType::Anonymous ||
			vma.vm_mode != VirtualMemoryAreaMode::Private ||
			!VmaAllowsAccess(vma, VirtualMemoryAccess::Write)) return false;
		pageint page = address;
		PageEntry* entry = pb->paging.getEntry(page);
		stduint current_properties;
		if (_IMM(entry) == ~_IMM0 || page.crt_level != 0 ||
			!entry->DecodeLeafProperties(page.crt_level, current_properties) ||
			current_properties != expected_properties ||
			_IMM(pb->paging[address]) != expected_physical) return false;
		PageEntry replacement;
		if (!replacement.SetupAsLeaf(replacement_physical, page.crt_level,
			expected_properties | PGPROP_writable)) return false;
		entry->raw = replacement.raw;
		return true;
	}
	return false;
}

// The caller holds pb->mutex_mapping.
static void* ResolveAnonymousCowPage(ProcessBlock* pb, stduint address) {
	stduint old_physical;
	stduint old_properties;
	if (!ReadAnonymousCowPage(pb, address, old_physical, old_properties))
		return nullptr;

	ManagedPageInfo old_page;
	if (!Memory::QueryManagedPage(old_physical, old_page) ||
		old_page.source != ManagedPageSource::Anonymous ||
		!old_page.mapping_refs) return nullptr;

	if (old_page.mapping_refs == 1 && !old_page.cache_refs) {
		if (!ReplaceAnonymousCowPage(pb, address, old_physical,
			old_properties, old_physical)) return nullptr;
		Memory::InvalidateProcessPage(pb, address);
		return (void*)old_physical;
	}

	void* new_physical = mempool.allocate(PAGE_SIZE, 12);
	if (!new_physical || new_physical == (void*)~_IMM0) return nullptr;
	if (MemCopyP(new_physical, kernel_paging, (void*)old_physical,
		kernel_paging, PAGE_SIZE) != PAGE_SIZE) {
		mempool.deallocate(new_physical, PAGE_SIZE);
		return nullptr;
	}
	if (!Memory::AdoptManagedPage(_IMM(new_physical),
		ManagedPageSource::Anonymous)) return nullptr;
	if (!ReplaceAnonymousCowPage(pb, address, old_physical,
		old_properties, _IMM(new_physical))) {
		Memory::ReleaseManagedPageMapping(_IMM(new_physical));
		return nullptr;
	}
	Memory::InvalidateProcessPage(pb, address);
	if (!Memory::ReleaseManagedPageMapping(old_physical)) KASSERT(false);
	return new_physical;
}

void* ProcessBlock::ResolveVmaFault(stduint address,
	VirtualMemoryAccess access, bool protection_fault) {
	address &= ~_IMM(0xFFF);
	if (!protection_fault) {
		void* physical = EnsureVmaPage(address, access);
		if (physical || access != VirtualMemoryAccess::Write) return physical;
	}
	if (access != VirtualMemoryAccess::Write) return nullptr;
	MutexLocal mapping_guard(&mutex_mapping);
	return ResolveAnonymousCowPage(this, address);
}

extern "C" void* kernel_prefault_page(ProcessBlock* pb, stduint address,
	bool write) {
	return pb ? pb->EnsureVmaPage(address, write ?
		VirtualMemoryAccess::Write : VirtualMemoryAccess::Read) : nullptr;
}
#endif

void ProcessBlock::ReleaseLoadslices() {
	#if CONFIG_ENABLE_MMU
	for0a(i, load_slices) {
		if (!load_slices[i].length) continue;
		const stduint begin = load_slices[i].address & ~_IMM(PAGE_SIZE - 1);
		const stduint end = (load_slices[i].address + load_slices[i].length +
			PAGE_SIZE - 1) & ~_IMM(PAGE_SIZE - 1);
		bool slice_released = true;
		for (stduint address = begin; address < end; address += PAGE_SIZE) {
			void* physical;
			bool unmapped = false;
			{
				SpinlockLocal guard(&vma_lock);
				physical = paging[address];
				if (physical != (void*)~_IMM0)
					unmapped = paging.Unmap(address, PAGE_SIZE);
			}
			if (physical != (void*)~_IMM0) {
				if (!unmapped) {
					slice_released = false;
					plogerro("Failed to release ELF page at VA=%[x]", address);
					continue;
				}
				RefreshVirtualAddress(address);
				if (!Memory::ReleaseManagedPageMapping(_IMM(physical)))
					plogerro("Missing managed ELF page at physical address %[x]",
						physical);
			}
		}
		if (slice_released) {
			load_slices[i].address = 0;
			load_slices[i].length = 0;
		}
	}
	#else
	for0a(i, load_slices) {
		if (!load_slices[i].length) continue;
		mempool.deallocate((void*)load_slices[i].address,
			load_slices[i].length);
		load_slices[i].address = 0;
		load_slices[i].length = 0;
	}
	#endif
}




// ---------------------------------------------------------
// LockedAllocator Implementation
// ---------------------------------------------------------


void* LockedAllocator::allocate(stduint size, stduint alignment, stduint boundary) {
	#if MEMPOOL_BLOCK_ALIGN > 1
	if (size & (MEMPOOL_BLOCK_ALIGN - 1)) size = (size + MEMPOOL_BLOCK_ALIGN) & ~(stduint)(MEMPOOL_BLOCK_ALIGN - 1);
	#endif
	bool old_if = mempool_lock.Acquire();
	void* ret = base_allocator->allocate(size, alignment, boundary);
	mempool_lock.Release(old_if);
	return ret;
}
bool LockedAllocator::deallocate(void* ptr, stduint size) {
	if (!ptr) return false; 
	#if MEMPOOL_BLOCK_ALIGN > 1
	if (size & (MEMPOOL_BLOCK_ALIGN - 1)) size = (size + MEMPOOL_BLOCK_ALIGN) & ~(stduint)(MEMPOOL_BLOCK_ALIGN - 1);
	#endif
	bool old_if = mempool_lock.Acquire();
	bool ret = base_allocator->deallocate(ptr, size);
	mempool_lock.Release(old_if);
	return ret;
}

// ---- INTERFACE ----

#if 1
#if _MCCA == 0x8632
void* (*uni::_physical_allocate)(stduint size) = 0;
#endif

void* Memory::allocate(stduint siz, stduint alignment, stduint boundary) {
	if (!map_ready) {
		plogerro("no pg-mapping");
		return nullptr;
	}
	return mempool.allocate(siz, alignment, boundary);
}
bool Memory::deallocate(void* ptr, stduint size _Comment(zero_for_block)) {
	if (!map_ready) {
		return false;
	}
	return mempool.deallocate(ptr, size);
}

#endif

// ---- PAGING ----

#if CONFIG_ENABLE_MMU
Paging kernel_paging;
#endif

// ---- SEGMENT ----

#if (_MCCA & 0xFF00) == 0x8600

uint64 GDT_LIST[]{
	0x0000000000000000ull,//(SegNull) Ring0
	0x000F9A000000FFFFull,//(SegCo16) Ring0
	0x0020980000000000ull,//(SegCo64) Ring0
	0x00CF92000000FFFFull,//(SegData) Ring0
	0x00CF9A000000FFFFull,//(SegCo32) Ring0
	0x00CFF2000000FFFFull,//(Rg3Data) Ring3
	0x0020FA0000000000ull,//(Rg3Code) Ring3
	0x0000000000000000ull,//(SegCall) Ring3
	0x0000000000000000ull,//(SegGldt) Ring3
	#if __BITS__ == 64
	0x0000000000000000ull,
	#endif
	0x0000890000000000ull,//(SegTTS0) Ring0
	#if __BITS__ == 64
	0x0000000000000000ull,
	#endif
};// no address and limit for x64

#endif

#if (_MCCA & 0xFF00) == 0x8600
_ESYM_C void RefreshGDT();
// previous GDT may be broken, omit __asm("sgdt _buf");

void GDT_Init() {
	(*mecocoa_global).gdt_ptr = (mec_gdt*)(0x600);
	MemCopyN(mecocoa_global->gdt_ptr, GDT_LIST, sizeof(GDT_LIST));
	#if _MCCA == 0x8632
	mecocoa_global->gdt_ptr->rout.setModeCall(mglb(Handint_SYSCALL_Entry), SegCo32);
	mecocoa_global->gdt_ptr->cor3 = mecocoa_global->gdt_ptr->code;
	mecocoa_global->gdt_ptr->cor3.DPL = 3;
	#endif
	loadGDT(_IMM(mecocoa_global->gdt_ptr), mecocoa_global->gdt_len = sizeof(mec_gdt) - 1);
	#if _MCCA == 0x8632
	RefreshGDT();
	#elif _MCCA == 0x8664
	setDSAll(SegData);
	setCSSS(SegCo64, SegData);
	#endif
}
void GDT_Next() {
	(*mecocoa_global).gdt_ptr = (mec_gdt*)mglb(0x600);
	loadGDT(_IMM(mecocoa_global->gdt_ptr), mecocoa_global->gdt_len = sizeof(mec_gdt) - 1);
	#if _MCCA == 0x8632
	RefreshGDT();
	#elif _MCCA == 0x8664
	setDSAll(SegData);
	setCSSS(SegCo64, SegData);
	#endif
}

word GDT_GetNumber() {
	return (mecocoa_global->gdt_len + 1) / 8;
}

// allocate and apply, return the allocated number
word GDT_Alloc() {
	word ret = mecocoa_global->gdt_len + 1;
	loadGDT(_IMM(mecocoa_global->gdt_ptr), mecocoa_global->gdt_len += 8);
	return ret;
}
#endif

// ---- STDLIB ----

#if 1

// linear allocator
_ESYM_C void* malloc(size_t size) {
	auto ret = (mempool.allocate(size));
	if (!ret)
		printlog(ret ? _LOG_INFO : _LOG_ERROR, "malloc %u at 0x%[x]", size, ret);
	else
		MemSet(ret, 0xCC, size);// DEBUG
	return ret;
}
_ESYM_C void* calloc(size_t nmemb, size_t size) {
	// ploginfo("calloc %u %u", nmemb, size);
	void* ret = malloc(nmemb * size);
	if (ret) MemSet(ret, 0, nmemb * size);
	return ret;
}
void* operator new(size_t size) {
	return malc(size);
}
void* operator new[](size_t size) {
	// ploginfo("new[] %u", size);
	return malc(size);
}
void* operator new[](size_t size, std::align_val_t ali) {
	auto bit = intlog2_iexpo _IMM(ali);
	return mempool.allocate(size, bit);
}
void operator delete(void* p) {
	free(p);
}
void operator delete[](void* p) {
	free(p);
}
void operator delete[](void* p, std::align_val_t) noexcept {
	free(p);
}
void operator delete(void* ptr, stduint size) noexcept {
	// ploginfo("delete[](%[x], %[x])", ptr, size);
	if (!mempool.deallocate(ptr, size)) plogerro("del BAD");
	// if (!mempool.deallocate(ptr)) plogerro("del BAD");
}
void operator delete[](void* ptr, stduint size) {
	::operator delete(ptr, size);
}
#if defined(_UEFI)
void operator delete(void* ptr, stduint size, std::align_val_t) noexcept { ::operator delete(ptr, size); }
#endif
//
_ESYM_C void free(void* p) {
	if (!p) return;
	bool a = (mempool.deallocate(p));
	if (!a) printlog(a ? _LOG_INFO: _LOG_ERROR, "mfree 0x%[x]", p);
}
_ESYM_C void memf(void* ptr) { free(ptr); }

_ESYM_C void* realloc(void* ptr, size_t size) {
	if (!ptr) return malloc(size);
	if (!size) {
		free(ptr);
		return nullptr;
	}
	struct Header {
		stduint size;
		stduint prop;
	};
	Header* header = (Header*)ptr - 1;
	if (header->prop != _IMM(0xFEDC5AA5)) {
		return nullptr;
	}
	size_t old_size = header->size;
	if (old_size >= size) {
		return ptr;
	}
	void* new_ptr = malloc(size);
	if (new_ptr) {
		MemCopyN(new_ptr, ptr, old_size);
		free(ptr);
	}
	return new_ptr;
}
#endif

// ---- DMA LOW POOL (< 16MB) ----
#if (_MCCA & 0xFF00) == 0x8600
namespace {
	struct DmaLowPool {
		stduint base_phys = 0;
		uint16 page_bitmap = 0; // 1 bit per 4KB page (0: free, 1: used), 16 pages = 64KB
		Spinlock lock;

		void Init(stduint base) {
			base_phys = base;
			page_bitmap = 0;
		}

		void* Allocate(stduint bytes) {
			if (!base_phys || !bytes) return nullptr;
			stduint pages = (bytes + 0xFFF) >> 12;
			if (pages > 16) return nullptr;
			SpinlockLocal guard(&lock);
			for (stduint i = 0; i <= 16 - pages; ++i) {
				uint16 mask = uint16(((1u << pages) - 1u) << i);
				if ((page_bitmap & mask) == 0) {
					page_bitmap |= mask;
					return reinterpret_cast<void*>(base_phys + (i << 12));
				}
			}
			return nullptr;
		}

		bool Deallocate(void* ptr, stduint bytes) {
			stduint addr = reinterpret_cast<stduint>(ptr);
			if (!base_phys || addr < base_phys || addr >= base_phys + 0x10000) return false;
			stduint start_page = (addr - base_phys) >> 12;
			stduint pages = bytes ? ((bytes + 0xFFF) >> 12) : 1;
			if (start_page + pages > 16) pages = 16 - start_page;
			uint16 mask = uint16(((1u << pages) - 1u) << start_page);
			SpinlockLocal guard(&lock);
			page_bitmap &= ~mask;
			return true;
		}

		bool IsInRange(void* ptr) const {
			stduint addr = reinterpret_cast<stduint>(ptr);
			return base_phys && addr >= base_phys && addr < base_phys + 0x10000;
		}
	};

	DmaLowPool dma_low_pool;
}

void DmaLowPoolInit(stduint base_phys) {
	dma_low_pool.Init(base_phys);
}

void* DmaLowAlloc(stduint bytes) {
	return dma_low_pool.Allocate(bytes);
}

bool DmaLowFree(void* ptr, stduint bytes) {
	return dma_low_pool.Deallocate(ptr, bytes);
}

bool DmaLowIsInRange(void* ptr) {
	return dma_low_pool.IsInRange(ptr);
}
#endif


