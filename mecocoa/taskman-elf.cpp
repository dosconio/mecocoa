// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <c/format/ELF.h>
#include "../include/filesys.hpp"

struct ELFInterpreterFileCloser {
	void operator()(uni::vfs_file* file) const {
		if (file) Filesys::Close(file);
	}
};

#if CONFIG_ENABLE_MMU
namespace uni {
	extern Mutex vfs_lock;
	bool PinExecutableImage(ProcessBlock* process, vfs_file* file,
		uint64 expected_version);
	void UnpinExecutableImage(ProcessBlock* process);
}

// Keep each backend read under vfs_lock and reject an incomplete read.
class LockedInterpreterFileBlockBridge : public BlockTrait {
	FilesysTrait* filesystem;
	void* file_handler;
	stduint file_size;
public:
	LockedInterpreterFileBlockBridge(FilesysTrait* fs, void* handler,
		stduint size) : filesystem(fs), file_handler(handler), file_size(size) {
		Block_Size = 512;
		readable = true;
		writable = false;
	}
	using BlockTrait::Read;
	using BlockTrait::Write;
	bool Read(stduint block, void* destination, stduint times = 1) override {
		if (!filesystem || !file_handler || !times ||
			block > ~_IMM0 / Block_Size || times > ~_IMM0 / Block_Size)
			return false;
		const stduint offset = block * Block_Size;
		if (offset >= file_size) return false;
		const stduint requested = times * Block_Size;
		const stduint valid = minof(requested, file_size - offset);
		MutexLocal guard(&uni::vfs_lock);
		if (filesystem->readfl(file_handler, Slice { offset, valid },
			(byte*)destination) != valid) return false;
		if (valid < requested)
			MemSet((byte*)destination + valid, 0, requested - valid);
		return true;
	}
	bool Write(stduint, const void*, stduint = 1) override {
		return false;
	}
	stduint getUnits() override {
		return file_size / Block_Size + !!(file_size % Block_Size);
	}
};

struct ELFImageScope {
	ProcessBlock* process = nullptr;
	ThreadBlock* thread = nullptr;
	byte* normal_stack = nullptr;
	byte* privileged_stack = nullptr;
	~ELFImageScope() {
		if (!process) return;
		{
			MutexLocal guard(&process->mutex_mapping);
			process->ReleaseAllVmas();
			process->ReleaseLoadslices();
		}
		uni::UnpinExecutableImage(process);
		if (thread) {
			thread->~ThreadBlock();
			mempool.deallocate(thread, sizeof(ThreadBlock));
			process->main_thread = nullptr;
			process->thread_list_head = nullptr;
		}
		if (privileged_stack && privileged_stack != normal_stack)
			mempool.deallocate(privileged_stack, HIGHER_STACK_SIZE);
		if (normal_stack)
			mempool.deallocate(normal_stack, HIGHER_STACK_SIZE);
		ProcessBlock::Release(process);
	}
	void Keep() { process = nullptr; }
};

enum class ELFLoadPageKind : byte {
	File,
	Private,
	Zero,
};

struct ELFLoadPagePlan {
	stduint address = 0;
	stduint flags = 0;
	stduint file_offset = 0;
	ELFLoadPageKind kind = ELFLoadPageKind::Zero;
};

static bool FullFileLoadPage(const ELF_PHT_t* headers, stduint count,
	stduint segment, stduint page, stduint load_bias) {
	const auto& ph = headers[segment];
	const stduint begin = ph.p_vaddr + load_bias;
	if (page < begin || ph.p_filesz < PAGE_SIZE ||
		page - begin > ph.p_filesz - PAGE_SIZE ||
		((ph.p_offset + page - begin) & (PAGE_SIZE - 1))) return false;
	for (stduint i = 0; i < count; i++) {
		if (i == segment || headers[i].p_type != PT_LOAD ||
			!headers[i].p_memsz) continue;
		const stduint other_begin = headers[i].p_vaddr + load_bias;
		const stduint other_end = other_begin + headers[i].p_memsz;
		if (page < other_end && other_begin < page + PAGE_SIZE) return false;
	}
	return true;
}

static bool NextELFLoadPage(const ELF_PHT_t* headers, stduint count,
	stduint load_bias, stduint after, bool first, stduint& result) {
	result = ~_IMM0;
	for (stduint i = 0; i < count; i++) {
		const auto& ph = headers[i];
		if (ph.p_type != PT_LOAD || !ph.p_memsz) continue;
		const stduint begin = (ph.p_vaddr + load_bias) &
			~_IMM(PAGE_SIZE - 1);
		const stduint end = (ph.p_vaddr + load_bias + ph.p_memsz +
			PAGE_SIZE - 1) & ~_IMM(PAGE_SIZE - 1);
		stduint candidate = begin;
		if (!first && candidate <= after) candidate = after + PAGE_SIZE;
		if (candidate < end && candidate < result) result = candidate;
	}
	return result != ~_IMM0;
}

static bool DescribeELFLoadPage(const ELF_PHT_t* headers, stduint count,
	stduint load_bias, stduint page, ELFLoadPagePlan& result) {
	bool covered = false;
	bool has_file_bytes = false;
	bool executable = false;
	bool writable = false;
	stduint full_file_segment = ~_IMM0;
	for (stduint i = 0; i < count; i++) {
		const auto& ph = headers[i];
		if (ph.p_type != PT_LOAD || !ph.p_memsz) continue;
		const stduint begin = ph.p_vaddr + load_bias;
		const stduint end = begin + ph.p_memsz;
		if (page >= end || begin >= page + PAGE_SIZE) continue;
		covered = true;
		// One hardware page may contain bytes from adjacent PT_LOAD segments.
		executable |= !!(ph.p_flags & PF_X);
		writable |= !!(ph.p_flags & PF_W);
		const stduint file_end = begin + ph.p_filesz;
		if (page < file_end && begin < page + PAGE_SIZE)
			has_file_bytes = true;
		if (FullFileLoadPage(headers, count, i, page, load_bias))
			full_file_segment = i;
	}
	if (!covered) return false;
	result.address = page;
	result.flags = PGPROP_present | PGPROP_user_access;
	if (writable) result.flags |= PGPROP_writable;
	#if _MCCA != 0x8632
	if (!executable) result.flags |= PGPROP_nonexecutable;
	#endif
	if (full_file_segment != ~_IMM0) {
		const auto& ph = headers[full_file_segment];
		result.kind = ELFLoadPageKind::File;
		result.file_offset = ph.p_offset + page -
			(ph.p_vaddr + load_bias);
	}
	else result.kind = has_file_bytes ?
		ELFLoadPageKind::Private : ELFLoadPageKind::Zero;
	return true;
}

static bool SameELFLoadVma(const ELFLoadPagePlan& left,
	const ELFLoadPagePlan& right) {
	if (left.flags != right.flags) return false;
	const bool left_file = left.kind == ELFLoadPageKind::File;
	const bool right_file = right.kind == ELFLoadPageKind::File;
	if (left_file != right_file) return false;
	return !left_file || right.file_offset == left.file_offset +
		right.address - left.address;
}

static bool InsertELFLoadVma(ProcessBlock* pb, uni::vfs_file* file,
	const ELFLoadPagePlan& plan, stduint end) {
	VirtualMemoryArea vma(plan.address, end, plan.flags);
	if (plan.kind == ELFLoadPageKind::File) {
		vma.vm_type = VirtualMemoryAreaType::File;
		vma.vfile = new uni::vfs_file(*file);
		if (!vma.vfile) return false;
		vma.file_offset = plan.file_offset;
	}
	bool inserted;
	{
		SpinlockLocal vma_guard(&pb->vma_lock);
		inserted = pb->InsertVmaSorted(vma);
	}
	if (!inserted && vma.vfile) uni::Filesys::Close(vma.vfile);
	return inserted;
}

static bool PopulateELFPrivatePage(ProcessBlock* pb, BlockTrait* source,
	const ELF_PHT_t* headers, stduint count, stduint load_bias,
	stduint page, byte* buffer) {
	void* physical = pb->EnsureVmaPage(page, VirtualMemoryAccess::Read);
	if (!physical) return false;
	MemSet(physical, 0, PAGE_SIZE);
	for (stduint i = 0; i < count; i++) {
		const auto& ph = headers[i];
		if (ph.p_type != PT_LOAD || !ph.p_memsz) continue;
		const stduint begin = ph.p_vaddr + load_bias;
		const stduint end = begin + ph.p_memsz;
		if (page >= end || begin >= page + PAGE_SIZE) continue;
		const stduint file_end = begin + ph.p_filesz;
		const stduint copy_begin = maxof(page, begin);
		const stduint copy_end = minof(page + PAGE_SIZE, file_end);
		if (copy_begin < copy_end && source->Read(ph.p_offset +
			copy_begin - begin, (byte*)physical + copy_begin - page,
			copy_end - copy_begin, buffer) != copy_end - copy_begin)
			return false;
		const stduint zero_begin = maxof(page, file_end);
		const stduint zero_end = minof(page + PAGE_SIZE, end);
		if (zero_begin < zero_end)
			MemSet((byte*)physical + zero_begin - page, 0,
				zero_end - zero_begin);
	}
	return true;
}

static bool PlanELFFileLoads(BlockTrait* source, const ELF_Header_t& header,
	stduint file_size, stduint load_bias, byte* buffer,
	ELF_PHT_t* headers) {
	if ((header.e_type != ET_EXEC && header.e_type != ET_DYN) ||
		!header.e_phnum || header.e_phentsize != sizeof(ELF_PHT_t) ||
		header.e_phoff > file_size ||
		stduint(header.e_phnum) >
			(file_size - header.e_phoff) / sizeof(ELF_PHT_t)) return false;
	bool has_load = false;
	bool entry_in_segment = false;
	#if _MCCA == 0x8664
	const stduint user_end = 0x0000800000000000ull;
	#elif _MCCA == 0x1064
	const stduint user_end = 0x4000000000ull;
	#else
	const stduint user_end = 0x80000000;
	#endif
	for (stduint i = 0; i < header.e_phnum; i++) {
		if (source->Read(header.e_phoff + i * sizeof(ELF_PHT_t),
			&headers[i], sizeof(ELF_PHT_t), buffer) != sizeof(ELF_PHT_t))
			return false;
		const auto& ph = headers[i];
		if (ph.p_type != PT_LOAD || !ph.p_memsz) continue;
		has_load = true;
		if (ph.p_filesz > ph.p_memsz || ph.p_offset > file_size ||
			ph.p_filesz > file_size - ph.p_offset ||
			((ph.p_offset ^ ph.p_vaddr) & (PAGE_SIZE - 1)) ||
			(ph.p_align > 1 &&
				((ph.p_align & (ph.p_align - 1)) ||
				((ph.p_vaddr - ph.p_offset) & (ph.p_align - 1)))) ||
			ph.p_vaddr > ~_IMM0 - load_bias) return false;
		const stduint begin = ph.p_vaddr + load_bias;
		if (begin < 0x10000 || begin >= user_end ||
			ph.p_memsz > user_end - begin ||
			ph.p_memsz > ~_IMM0 - (PAGE_SIZE - 1) - begin)
			return false;
		if (begin < 0x1000 + HIGHER_STACK_SIZE &&
			begin + ph.p_memsz > 0x1000) return false;
		const stduint entry = header.e_entry + load_bias;
		if ((ph.p_flags & PF_X) && entry >= begin &&
			entry - begin < ph.p_memsz) entry_in_segment = true;
	}
	return has_load && header.e_entry <= ~_IMM0 - load_bias &&
		entry_in_segment;
}

// Keep the main image's segment metadata; interpreter pages are owned by VMAs.
static bool RecordMainELFLoads(ProcessBlock* pb, const ELF_PHT_t* headers,
	stduint count, stduint load_bias, stduint& load_slice_p,
	stduint& max_seg_end) {
	stduint load_count = 0;
	for (stduint i = 0; i < count; i++) {
		const auto& ph = headers[i];
		if (ph.p_type != PT_LOAD || !ph.p_memsz) continue;
		load_count++;
		if (load_count > numsof(pb->load_slices)) return false;
		const stduint begin = ph.p_vaddr + load_bias;
		const stduint end = begin + ph.p_memsz;
		pb->load_slices[load_slice_p].address = begin;
		pb->load_slices[load_slice_p].length = ph.p_memsz;
		load_slice_p++;
		max_seg_end = maxof(max_seg_end, end);
	}
	return true;
}

static bool MapELFFileLoads(ProcessBlock* pb, BlockTrait* source,
	uni::vfs_file* file, const ELF_PHT_t* headers, stduint count,
	stduint load_bias, byte* buffer) {
	// VMA runs are created only after every program header has been validated.
	stduint page;
	bool first = true;
	stduint after = 0;
	while (NextELFLoadPage(headers, count, load_bias, after, first, page)) {
		first = false;
		ELFLoadPagePlan first_plan;
		if (!DescribeELFLoadPage(headers, count, load_bias,
			page, first_plan)) return false;
		stduint run_end = page + PAGE_SIZE;
		ELFLoadPagePlan next_plan;
		while (DescribeELFLoadPage(headers, count, load_bias,
			run_end, next_plan) && SameELFLoadVma(first_plan, next_plan))
			run_end += PAGE_SIZE;
		if (!InsertELFLoadVma(pb, file, first_plan, run_end)) return false;
		if (first_plan.kind != ELFLoadPageKind::File) {
			for (stduint current = page; current < run_end;
				current += PAGE_SIZE) {
				ELFLoadPagePlan current_plan;
				if (!DescribeELFLoadPage(headers, count, load_bias,
					current, current_plan)) return false;
				if (current_plan.kind == ELFLoadPageKind::Private &&
					!PopulateELFPrivatePage(pb, source, headers, count,
						load_bias, current, buffer)) return false;
			}
		}
		after = run_end - PAGE_SIZE;
	}
	return true;
}
#endif
#if defined(_ARCH_ARM_ProfileM)
#include <cpp/Device/Cache>
#endif
#if !CONFIG_ENABLE_MMU
#define MemCopyP(a,b,c,d,e) _IMM((byte*)MemCopyN(a,c,e)-(byte*)a)
#define StrCopyP(a,b,c,d,e) _IMM((byte*)StrCopyN(a,c,e)-(byte*)a)
#endif

#if CONFIG_ENABLE_MMU
static void RollbackELFPages(Paging& paging, const stduint* addresses,
	stduint count) {
	while (count) {
		const stduint address = addresses[--count];
		void* physical = paging[address];
		if (physical == (void*)~_IMM0) continue;
		if (!paging.Unmap(address, PAGE_SIZE)) {
			plogerro("Failed to roll back ELF page at VA=%[x]", address);
			continue;
		}
		if (!Memory::ReleaseManagedPageMapping(_IMM(physical)))
			plogerro("Missing managed ELF page at physical address %[x]",
				physical);
	}
}
#endif

bool Taskman::CreateELF_Carry(char* vaddr, stduint mem_length, BlockTrait* source, stduint file_offset, stduint file_size, Paging& pg, byte* buffer, bool executable, bool writable, bool user) {
	// if page !exist, map it; write it.
	stduint compensation = _IMM(vaddr) & 0xFFF;
	stduint v_start1 = _IMM(vaddr) & ~_IMM(0xFFF);
	if (_IMM(vaddr) < 0x10000) {
		plogerro("%s: vaddr is too low", __FUNCIDEN__);
	}
	#if _MCCA == 0x8632 || _MCCA == 0x1032
	if (_IMM(vaddr) >= 0x80000000) {
		plogerro("Taskman::CreateELF_Carry");
	}
	#elif _MCCA == 0x8664
	if (_IMM(vaddr) >= 0xFFFFC0000000ull) {
		plogerro("Taskman::CreateELF_Carry");
	}
	#elif _MCCA == 0x1064
    if (_IMM(vaddr) >= 0x4000000000ull) { 
        plogerro("Taskman::CreateELF_Carry: vaddr out of RV64 Sv39 user space");
    }
	#endif

	#if CONFIG_ENABLE_MMU
	// ploginfo("Taskman::CreateELF_Carry(%[x], %[x], %[x])  pg(%[x])", vaddr, length, phy_src, pg);
	const stduint page_count = (mem_length + compensation + 0xFFF) / 0x1000;
	auto allocated_pages = uni::make_unique<stduint[]>(page_count);
	if (page_count && !allocated_pages) return false;
	stduint allocated_page_count = 0;
	stduint bytes_read = 0; 
	for0(i, page_count) {
		auto page_entry = pg.getEntry(_IMM(v_start1));
		stduint phy;
		
		if (_IMM(page_entry) == ~_IMM0 || !page_entry->isPresent()) {
			phy = _IMM(mempool.allocate(0x1000, 12));
			if (!phy || phy == ~_IMM0) {
				RollbackELFPages(pg, allocated_pages.get(), allocated_page_count);
				return false;
			}
			MemSet((void*)phy, 0, 0x1000);
			if (!Memory::AdoptManagedPage(phy, ManagedPageSource::ELF)) {
				RollbackELFPages(pg, allocated_pages.get(), allocated_page_count);
				return false;
			}
			stduint pgprop = PGPROP_present;
			if (writable) pgprop |= PGPROP_writable;
			if (user) pgprop |= PGPROP_user_access;
			#if _MCCA != 0x8632
			if (!executable) pgprop |= PGPROP_nonexecutable;
			#endif
			if (!pg.Map(v_start1, phy, 0x1000, PAGESIZE_4KB, pgprop)) {
				Memory::ReleaseManagedPageMapping(phy);
				RollbackELFPages(pg, allocated_pages.get(), allocated_page_count);
				return false;
			}
			allocated_pages[allocated_page_count++] = v_start1;
			page_entry = pg.getEntry(_IMM(v_start1));
			if (_IMM(page_entry) == ~_IMM0 || !page_entry->isPresent()) {
				plogerro("Mapping failed %[x] -> %[x]", v_start1, phy);
				RollbackELFPages(pg, allocated_pages.get(), allocated_page_count);
				return false;
			}
		}
		else phy = page_entry->getAddress(0);

		stduint chunk_size = 0x1000 - compensation;
		if (chunk_size > mem_length - bytes_read) {
			chunk_size = mem_length - bytes_read; 
		}

		stduint phy_dest = phy + compensation;
		void* kdest = (void*)(phy_dest);

		// xDATA or BSS 
		if (bytes_read >= file_size) {
			MemSet(kdest, 0, chunk_size);
		} 
		else {
			stduint copy_size = chunk_size;
			if (bytes_read + chunk_size > file_size) {
				copy_size = file_size - bytes_read;
				MemSet((void*)(phy_dest + copy_size), 0, chunk_size - copy_size);
			}
			auto ret = source->Read(file_offset + bytes_read, kdest, copy_size, buffer);
			if (ret != copy_size) {
				plogwarn("%s %u: Read failed, %u != %u", __FUNCIDEN__, __LINE__, ret, copy_size);
				RollbackELFPages(pg, allocated_pages.get(), allocated_page_count);
				return false;
			}
		}

		bytes_read += chunk_size;
		v_start1 += 0x1000;
		compensation = 0; 
	}
	return true;
	#endif
	return false;
}

#if CONFIG_ENABLE_MMU
static bool SplitELFVma(ProcessBlock* pb, stduint address) {
	VirtualMemoryArea snapshot;
	bool found = false;
	{
		SpinlockLocal guard(&pb->vma_lock);
		for (stduint i = 0; i < pb->vmas.Count(); i++) {
			const auto& vma = pb->vmas[i];
			if (address <= vma.vm_start || address >= vma.vm_end) continue;
			snapshot = vma;
			found = true;
			break;
		}
	}
	if (!found) return true;
	auto* right_file = snapshot.vfile ?
		new uni::vfs_file(*snapshot.vfile) : nullptr;
	if (snapshot.vfile && !right_file) return false;
	VirtualMemoryArea right = snapshot;
	right.vm_start = address;
	right.file_offset += address - snapshot.vm_start;
	right.vfile = right_file;
	bool inserted = false;
	{
		SpinlockLocal guard(&pb->vma_lock);
		for (stduint i = 0; i < pb->vmas.Count(); i++) {
			auto& vma = pb->vmas[i];
			if (!(vma == snapshot)) continue;
			vma.vm_end = address;
			inserted = pb->InsertVmaSorted(right);
			if (!inserted) vma.vm_end = snapshot.vm_end;
			break;
		}
	}
	if (!inserted && right_file) uni::Filesys::Close(right_file);
	return inserted;
}

static bool ProtectELFVmaRange(ProcessBlock* pb, stduint begin,
	stduint end) {
	if (begin >= end || ((begin | end) & (PAGE_SIZE - 1))) return false;
	if (!SplitELFVma(pb, begin) || !SplitELFVma(pb, end)) return false;
	bool complete = true;
	{
		SpinlockLocal guard(&pb->vma_lock);
		for (stduint page = begin; page < end; page += PAGE_SIZE) {
			bool covered = false;
			for (stduint i = 0; i < pb->vmas.Count(); i++) {
				auto& vma = pb->vmas[i];
				if (page < vma.vm_start || page >= vma.vm_end) continue;
				covered = true;
				vma.vm_flags &= ~_IMM(PGPROP_writable);
				pageint index = page;
				PageEntry* entry = pb->paging.getEntry(index);
				stduint properties;
				if (entry && _IMM(entry) != ~_IMM0 &&
					entry->DecodeLeafProperties(index.crt_level, properties) &&
					(properties & PGPROP_writable)) {
					PageEntry replacement;
					if (!replacement.SetupAsLeaf(_IMM(pb->paging[page]),
						index.crt_level, properties & ~_IMM(PGPROP_writable)))
						complete = false;
					else entry->raw = replacement.raw;
				}
				break;
			}
			if (!covered) complete = false;
		}
	}
	if (!complete) return false;
	for (stduint page = begin; page < end; page += PAGE_SIZE)
		Memory::InvalidateProcessPage(pb, page);
	return true;
}

static bool ELFFileRange(const ELF_PHT_t* headers, stduint count,
	stduint address, stduint length, stduint& file_offset) {
	if (!length || length > ~_IMM0 - address) return false;
	for (stduint i = 0; i < count; i++) {
		const auto& ph = headers[i];
		if (ph.p_type != PT_LOAD || address < ph.p_vaddr) continue;
		const stduint offset = address - ph.p_vaddr;
		if (offset <= ph.p_filesz && length <= ph.p_filesz - offset) {
			file_offset = ph.p_offset + offset;
			return true;
		}
	}
	return false;
}

static bool ELFRelocationTarget(const ELF_PHT_t* headers, stduint count,
	stduint address, stduint length) {
	if (!length || length > ~_IMM0 - address ||
		(address & (PAGE_SIZE - 1)) > PAGE_SIZE - length) return false;
	for (stduint i = 0; i < count; i++) {
		const auto& ph = headers[i];
		if (ph.p_type != PT_LOAD || !(ph.p_flags & PF_W) ||
			address < ph.p_vaddr) continue;
		const stduint offset = address - ph.p_vaddr;
		if (offset <= ph.p_memsz && length <= ph.p_memsz - offset)
			return true;
	}
	return false;
}

static bool ApplyELFRelro(ProcessBlock* pb, const ELF_PHT_t* headers,
	stduint count, stduint load_bias, bool has_interpreter) {
	if (has_interpreter) return true;
	bool has_vmas;
	{
		SpinlockLocal guard(&pb->vma_lock);
		has_vmas = !!pb->vmas.Count();
	}
	// Legacy eager images do not have VMA metadata to retain the protection.
	if (!has_vmas) return true;
	for (stduint i = 0; i < count; i++) {
		const auto& ph = headers[i];
		if (ph.p_type != PT_GNU_RELRO || !ph.p_memsz) continue;
		if (ph.p_vaddr > ~_IMM0 - load_bias ||
			ph.p_memsz > ~_IMM0 - (ph.p_vaddr + load_bias) -
				(PAGE_SIZE - 1)) return false;
		const stduint begin = (ph.p_vaddr + load_bias) &
			~_IMM(PAGE_SIZE - 1);
		const stduint end = (ph.p_vaddr + load_bias + ph.p_memsz +
			PAGE_SIZE - 1) & ~_IMM(PAGE_SIZE - 1);
		if (!ProtectELFVmaRange(pb, begin, end)) return false;
	}
	return true;
}

bool _Taskman_Relocate_PIE(BlockTrait* source, const ELF_Header_t& header,
	stduint load_bias, ProcessBlock* pb, bool mapping_lock_held,
	byte* block_buffer) {
	auto headers = uni::make_unique<ELF_PHT_t[]>(header.e_phnum);
	if (!headers) return false;
	bool has_interpreter = false;
	for (stduint i = 0; i < header.e_phnum; i++) {
		if (source->Read(header.e_phoff + i * header.e_phentsize,
			&headers[i], sizeof(ELF_PHT_t), block_buffer) !=
			sizeof(ELF_PHT_t)) return false;
		has_interpreter |= headers[i].p_type == PT_INTERP;
	}
	if (!load_bias) return ApplyELFRelro(pb, headers.get(),
		header.e_phnum, load_bias, has_interpreter);
	#if _MCCA != 0x8632 && _MCCA != 0x8664
	plogerro("%s: ET_DYN (PIE) is not relocatable on this architecture (_MCCA=%[x])", __FUNCIDEN__, (stduint)_MCCA);
	return false;
	#else
	for (stduint i = 0; i < header.e_phnum; i++) {
		const auto& ph = headers[i];
		if (ph.p_type != PT_DYNAMIC || !ph.p_filesz) continue;
		#if _MCCA == 0x8632
		if (ph.p_filesz % sizeof(Elf32_Dyn)) return false;
		stduint rel_vaddr = 0, rel_size = 0;
		stduint rel_entry = sizeof(Elf32_Rel);
		for (stduint j = 0; j < ph.p_filesz / sizeof(Elf32_Dyn); j++) {
			Elf32_Dyn dyn;
			if (source->Read(ph.p_offset + j * sizeof(Elf32_Dyn),
				&dyn, sizeof(dyn), block_buffer) != sizeof(dyn)) return false;
			if (dyn.d_tag == DT_NULL) break;
			if (dyn.d_tag == DT_REL) rel_vaddr = dyn.d_un.d_ptr;
			else if (dyn.d_tag == DT_RELSZ) rel_size = dyn.d_un.d_val;
			else if (dyn.d_tag == DT_RELENT) rel_entry = dyn.d_un.d_val;
		}
		if (!!rel_vaddr != !!rel_size) return false;
		if (rel_size) {
			if (rel_entry != sizeof(Elf32_Rel) ||
				rel_size % sizeof(Elf32_Rel)) return false;
			stduint rel_file_offset;
			if (!ELFFileRange(headers.get(), header.e_phnum, rel_vaddr,
				rel_size, rel_file_offset)) return false;
			for (stduint offset = 0; offset < rel_size;
				offset += sizeof(Elf32_Rel)) {
				Elf32_Rel rel;
				if (source->Read(rel_file_offset + offset, &rel,
					sizeof(rel), block_buffer) != sizeof(rel)) return false;
				if (ELF32_R_TYPE(rel.r_info) != R_386_RELATIVE) {
					if (!has_interpreter) return false;
					continue;
				}
				if (!ELFRelocationTarget(headers.get(), header.e_phnum,
					rel.r_offset, sizeof(uint32)) ||
					rel.r_offset > ~_IMM0 - load_bias) return false;
				const stduint target = rel.r_offset + load_bias;
				if (!PrepareUserBuffer(pb, target, sizeof(uint32), true,
					mapping_lock_held)) return false;
				void* physical = pb->paging[target & ~_IMM(PAGE_SIZE - 1)];
				if (physical == (void*)~_IMM0) return false;
				auto* value = (uint32*)((byte*)physical +
					(target & (PAGE_SIZE - 1)));
				*value += (uint32)load_bias;
			}
		}
		#elif _MCCA == 0x8664
		if (ph.p_filesz % sizeof(Elf64_Dyn)) return false;
		stduint rela_vaddr = 0, rela_size = 0;
		stduint rela_entry = sizeof(Elf64_Rela);
		for (stduint j = 0; j < ph.p_filesz / sizeof(Elf64_Dyn); j++) {
			Elf64_Dyn dyn;
			if (source->Read(ph.p_offset + j * sizeof(Elf64_Dyn),
				&dyn, sizeof(dyn), block_buffer) != sizeof(dyn)) return false;
			if (dyn.d_tag == DT_NULL) break;
			if (dyn.d_tag == DT_RELA) rela_vaddr = dyn.d_un.d_ptr;
			else if (dyn.d_tag == DT_RELASZ) rela_size = dyn.d_un.d_val;
			else if (dyn.d_tag == DT_RELAENT) rela_entry = dyn.d_un.d_val;
		}
		if (!!rela_vaddr != !!rela_size) return false;
		if (rela_size) {
			if (rela_entry != sizeof(Elf64_Rela) ||
				rela_size % sizeof(Elf64_Rela)) return false;
			stduint rela_file_offset;
			if (!ELFFileRange(headers.get(), header.e_phnum, rela_vaddr,
				rela_size, rela_file_offset)) return false;
			for (stduint offset = 0; offset < rela_size;
				offset += sizeof(Elf64_Rela)) {
				Elf64_Rela rela;
				if (source->Read(rela_file_offset + offset, &rela,
					sizeof(rela), block_buffer) != sizeof(rela)) return false;
				if (ELF64_R_TYPE(rela.r_info) != R_X86_64_RELATIVE) {
					if (!has_interpreter &&
						ELF64_R_TYPE(rela.r_info) != R_X86_64_NONE) return false;
					continue;
				}
				if (!ELFRelocationTarget(headers.get(), header.e_phnum,
					rela.r_offset, sizeof(uint64)) ||
					rela.r_offset > ~_IMM0 - load_bias) return false;
				const stduint target = rela.r_offset + load_bias;
				if (!PrepareUserBuffer(pb, target, sizeof(uint64), true,
					mapping_lock_held)) return false;
				void* physical = pb->paging[target & ~_IMM(PAGE_SIZE - 1)];
				if (physical == (void*)~_IMM0) return false;
				auto* value = (uint64*)((byte*)physical +
					(target & (PAGE_SIZE - 1)));
				*value = (uint64)(rela.r_addend + load_bias);
			}
		}
		#endif
		break;
	}
	return ApplyELFRelro(pb, headers.get(), header.e_phnum,
		load_bias, has_interpreter);
	#endif
}
#else
bool _Taskman_Relocate_PIE(BlockTrait*, const ELF_Header_t&,
	stduint load_bias, ProcessBlock*, bool, byte*) {
	return !load_bias;
}
#endif

#if !CONFIG_ENABLE_MMU

// NoMMU: the image is a flat copy, so load_bias is the physical address of vaddr 0 and a relocation is an in-place add
bool _Taskman_Relocate_PIE_Flat(BlockTrait* source, const ELF_Header_t& header, stduint load_bias, byte* block_buffer) {
	if (!load_bias) return true;
	for0(i, header.e_phnum) {
		struct ELF_PHT_t ph;
		source->Read(header.e_phoff + i * header.e_phentsize, &ph, sizeof(ph), block_buffer);
		if (ph.p_type != PT_DYNAMIC || !ph.p_filesz) continue;
		stduint rel_vaddr = 0, rel_sz = 0, rel_ent = sizeof(Elf32_Rel), rela_vaddr = 0;
		const stduint num_dyn = ph.p_filesz / sizeof(Elf32_Dyn);
		for0(j, num_dyn) {
			Elf32_Dyn dyn;
			source->Read(ph.p_offset + j * sizeof(Elf32_Dyn), &dyn, sizeof(dyn), block_buffer);
			if (dyn.d_tag == DT_NULL) break;
			if (dyn.d_tag == DT_REL) rel_vaddr = dyn.d_un.d_ptr;
			else if (dyn.d_tag == DT_RELSZ) rel_sz = dyn.d_un.d_val;
			else if (dyn.d_tag == DT_RELENT) rel_ent = dyn.d_un.d_val;
			else if (dyn.d_tag == DT_RELA) rela_vaddr = dyn.d_un.d_ptr;
		}
		if (!rel_vaddr) {
			// 32-bit ARM relocates with REL: a RELA table means the wrong target was built
			if (rela_vaddr) {
				plogerro("%s: DT_RELA is not supported, expected DT_REL", __FUNCIDEN__);
				return false;
			}
			continue;
		}
		if (!rel_sz || !rel_ent) continue;
		stduint rel_file_off = 0;
		bool rel_found = false;
		for0(k, header.e_phnum) {
			struct ELF_PHT_t lph;
			source->Read(header.e_phoff + k * header.e_phentsize, &lph, sizeof(lph), block_buffer);
			if (lph.p_type == PT_LOAD && rel_vaddr >= lph.p_vaddr && rel_vaddr < lph.p_vaddr + lph.p_filesz) {
				rel_file_off = lph.p_offset + (rel_vaddr - lph.p_vaddr);
				rel_found = true;
				break;
			}
		}
		if (!rel_found) {
			plogerro("%s: DT_REL (vaddr=%[x]) is not covered by any PT_LOAD", __FUNCIDEN__, rel_vaddr);
			return false;
		}
		const stduint rel_count = rel_sz / rel_ent;
		stduint image_span = 0;
		for0(k, header.e_phnum) {
			struct ELF_PHT_t lph;
			source->Read(header.e_phoff + k * header.e_phentsize, &lph, sizeof(lph), block_buffer);
			if (lph.p_type == PT_LOAD && lph.p_vaddr + lph.p_memsz > image_span) image_span = lph.p_vaddr + lph.p_memsz;
		}
		for0(r, rel_count) {
			Elf32_Rel rel;
			source->Read(rel_file_off + r * sizeof(Elf32_Rel), &rel, sizeof(rel), block_buffer);
			const stduint rel_type = ELF32_R_TYPE(rel.r_info);
			if (!rel_type) continue;
			if (rel.r_offset + 4 > image_span) {
				plogerro("%s: reloc %u at %[x] is outside the %u byte image", __FUNCIDEN__, rel_type, rel.r_offset, image_span);
				return false;
			}
			#ifdef ELF_RELOC_RELATIVE
			if (rel_type == ELF_RELOC_RELATIVE || rel_type == ELF_RELOC_ABS32) {
				*(uint32*)(load_bias + rel.r_offset) += (uint32)load_bias;
				// ploginfo("[ELF] reloc %[x] += %[x]", rel.r_offset, load_bias);
				continue;
			}
			#endif
			plogerro("%s: unsupported reloc %u at %[x]", __FUNCIDEN__, rel_type, rel.r_offset);
			return false;
		}
	}
	return true;
}

#endif

// Resolve PT_INTERP to an opened file, so its inode survives namespace changes.
static bool _Taskman_Resolve_Interp(BlockTrait* source, const ELF_Header_t& header,
	stduint file_size, byte* block_buffer, vfs_dentry* cwd,
	uni::vfs_file** out_interp) {
	*out_interp = nullptr;
	if (!header.e_phnum || header.e_phentsize != sizeof(ELF_PHT_t) ||
		header.e_phoff > file_size ||
		stduint(header.e_phnum) >
			(file_size - header.e_phoff) / sizeof(ELF_PHT_t)) return false;
	String interp_buf(String::Charset::Memory, 256);
	bool found = false;
	for (stduint i = 0; i < header.e_phnum; i++) {
		ELF_PHT_t ph;
		if (source->Read(header.e_phoff + i * sizeof(ELF_PHT_t),
			&ph, sizeof(ph), block_buffer) != sizeof(ph)) return false;
		if (ph.p_type != PT_INTERP) continue;
		if (found || ph.p_filesz < 2 || ph.p_filesz > 256 ||
			ph.p_offset > file_size || ph.p_filesz > file_size - ph.p_offset ||
			source->Read(ph.p_offset, interp_buf.reflect(),
				ph.p_filesz, block_buffer) != ph.p_filesz) return false;
		const char* path = interp_buf.reference();
		if (path[0] != '/' || path[ph.p_filesz - 1]) return false;
		for (stduint j = 0; j + 1 < ph.p_filesz; j++)
			if (!path[j]) return false;
		found = true;
	}
	if (!found) return true;
	String real_interp_path;
	const auto& runtime_root = Filesys::GetSystemRuntimeRootPath();
	if (runtime_root.getByteCount() > 0)
		real_interp_path = String::newFormat("%s%s",
			runtime_root.reference(), interp_buf.reference());
	else real_interp_path = interp_buf;
	ploginfo("%s: ELF interpreter mapped to: %s", __FUNCIDEN__,
		real_interp_path.reference());
	uni::vfs_file* file = nullptr;
	if (Filesys::Open(real_interp_path.reference(), O_RDONLY, &file, cwd) ||
		!file) {
		plogerro("%s: Interpreter not found at %s", __FUNCIDEN__,
			real_interp_path.reference());
		return false;
	}
	if (!file->f_inode || (file->f_inode->i_mode & I_TYPE_MASK) != I_REGULAR ||
		!file->f_inode->i_sb || !file->f_inode->i_sb->fs) {
		Filesys::Close(file);
		return false;
	}
	plogtrac("%s: Interpreter FOUND and ready to load.", __FUNCIDEN__);
	*out_interp = file;
	return true;
}

#if CONFIG_ENABLE_MMU
// The image is not running yet; inspect only VMA metadata under the spinlock.
static bool InterpreterLoadRangeAvailable(ProcessBlock* pb,
	const ELF_PHT_t* headers, stduint count) {
	{
		SpinlockLocal guard(&pb->vma_lock);
		for (stduint i = 0; i < count; i++) {
			const auto& ph = headers[i];
			if (ph.p_type != PT_LOAD || !ph.p_memsz) continue;
			const stduint begin = ph.p_vaddr & ~_IMM(PAGE_SIZE - 1);
			const stduint end = (ph.p_vaddr + ph.p_memsz + PAGE_SIZE - 1) &
				~_IMM(PAGE_SIZE - 1);
			for (stduint j = 0; j < pb->vmas.Count(); j++)
				if (begin < pb->vmas[j].vm_end && pb->vmas[j].vm_start < end)
					return false;
			for0a(j, pb->load_slices) {
				const auto& slice = pb->load_slices[j];
				if (!slice.length) continue;
				if (slice.length > ~_IMM0 - slice.address ||
					(begin < slice.address + slice.length && slice.address < end))
					return false;
			}
		}
	}
	for (stduint i = 0; i < count; i++) {
		const auto& ph = headers[i];
		if (ph.p_type != PT_LOAD || !ph.p_memsz) continue;
		const stduint begin = ph.p_vaddr & ~_IMM(PAGE_SIZE - 1);
		const stduint end = (ph.p_vaddr + ph.p_memsz + PAGE_SIZE - 1) &
			~_IMM(PAGE_SIZE - 1);
		for (stduint page = begin; page < end; page += PAGE_SIZE)
			if (pb->paging[page] != (void*)~_IMM0) return false;
	}
	return true;
}
#endif

// Validate the interpreter before creating its file-backed VMAs.
static bool _Taskman_Load_Interp(uni::vfs_file* file, ProcessBlock* pb,
	stduint expected_machine, byte* block_buffer, stduint* out_entry) {
	#if CONFIG_ENABLE_MMU
	if (!file || !file->f_inode) return false;
	stduint file_size;
	uint64 file_version;
	FilesysTrait* filesystem;
	void* file_handler;
	{
		MutexLocal guard(&uni::vfs_lock);
		file_size = file->f_inode->i_size;
		file_version = file->f_inode->content_version;
		filesystem = file->f_inode->i_sb->fs;
		file_handler = file->f_inode->internal_handler;
	}
	// Pin outside vfs_lock before reading the interpreter image.
	if (!uni::PinExecutableImage(pb, file, file_version)) return false;
	LockedInterpreterFileBlockBridge interp_device(filesystem,
		file_handler, file_size);
	ELF_Header_t interp_header;
	if (interp_device.Read(0, &interp_header, sizeof(interp_header),
		block_buffer) != sizeof(interp_header) ||
		MemCompare((const char*)interp_header.e_ident, "\x7F""ELF", 4) ||
		interp_header.e_ident[EI_CLASS] !=
			(sizeof(stduint) == 4 ? ELFCLASS32 : ELFCLASS64) ||
		interp_header.e_ident[EI_DATA] != ELFDATA2LSB ||
		interp_header.e_ident[EI_VERSION] != EV_CURRENT ||
		interp_header.e_version != EV_CURRENT ||
		interp_header.e_machine != expected_machine ||
		interp_header.e_type != ET_EXEC || !interp_header.e_phnum ||
		interp_header.e_phentsize != sizeof(ELF_PHT_t) ||
		interp_header.e_phoff > file_size ||
		stduint(interp_header.e_phnum) >
			(file_size - interp_header.e_phoff) / sizeof(ELF_PHT_t))
		return false;
	auto headers = uni::make_unique<ELF_PHT_t[]>(interp_header.e_phnum);
	if (!headers || !PlanELFFileLoads(&interp_device, interp_header,
		file_size, 0, block_buffer, headers.get()) ||
		!InterpreterLoadRangeAvailable(pb, headers.get(),
			interp_header.e_phnum)) return false;
	*out_entry = _IMM(interp_header.e_entry);
	// The VMA owns its own file reference after the temporary interpreter handle closes.
	if (!MapELFFileLoads(pb, &interp_device, file, headers.get(),
		interp_header.e_phnum, 0, block_buffer)) return false;
	ploginfo("[interp-vma] image=%p inode=%[64H] entry=%[x]", pb, file->f_inode->identity, *out_entry);
	// Log one materialized writable boundary page for cross-process comparison.
	bool logged_private = false;
	for (stduint i = 0; i < interp_header.e_phnum && !logged_private; i++) {
		const auto& ph = headers[i];
		if (ph.p_type != PT_LOAD || !ph.p_memsz || !(ph.p_flags & PF_W))
			continue;
		const stduint candidates[2] = {
			ph.p_vaddr & ~_IMM(PAGE_SIZE - 1),
			(ph.p_vaddr + (ph.p_filesz ? ph.p_filesz - 1 : 0)) &
				~_IMM(PAGE_SIZE - 1),
		};
		for (stduint candidate : candidates) {
			ELFLoadPagePlan plan;
			if (!DescribeELFLoadPage(headers.get(), interp_header.e_phnum,
				0, candidate, plan) || plan.kind != ELFLoadPageKind::Private ||
				!(plan.flags & PGPROP_writable)) continue;
			void* physical = pb->paging[candidate];
			if (physical == (void*)~_IMM0) continue;
			plogtrac("[interp-private] image=%p va=%[x] pa=%[x]", pb, candidate, _IMM(physical));
			logged_private = true;
			break;
		}
	}
	return true;
	#else
	return false;
	#endif
}

static ProcessBlock* CreateELFImage(BlockTrait* source,
	uni::vfs_file* image_file, byte ring, bool append) {
	#if (_MCCA & 0xFF00) == 0x8600 || (_MCCA & 0xFF00) == 0x1000
	#if CONFIG_ENABLE_MMU
	uint64 image_version = 0;
	stduint image_size = 0;
	if (image_file) {
		MutexLocal guard(&uni::vfs_lock);
		image_version = image_file->f_inode->content_version;
		image_size = image_file->f_inode->i_size;
	}
	#endif
	String block_buffer(String::Charset::Memory, 512);
	struct ELF_Header_t header;
	if (source->Read(0, &header, sizeof(header),
		(byte*)block_buffer.reflect()) != sizeof(header)) return nullptr;
	if (MemCompare((const char*)header.e_ident, "\x7F""ELF", 4)) {
		plogerro("%s: Invalid ELF File Magic Number", __FUNCIDEN__);
		return nullptr;
	}
	stduint source_size = 0;
	#if CONFIG_ENABLE_MMU
	if (image_file) source_size = image_size;
	#endif
	if (!source_size) {
		const stduint unit_size = source->Block_Size ? source->Block_Size : 512;
		const stduint units = source->getUnits();
		if (units > ~_IMM0 / unit_size) return nullptr;
		source_size = units * unit_size;
	}
	const bool map_main_file = CONFIG_ENABLE_MMU && image_file && ring != RING_M;
	#if CONFIG_ENABLE_MMU
	auto program_headers = map_main_file ?
		uni::make_unique<ELF_PHT_t[]>(header.e_phnum) :
		uni::UniquePtr<ELF_PHT_t[]>();
	const stduint planned_bias = header.e_type == ET_DYN ? 0x400000 : 0;
	if (map_main_file && (!program_headers ||
		!PlanELFFileLoads(source, header, image_size,
			planned_bias, (byte*)block_buffer.reflect(),
			program_headers.get()))) return nullptr;
	#endif
	
	ProcessBlock* pb = Taskman::AllocateTask();
	if (!pb) return nullptr;
	#if CONFIG_ENABLE_MMU
	ELFImageScope image_scope;
	image_scope.process = pb;
	if (map_main_file) {
		if (!uni::PinExecutableImage(pb, image_file,
			image_version)) return nullptr;
	}
	#endif
	pb->ring = ring;
	pb->parent_id = Task_Kernel;
	*pb->focus_tty.Lock() = nullptr;
	
	auto tb = Taskman::AllocateThread();
	if (!tb) return nullptr;
	pb->main_thread = tb;
	pb->thread_list_head = tb;
	tb->parent_process = pb;
	#if CONFIG_ENABLE_MMU
	image_scope.thread = tb;
	#endif

	tb->stack_size = HIGHER_STACK_SIZE;
	auto stack_norm = (byte*)mempool.allocate(tb->stack_size, PAGESIZE_4KB);
	auto stack_ring = ring != RING_M ? (byte*)mempool.allocate(tb->stack_size, PAGESIZE_4KB) : stack_norm;
	#if CONFIG_ENABLE_MMU
	image_scope.normal_stack = stack_norm;
	image_scope.privileged_stack = stack_ring;
	#endif
	if (!stack_norm || !stack_ring) return nullptr;
	tb->stack_lineaddr = (byte*)0x1000;
	tb->stack_levladdr = stack_ring;
	// [DIAG] Write stack canary at the bottom of the kernel stack
	*(stduint*)stack_ring = 0xDEADBEEF;

	// ---- CR3 Mapping ---- //
	stduint kernel_size = _TEMP 0x00400000;
	#if (_MCCA & 0xFF00) == 0x8600
	tb->context.CR3 = Taskman::CreatePaging(pb, ring, _IMM(stack_norm));
	#elif _MCCA == 0x1032 || _MCCA == 0x1064
	tb->context.satp = Taskman::CreatePaging(pb, ring, _IMM(stack_norm));
	#endif

	stduint load_bias = (header.e_type == ET_DYN) ? 0x400000 : 0;
	tb->context.IP = _IMM(header.e_entry) + load_bias;

	uni::vfs_file* opened_interp = nullptr;
	if (!_Taskman_Resolve_Interp(source, header, source_size,
		(byte*)block_buffer.reflect(), nullptr, &opened_interp)) {
		return nullptr;
	}
	uni::UniquePtr<uni::vfs_file, ELFInterpreterFileCloser> interp_file(opened_interp);

	stduint load_slice_p = 0;
	stduint max_seg_end = 0;
	stduint phdr_addr = 0;
	#if CONFIG_ENABLE_MMU
	if (map_main_file) {
		if (!RecordMainELFLoads(pb, program_headers.get(), header.e_phnum,
			load_bias, load_slice_p, max_seg_end) ||
			!MapELFFileLoads(pb, source, image_file,
				program_headers.get(), header.e_phnum, load_bias,
				(byte*)block_buffer.reflect()))
			return nullptr;
	}
	#endif
	for0(i, header.e_phnum) {
		struct ELF_PHT_t ph;
		stduint ph_offset = header.e_phoff + header.e_phentsize * i;
		source->Read(ph_offset, &ph, sizeof(ph), (byte*)block_buffer.reflect());
		if (ph.p_type == PT_DYNAMIC) {
			// PIE STATIC will have PT_DYNAMIC, do not abort
		}
		if (ph.p_type == PT_PHDR) phdr_addr = ph.p_vaddr + load_bias;
		if (ph.p_type == PT_LOAD && ph.p_offset == 0 && !phdr_addr) phdr_addr = ph.p_vaddr + load_bias + header.e_phoff;
		if (ph.p_type == PT_LOAD && ph.p_memsz && !map_main_file)
		{
			bool executable = !!(ph.p_flags & PF_X);
			bool writable = !!(ph.p_flags & PF_W);
			bool user = (ring != RING_M);
			if (!Taskman::CreateELF_Carry((char*)(ph.p_vaddr + load_bias), ph.p_memsz, source, ph.p_offset, ph.p_filesz, pb->paging, (byte*)block_buffer.reflect(), executable, writable, user)) {
				plogerro("%s: segment load failed (vaddr=%[x] memsz=%u)", __FUNCIDEN__, ph.p_vaddr + load_bias, ph.p_memsz);
				return nullptr;
			}
			if (load_slice_p < numsof(pb->load_slices)) {
				pb->load_slices[load_slice_p].address = ph.p_vaddr + load_bias;
				pb->load_slices[load_slice_p].length = ph.p_memsz;
				load_slice_p++;
			}
			else {
				plogwarn("[Taskman] CreateELF LoadSlice Overflow");
			}
			stduint seg_end = ph.p_vaddr + load_bias + ph.p_memsz;
			if (seg_end > max_seg_end) {
				max_seg_end = seg_end;
			}
		}
	}
	if (!_Taskman_Relocate_PIE(source, header, load_bias, pb, false,
		(byte*)block_buffer.reflect())) {
		plogerro("%s: PIE relocation failed", __FUNCIDEN__);
		return nullptr;
	}

	if (max_seg_end > 0) {
		pb->heapbtm = (max_seg_end + 0xFFF) & ~_IMM(0xFFF);
		pb->heapbtm += 0x10000;
		pb->heaptop = pb->heapbtm;
	} else {
		pb->heapbtm = 0x08000000;
		pb->heaptop = pb->heapbtm;
	}

	// ---- Stack and Gen.Regis ---- //
	const stduint stack_loc_top = _IMM(tb->stack_lineaddr) + tb->stack_size;
	const stduint initial_sp = Taskman::SetupStack(pb, pb, nullptr, nullptr,
		(stduint)header.e_entry + load_bias, phdr_addr, header.e_phnum, header.e_phentsize);

	#if (_MCCA & 0xFF00) == 0x8600
	tb->context.RING = ring;
	tb->context.SP = initial_sp;
	// ploginfo("[elf] entry=%[x] sp=%[x] stack=[%[x],%[x]) heap=[%[x],%[x]) ring=%u",
	// 	tb->context.IP, tb->context.SP, tb->stack_lineaddr, stack_loc_top, pb->heapbtm, pb->heaptop, ring);
	Taskman::SetSegment(&tb->context);
	#if (_MCCA & 0xFF00) == 0x8600
	// Initialize FPU/SSE context to a safe state (masked exceptions)
	// Offset 0: FPU Control Word (0x037F = Mask all exceptions)
	treat<uint16>(&tb->context.floating_point_context[0]) = 0x037F;
	// Offset 24: MXCSR (0x1F80 = Mask all SSE exceptions)
	treat<uint32>(&tb->context.floating_point_context[24]) = 0x1F80;
	#endif

	#elif _MCCA == 0x1032 || _MCCA == 0x1064
	constexpr stduint floating_support = (1 << 13);
	tb->context.sp = initial_sp;
	tb->context.IP = _IMM(header.e_entry) + load_bias;
	tb->context.mstatus = (ring << 11) | floating_support | _MSTATUS_MPIE;
	tb->context.kernel_sp = _IMM(tb->stack_levladdr) + tb->stack_size - 0x10;

	#endif

	if (interp_file) {
		stduint interp_entry = 0;
		if (!_Taskman_Load_Interp(interp_file.get(), pb, header.e_machine,
			(byte*)block_buffer.reflect(), &interp_entry)) {
			return nullptr;
		}
		tb->context.IP = interp_entry; // Override entry point
	}

	tb->priority = (ring != RING_M) ? 4 : 0;
	tb->time_slice = (ring != RING_M) ? 3 : 4;
	if (append) {
		Taskman::Append(pb);
		Taskman::AppendThread(tb);
	}
	#if CONFIG_ENABLE_MMU
	image_scope.Keep();
	#endif
	return pb;
	#elif !CONFIG_ENABLE_MMU
	// NoMMU: one contiguous flat region holds the whole image, relocations are applied in place
	String block_buffer(String::Charset::Memory, 512);
	struct ELF_Header_t header;
	source->Read(0, &header, sizeof(header), (byte*)block_buffer.reflect());
	if (MemCompare((const char*)header.e_ident, "\x7F""ELF", 4)) {
		plogerro("%s: Invalid ELF File Magic Number", __FUNCIDEN__);
		return nullptr;
	}
	if (header.e_type != ET_DYN || !header.e_phnum) {
		plogerro("%s: only ET_DYN (static PIE) is loadable without MMU", __FUNCIDEN__);
		return nullptr;
	}
	stduint image_span = 0, image_align = 0x1000;
	for0(i, header.e_phnum) {
		struct ELF_PHT_t ph;
		source->Read(header.e_phoff + i * header.e_phentsize, &ph, sizeof(ph), (byte*)block_buffer.reflect());
		if (ph.p_type != PT_LOAD || !ph.p_memsz) continue;
		if (ph.p_vaddr + ph.p_memsz > image_span) image_span = ph.p_vaddr + ph.p_memsz;
		if (ph.p_align > image_align) image_align = ph.p_align;
	}
	if (!image_span) {
		plogerro("%s: no PT_LOAD segment", __FUNCIDEN__);
		return nullptr;
	}
	stduint align_expo = 12;// 4KB meets the usual p_align, mempool alignment is a power-of-two exponent
	while ((1u << align_expo) < image_align && align_expo < 20) align_expo++;
	constexpr stduint flat_heap_reserve = 7u << 20;// the block owns the process heap too, so its MPU region covers malloc
	while ((1u << align_expo) < image_span + flat_heap_reserve && align_expo < 24) align_expo++;// the block is its own MPU region, so it owns a whole power of two
	const stduint image_block = 1u << align_expo;
	byte* image = (byte*)mempool.allocate(image_block, align_expo);
	if (!image) {
		plogerro("%s: no memory for the image (%u bytes)", __FUNCIDEN__, image_block);
		return nullptr;
	}
	MemSet(image, 0, image_block);// zeroing keeps the BSS tail of every segment
	const stduint load_bias = _IMM(image);// a PIE links from vaddr 0, so the image base is vaddr 0
	for0(i, header.e_phnum) {
		struct ELF_PHT_t ph;
		source->Read(header.e_phoff + i * header.e_phentsize, &ph, sizeof(ph), (byte*)block_buffer.reflect());
		if (ph.p_type != PT_LOAD || !ph.p_filesz) continue;
		byte* dest = image + ph.p_vaddr;
		stduint done = 0;
		while (done < ph.p_filesz) {
			stduint chunk = ph.p_filesz - done;
			if (chunk > 0x1000) chunk = 0x1000;
			stduint got = source->Read(ph.p_offset + done, dest + done, chunk, (byte*)block_buffer.reflect());
			if (got != chunk) {
				plogerro("%s: segment read failed (%u of %u)", __FUNCIDEN__, got, chunk);
				return nullptr;
			}
			done += chunk;
		}
	}
	// a faulted task resumes here to raise a clean exit syscall
	*(uint16*)(image + image_block - 8) = 0xDF00;// svc #0
	*(uint16*)(image + image_block - 6) = 0xE7FE;// b .
	if (!_Taskman_Relocate_PIE_Flat(source, header, load_bias, (byte*)block_buffer.reflect())) {
		plogerro("%s: PIE relocation failed", __FUNCIDEN__);
		return nullptr;
	}
	#if defined(_ARCH_ARM_ProfileM)
	L1C.CleanDCacheRange(image, image_block);// the image was written through the D-cache
	L1C.InvalidateICacheAll();
	#endif
	ProcessBlock* pb = Taskman::Create((void*)((load_bias + header.e_entry) | 1), ring, false);
	if (!pb) return nullptr;
	const stduint arg_sp = Taskman::SetupStack(pb, pb, nullptr, nullptr,
		load_bias + header.e_entry, 0, header.e_phnum, header.e_phentsize);
	if (arg_sp) {
		auto& ctx = pb->main_thread->context;
		#if defined(_ARCH_ARM_ProfileM)
		stduint* frame = (stduint*)(arg_sp - 0x20);
		frame[0] = *(const stduint*)arg_sp;
		frame[1] = arg_sp + sizeof(stduint);
		frame[2] = arg_sp + (frame[0] + 2) * sizeof(stduint);
		frame[3] = 0;
		frame[4] = 0;
		frame[5] = 0;
		frame[6] = (load_bias + header.e_entry) | 1;
		frame[7] = 0x01000000;
		ctx.sp = arg_sp - 0x20;
		#else
		ctx.sp = arg_sp;
		#endif
	}
	pb->load_slices[0].address = load_bias;
	pb->load_slices[0].length = image_block;// the release and the MPU region cover the same range
	pb->heapbtm = (load_bias + image_span + 0xFFF) & ~_IMM(0xFFF);// the heap shares the image block, so its MPU region covers it
	pb->heaptop = pb->heapbtm;
	ploginfo("[ELF] flat base=%[x] span=%u entry=%[x]", load_bias, image_span, load_bias + header.e_entry);
	if (append) {
		Taskman::Append(pb);
		Taskman::AppendThread(pb->main_thread);
	}
	return pb;
	#endif
	return nullptr;
}

ProcessBlock* Taskman::CreateELF(BlockTrait* source, byte ring, bool append) {
	return CreateELFImage(source, nullptr, ring, append);
}

#if CONFIG_ENABLE_MMU
ProcessBlock* Taskman_CreateELF_File(BlockTrait* source,
	uni::vfs_file* image_file, byte ring, bool append) {
	return CreateELFImage(source, image_file, ring, append);
}

#endif


