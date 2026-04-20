// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <c/format/ELF.h>

#include "../include/filesys.hpp"

void* higher_stacks[PCU_CORES_MAX];// when 1 core 1 stack

#if _MCCA == 0x8632
constexpr unsigned ldt_entry_cnt = 8;
alignas(16) static descriptor_t _LDT[ldt_entry_cnt];
#define FLAT_CODE_R3_PROP 0x00CFFA00
#define FLAT_DATA_R3_PROP 0x00CFF200
static void make_LDT(descriptor_t* ldt_alias, byte ring) {
	for0(i, 4) {
		ldt_alias[i]._data = (uint64(FLAT_CODE_R3_PROP) << 32) | 0x0000FFFF;
		ldt_alias[i].DPL = i;
		ldt_alias[i + 4]._data = (uint64(FLAT_DATA_R3_PROP) << 32) | 0x0000FFFF;
		ldt_alias[i + 4].DPL = i;
	}
	// : Although the stack segment is not Expand-down, the ESP always decreases.
}
#endif

static int ProcCmp(pureptr_t a, pureptr_t b) {
	return treat<ProcessBlock>(((Dnode*)a)->offs).pid -
	treat<ProcessBlock>(((Dnode*)b)->offs).pid;
}

#if   _MCCA == 0x8632
#define _NORMAL_RINGSTACK         0xFFC01000ull
#elif _MCCA == 0x8664
#define _NORMAL_RINGSTACK 0xFFFFFFFFC0001000ull
#endif

static void _Mapping_Core_Stack(Paging& paging) {
	#if _MCCA == 0x8632
	//{TODO} Map more cores, cpu1 at 0x0000FFFFFFFF8000ull...
	paging.Map(0xFFFFF000u, _IMM(higher_stacks[0]), 0x1000, PAGESIZE_4KB, PGPROP_present | PGPROP_writable);// High Part

	#elif _MCCA == 0x8664
	//{TODO} Map more cores, cpu1 at 0x0000FFFFFFFF8000ull...
	paging.Map(0x0000FFFFFFFFF000ull, _IMM(higher_stacks[0]), 0x1000, PAGESIZE_4KB, PGPROP_present | PGPROP_writable);// High Part

	#elif (_MCCA & 0xFF00) == 0x1000
	// M-MCCA need not map kernel
	// S-MCCA need map kernel (TODO)
	#endif
}
static stduint _Taskman_Create_Paging(ProcessBlock *ppb, byte ring, stduint stack_norm) {
	// [PHINA]: should include LDT in Paging if use jmp-tss: pb->paging.Map(_IMM(page), _IMM(page), allocsize, PAGESIZE_4KB, PGPROP_present | PGPROP_writable);
	// keep 0x00000000 default empty page
	#if (_MCCA & 0xFF00) == 0x8600
	if (ring == 3) {
		ppb->paging.Reset();
		// stack
		ppb->paging.Map(_IMM(ppb->main_thread->stack_lineaddr), (stack_norm), ppb->main_thread->stack_size, PAGESIZE_4KB, PGPROP_present | PGPROP_writable | PGPROP_user_access);
		ppb->paging.Map(_NORMAL_RINGSTACK, _IMM(ppb->main_thread->stack_levladdr), ppb->main_thread->stack_size, PAGESIZE_4KB, PGPROP_present | PGPROP_writable);
		//
		#if   _MCCA == 0x8632
		ppb->paging.Map(0x80000000, 0x00000000, 0x04000000, PAGESIZE_4KB, PGPROP_present | PGPROP_writable);
		// pb->paging.Map(0x80000000, 0x00000000, kernel_size, true, _Comment(R0) false);// should include LDT
		#elif _MCCA == 0x8664
		ppb->paging.Map(0x0000FFFFC0000000ull,
			0x0000000000000000ull, 0x1000,
			PAGESIZE_4KB, PGPROP_present | PGPROP_writable
		);
		ppb->paging.Map(0x0000FFFFC0010000ull,
			0x0000000000010000ull, _IMM1S(PAGESIZE_2MB) - 0x10000,
			PAGESIZE_4KB, PGPROP_present | PGPROP_writable
		);
		ppb->paging.Map(0x0000FFFFC0000000ull + _IMM1S(PAGESIZE_2MB),
			0x0000000000000000ull + _IMM1S(PAGESIZE_2MB),
			0x40000000ull - 2 * _IMM1S(PAGESIZE_2MB),
			PAGESIZE_2MB, PGPROP_present | PGPROP_writable
		);// High Part
		#endif

		_Mapping_Core_Stack(ppb->paging);
	}
	else {
		ppb->paging.root_level_page = (PageEntry*)getCR3();
	}
	return _IMM(ppb->paging.root_level_page);

	#elif (_MCCA & 0xFF00) == 0x1000
	ppb->paging.Reset();
	// stack
	ppb->paging.Map(_IMM(ppb->main_thread->stack_lineaddr), _IMM(stack_norm), ppb->main_thread->stack_size, PAGESIZE_4KB, PGPROP_present | PGPROP_writable | PGPROP_user_access);
	ppb->paging.Map(0x80000000, 0x80000000, 0x01000000, PAGESIZE_4KB, 
                PGPROP_present | PGPROP_writable);
	return ppb->paging.MakeSATP();

	#endif
	return nil;
}
extern stduint kernel_stack_top_cpu0[];
void Taskman::Initialize(stduint cpuid) {
	#if (_MCCA & 0xFF00) == 0x8600
	if (cpuid || PCU_CORES_TSS[0]) return; // already initialized
	PCU_CORES = PCU_CORES_MAX;
	auto addr = (TSS_t*)mem.allocate(sizeof(TSS_t) * PCU_CORES);
	MemSet(addr, 0, sizeof(TSS_t) * PCU_CORES);
	for0(i, PCU_CORES) {
		PCU_CORES_TSS[i] = addr;
		higher_stacks[i] = (mem.allocate(0x1000, PAGESIZE_4KB));
		#if _MCCA == 0x8664
		addr->RSP0 = _NORMAL_RINGSTACK + HIGHER_STACK_SIZE - 8;// for user-app in cpu0
		#else
		addr->ESP0 = _IMM(mem.allocate(0x1000));
		#endif
		addr++;
		//{} TEMP GDT_Alloc and tss.setRange
		if (i == 0)
		{
			mecocoa_global->gdt_ptr->tss.setRange(mglb(PCU_CORES_TSS[i]), sizeof(TSS_t) - 1);
			#if _MCCA == 0x8632
			const stduint stack_lev_top = _NORMAL_RINGSTACK + HIGHER_STACK_SIZE;
			PCU_CORES_TSS[i]->ESP0 = stack_lev_top - 0x10;
			PCU_CORES_TSS[i]->SS0 = SegData;
			PCU_CORES_TSS[i]->ESP1 = stack_lev_top - 0x10;
			PCU_CORES_TSS[i]->SS1 = 8 * 5 + 4 + 1;// 4:LDT 8*5:SS1 1:Ring1
			PCU_CORES_TSS[i]->ESP2 = stack_lev_top - 0x10;
			PCU_CORES_TSS[i]->SS2 = 8 * 6 + 4 + 2;// 4:LDT 8*6:SS2 2:Ring2
			//
			PCU_CORES_TSS[i]->LDTDptr = SegGLDT + 3; // LDT yo GDT
			PCU_CORES_TSS[i]->LDTLength = 8 * 8 - 1;
			PCU_CORES_TSS[i]->STRC_15_T = 0;
			PCU_CORES_TSS[i]->IO_MAP = sizeof(TSS_t) - 1;
			#endif
		}
	}
	_Mapping_Core_Stack(kernel_paging);
	
	
	#if _MCCA == 0x8632// TEMP x64 do not use LDT (no R1 and R2)
	kernel_paging.Map(_NORMAL_RINGSTACK, (stduint)mem.allocate(HIGHER_STACK_SIZE), HIGHER_STACK_SIZE, PAGESIZE_4KB, PGPROP_present | PGPROP_writable);
	make_LDT(_LDT, 3);
	const auto LDTLength = sizeof(_LDT) - 1;
	descriptor_t* const GDT = (descriptor_t*)mecocoa_global->gdt_ptr;
	Descriptor32Set(&GDT[SegGLDT / 8], mglb(&_LDT), LDTLength, _Dptr_LDT, 0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */);
	#endif
	
	loadTask(SegTSS0);

	#if _MCCA == 0x8632
	_ASM("LLDT %w0" : : "r"(SegGLDT) : "memory");
	#endif

	#endif// (_MCCA & 0xFF00) == 0x8600

	// register kernel as pid 0
	auto kernel_task = AllocateTask();
	auto kernel_thread = AllocateThread();
	kernel_task->main_thread = kernel_thread;
	kernel_task->thread_list_head = kernel_thread;
	kernel_thread->parent_process = kernel_task;
	
	min_available_left = chain.Append(kernel_task);
	min_available_thleft = thchain.Append(kernel_thread);
	kernel_task->state = ProcessBlock::State::Active;
	kernel_task->paging.root_level_page = kernel_paging.root_level_page;
	
	kernel_thread->tid = 0;
	current_thread[cpuid] = kernel_thread;
	//
	chain.Compare_f = ProcCmp;
	min_available_pid = 1;
	min_available_tid = 1;

	#if (_MCCA & 0xFF00) == 0x1000
	setMSCRATCH _IMM(&kernel_thread->context);
	setMIE(getMIE() | _MIE_MSIE);// software interrupts
	kernel_thread->context.kernel_sp = (usize)kernel_stack_top_cpu0;
	#endif
	kernel_thread->priority = 12;
	kernel_thread->time_slice = 4;
	kernel_task->focus_tty = vttys[0];
	Taskman::AppendThread(kernel_thread);

	auto idle_task = Create((void*)Taskman::Idle, RING_M, false);
	if (idle_task) {
		idle_task->main_thread->priority = 31; // lowest priority
		idle_task->main_thread->time_slice = 1;
		idle_thread[cpuid] = idle_task->main_thread;
	}
}


#if (_MCCA & 0xFF00) == 0x8600
static stduint getCS(stduint ring) {
	if (!ring) return (__BITS__ == 64 ? SegCo64 : SegCo32);
	if (ring == 3) return SegCoR3 | ring;
	else return 8 * (ring) + 0b100 | ring;
}
static stduint getSS(stduint ring) {
	if (!ring) return SegData;
	if (ring == 3) return SegDaR3 | ring;
	else return 8 * (4 + ring) + 0b100 + ring;
}
static void SetSegment(NormalTaskContext* ntc) {
	REG_FLAG_t flag = {};
	flag._r1 = 1, flag.IF = 1, flag.IOPL = (ntc->RING == 1 ? 0x1u : 0u);
	ntc->FLAG = cast<stduint>(flag);
	ntc->CS = getCS(ntc->RING);
	ntc->SS = getSS(ntc->RING);
	#if _MCCA == 0x8632
	ntc->DS = ntc->SS;
	ntc->ES = ntc->SS;
	ntc->FS = ntc->SS;
	ntc->GS = ntc->SS;
	#elif _MCCA == 0x8664
	ntc->DS = ntc->ES = ntc->FS = ntc->GS = nil;
	#endif
}
#endif

ProcessBlock* Taskman::Create(void* entry, byte ring, bool append)
{
	auto ppb = AllocateTask();
	if (!ppb) return nullptr;
	ppb->ring = ring;
	ppb->parent_id = Task_Kernel;
	ppb->focus_tty = vttys[0];
	
	auto tb = AllocateThread();
	ppb->main_thread = tb;
	ppb->thread_list_head = tb;
	tb->parent_process = ppb;
	
	#if (_MCCA & 0xFF00) == 0x8600
	ppb->paging.root_level_page = (PageEntry *)getCR3();
	auto& new_ctx = tb->context;
	new_ctx.IP = _IMM(entry);
	new_ctx.CR3 = getCR3();
	new_ctx.RING = ring;
	SetSegment(&new_ctx);
	treat<uint32>(&new_ctx.floating_point_context[24]) = 0x1F80;// ban all MXCSR exception
	tb->stack_size = DEFAULT_STACK_SIZE;
	tb->stack_lineaddr = (byte*)mempool.allocate(tb->stack_size, 12);
	tb->stack_levladdr = ring != RING_M ? (byte*)mempool.allocate(tb->stack_size, 12) : tb->stack_lineaddr;
	const stduint stack_top = _IMM(tb->stack_lineaddr) + DEFAULT_STACK_SIZE;
	new_ctx.SP = (stack_top & ~0xFlu) - 8;

	#elif (_MCCA & 0xFF00) == 0x1000
	tb->stack_size = DEFAULT_STACK_SIZE;
	tb->stack_levladdr = (byte*)mempool.allocate(tb->stack_size, PAGESIZE_4KB);
	auto& ctx = tb->context;
	ctx.sp = (stduint)mempool.allocate(DEFAULT_STACK_SIZE, 12) + DEFAULT_STACK_SIZE - 0x10;
	ctx.IP = _IMM(entry);
	ctx.mstatus = (ring << 11) | _MSTATUS_MPIE;
	ctx.kernel_sp = _IMM(tb->stack_levladdr) + tb->stack_size - 0x10;

	if (ring == RING_U) {
		_TODO// Paging
	}

	#endif

	tb->priority = (ring == RING_U) ? 3 : 0;
	tb->time_slice = (ring == RING_U) ? 3 : 4;
	
	if (append) {
		Append(ppb);
		AppendThread(tb);
	}
	return ppb;
}
static void _CreateELF_Carry(char* vaddr, stduint mem_length, BlockTrait* source, stduint file_offset, stduint file_size, Paging& pg, byte* buffer) {
	// if page !exist, map it; write it.
	stduint compensation = _IMM(vaddr) & 0xFFF;
	stduint v_start1 = _IMM(vaddr) & ~_IMM(0xFFF);
	if (_IMM(vaddr) < 0x10000) {
		plogerro("_CreateELF_Carry: vaddr is too low");
	}
	#if _MCCA == 0x8632 || _MCCA == 0x1032
	if (_IMM(vaddr) >= 0x80000000) {
		plogerro("_CreateELF_Carry");
	}
	#elif _MCCA == 0x8664
	if (_IMM(vaddr) >= 0xFFFFC0000000ull) {
		plogerro("_CreateELF_Carry");
	}
	#elif _MCCA == 0x1064
    if (_IMM(vaddr) >= 0x4000000000ull) { 
        plogerro("_CreateELF_Carry: vaddr out of RV64 Sv39 user space");
    }
	#endif
	// ploginfo("_CreateELF_Carry(%[x], %[x], %[x])  pg(%[x])", vaddr, length, phy_src, pg);
	stduint bytes_read = 0; 
	for0(i, (mem_length + compensation + 0xFFF) / 0x1000) {
		auto page_entry = pg.getEntry(_IMM(v_start1));
		stduint phy;
		
		if (_IMM(page_entry) == ~_IMM0 || !page_entry->isPresent()) {
			phy = _IMM(mempool.allocate(0x1000, 12));
			pg.Map(v_start1, phy, 0x1000, PAGESIZE_4KB, PGPROP_present | PGPROP_writable | PGPROP_user_access);
			page_entry = pg.getEntry(_IMM(v_start1));
			if (_IMM(page_entry) == ~_IMM0 || !page_entry->isPresent()) {
				plogerro("Mapping failed %[x] -> %[x]", v_start1, phy);
			}
		}
		else phy = page_entry->getAddress();

		stduint chunk_size = 0x1000 - compensation;
		if (chunk_size > mem_length - bytes_read) {
			chunk_size = mem_length - bytes_read; 
		}

		stduint phy_dest = phy + compensation;

		// xDATA or BSS 
		if (bytes_read >= file_size) {
			MemSet((void*)phy_dest, 0, chunk_size);
		} 
		else {
			stduint copy_size = chunk_size;
			if (bytes_read + chunk_size > file_size) {
				copy_size = file_size - bytes_read;
				MemSet((void*)(phy_dest + copy_size), 0, chunk_size - copy_size);
			}
			auto ret = source->Read(file_offset + bytes_read, (void*)phy_dest, copy_size, buffer);
			if (ret != copy_size) {
				plogwarn("%s %u: Read failed", __FUNCIDEN__, __LINE__);
			}
		}

		bytes_read += chunk_size;
		v_start1 += 0x1000;
		compensation = 0; 
	}
}
ProcessBlock* Taskman::CreateELF(BlockTrait* source, byte ring) {
	#if (_MCCA & 0xFF00) == 0x8600 || (_MCCA & 0xFF00) == 0x1000
	auto block_buffer = new byte[512];
	struct ELF_Header_t header;
	source->Read(0, &header, sizeof(header), block_buffer);
	if (MemCompare((const char*)header.e_ident, "\x7F""ELF", 4)) {
		delete[] block_buffer;
		plogerro("%s: Invalid ELF File Magic Number", __FUNCIDEN__);
		return nullptr;
	}
	
	ProcessBlock* pb = Taskman::AllocateTask();
	pb->ring = ring;
	pb->parent_id = Task_Kernel;
	pb->focus_tty = vttys[0];
	
	auto tb = AllocateThread();
	pb->main_thread = tb;
	pb->thread_list_head = tb;
	tb->parent_process = pb;

	tb->stack_size = HIGHER_STACK_SIZE;
	auto stack_norm = (byte*)mempool.allocate(tb->stack_size, PAGESIZE_4KB);
	auto stack_ring = ring != RING_M ? (byte*)mempool.allocate(tb->stack_size, PAGESIZE_4KB) : stack_norm;
	tb->stack_lineaddr = (byte*)0x1000;
	tb->stack_levladdr = stack_ring;

	// ---- CR3 Mapping ---- //
	stduint kernel_size = _TEMP 0x00400000;
	#if (_MCCA & 0xFF00) == 0x8600
	tb->context.CR3 = _Taskman_Create_Paging(pb, ring, _IMM(stack_norm));
	#elif _MCCA == 0x1032 || _MCCA == 0x1064
	tb->context.satp = _Taskman_Create_Paging(pb, ring, _IMM(stack_norm));
	#endif

	tb->context.IP = _IMM(header.e_entry);

	stduint load_slice_p = 0;
	for0(i, header.e_phnum) {
		struct ELF_PHT_t ph;
		stduint ph_offset = header.e_phoff + header.e_phentsize * i;
		source->Read(ph_offset, &ph, sizeof(ph), block_buffer);
		if (ph.p_type == PT_LOAD && ph.p_memsz) 
		{
			_CreateELF_Carry((char*)ph.p_vaddr, ph.p_memsz, source, ph.p_offset, ph.p_filesz, pb->paging, block_buffer);
			if (load_slice_p < numsof(pb->load_slices)) {
				pb->load_slices[load_slice_p].address = ph.p_vaddr;
				pb->load_slices[load_slice_p].length = ph.p_memsz;
				load_slice_p++;
			}
			else {
				plogwarn("[Taskman] CreateELF LoadSlice Overflow");
			}
		}
	}
	delete[] block_buffer;

	// ---- Stack and Gen.Regis ---- //
	const stduint stack_loc_top = _IMM(tb->stack_lineaddr) + tb->stack_size;

	#if (_MCCA & 0xFF00) == 0x8600
	tb->context.RING = ring;
	tb->context.SP = (stack_loc_top & ~0xFlu) - 8 - 0x10;// single stack
	treat<uint32>(&tb->context.floating_point_context[24]) = 0x1F80;// ban all MXCSR exception
	SetSegment(&tb->context);

	#elif _MCCA == 0x1032 || _MCCA == 0x1064
	constexpr stduint floating_support = (1 << 13);
	tb->context.sp = (stack_loc_top & ~0xFlu) - 0x10;
	tb->context.IP = _IMM(header.e_entry);
	tb->context.mstatus = (ring << 11) | floating_support | _MSTATUS_MPIE;
	tb->context.kernel_sp = _IMM(tb->stack_levladdr) + tb->stack_size - 0x10;

	#endif

	tb->priority = (ring == RING_U) ? 4 : 0;
	tb->time_slice = (ring == RING_U) ? 3 : 4;

	return pb;
	#endif
}

ProcessBlock* Taskman::CreateFork(ProcessBlock* fo, const CallgateFrame* frame) {
	ProcessBlock* pb = Taskman::AllocateTask();
	if (!pb) return 0;
	
	ThreadBlock* target_fo_thread = fo->main_thread; // Fork clones main thread context for now

	auto ring = fo->ring;
	pb->ring = ring;
	pb->parent_id = fo->getID();
	pb->focus_tty = fo->focus_tty;
	
	auto tb = AllocateThread();
	pb->main_thread = tb;
	pb->thread_list_head = tb;
	tb->parent_process = pb;

	#if _MCCA == 0x8632
	// check undone and duplicate operations
	// 1. Context
	// 2. Copy segmants and stack
	// 3. FS Operation

	// ---- Stack ---- //
	tb->stack_size = target_fo_thread->stack_size;
	auto stack_norm = (byte*)mempool.allocate(tb->stack_size, 12);
	auto stack_ring = ring ? (byte*)mempool.allocate(tb->stack_size, 12) : stack_norm;
	tb->stack_lineaddr = target_fo_thread->stack_lineaddr;
	tb->stack_levladdr = stack_ring;

	// ---- Copy CR3 Mapping ---- //
	// - Segments Mapping with coping
	// - Kernel-Area Mapping
	// - (TODO) Heap Area Mapping
	tb->context = target_fo_thread->context;
	//{TEMP} use simple method
	tb->context.CR3 = _Taskman_Create_Paging(pb, ring, _IMM(stack_norm));
	for0a(i, fo->load_slices) {
		if (!fo->load_slices[i].length) break;
		pb->load_slices[i] = fo->load_slices[i];
		stduint appendix = fo->load_slices[i].address & _IMM(PAGE_SIZE - 1);
		stduint pagesize = vaultAlignHexpow(PAGE_SIZE, fo->load_slices[i].length + appendix);
		stduint newaddr = (stduint)mempool.allocate(pagesize, 12);
		stduint mapsrc = fo->load_slices[i].address & ~_IMM(PAGE_SIZE - 1);
		// ploginfo("fork.map: 0x%x->0x%x(0x%x)", mapsrc, newaddr, pagesize);
		pb->paging.Map(mapsrc, newaddr, pagesize, PAGESIZE_4KB, _TEMP PGPROP_present | PGPROP_writable | PGPROP_user_access);// Map and allocation
		// ploginfo("memcpyp: %x+%x, ., %x, ., %x", mapsrc, appendix, fo->load_slices[i].address, fo->load_slices[i].length);
		MemCopyP((char*)mapsrc + appendix, pb->paging,
			(void*)fo->load_slices[i].address, fo->paging,
			fo->load_slices[i].length);
	}

	tb->priority = target_fo_thread->priority;
	tb->time_slice = target_fo_thread->time_slice;

	// ---- Stack and Gen.Regis ---- //
	const stduint stack_loc_top = _IMM(tb->stack_lineaddr) + tb->stack_size;
	const stduint stack_lev_top = _TEMP 0x1000 + tb->stack_size * 2;
	//
	tb->context.ES = SegDaR3 | ring;
	tb->context.CS = getCS(ring);
	tb->context.SS = getSS(ring);
	tb->context.DS = tb->context.ES;
	tb->context.FS = tb->context.ES;
	tb->context.GS = tb->context.ES;
	
	MemCopyP(tb->stack_lineaddr, pb->paging, target_fo_thread->stack_lineaddr, fo->paging, tb->stack_size);
	MemCopyP(tb->stack_levladdr, kernel_paging, target_fo_thread->stack_levladdr, kernel_paging, tb->stack_size);

	tb->context.AX = nil; // return from fork()
	tb->context.CX = frame->cx;
	tb->context.DX = frame->dx;
	tb->context.BX = frame->bx;
	tb->context.SI = frame->si;
	tb->context.DI = frame->di;
	tb->context.BP = frame->bp;
	tb->context.IP = frame->ip;
	tb->context.SP = frame->sp0;

	// ---- File ---- //
	fo->sys_lock.Acquire();
	for0a(i, pb->pfiles) if (pb->pfiles[i]) {
		pb->pfiles[i] = fo->pfiles[i];
		pb->pfiles[i]->vfile->f_inode->ref_count++;
	}
	fo->sys_lock.Release();

	Taskman::Append(pb);
	Taskman::AppendThread(tb);
	return pb;

	#else
	return nullptr;
	#endif
}

//

ProcessBlock* Taskman::CreateFile(const char* path, byte ring, stduint parent) {
	//{} ELF
	auto label = StrIndexCharRight(path, '/');
	if (!label) label = path; else label++;
	vfs_dentry* d = Filesys::Index(path);
	if (d && d->d_inode && d->d_inode->i_sb && d->d_inode->i_sb->fs) {
		FileBlockBridge loop_device(d->d_inode->i_sb->fs, d->d_inode->internal_handler, d->d_inode->i_size, 512);
		if (auto task = Taskman::CreateELF(&loop_device, ring)) {
			task->parent_id = parent;
			printlog(_LOG_INFO, "Loaded %s from %s", label, path);
			return task;
		}
		else plogerro("%s: ELF Load Fail", label);
	}
	else plogwarn("%s: Not found at %s", label, path);
	return nullptr;
};

//

ProcessBlock* Taskman::Exec(stduint parent, rostr usr_fullpath, void* usr_argstack, stduint stacklen)
{
	char buf_fullpath[_TEMP 32];
	auto parent_pb = Locate(parent);
	StrCopyP(buf_fullpath, kernel_paging, usr_fullpath, parent_pb->paging, sizeof(buf_fullpath));
	ploginfo("%s: %u \"%s\" %x %x", __FUNCIDEN__, parent, buf_fullpath, usr_argstack, stacklen);

	auto new_pb = Taskman::CreateFile(buf_fullpath, RING_U, parent);
	if (!new_pb) {
		plogwarn("%s: failed to load file", __FUNCIDEN__);
		return nullptr;
	}

	stduint argc = 0;
	stduint argv_ptr = 0;
	stduint new_sp = new_pb->main_thread->context.SP;

	if (stacklen > 0 && usr_argstack != nullptr) {
		byte* temp_stack = new byte[stacklen];
		if (!temp_stack) {
			plogerro("%s: alloc temp_stack fail", __FUNCIDEN__);
		}
		else {
			MemCopyP(temp_stack, kernel_paging, usr_argstack, parent_pb->paging, stacklen);
			new_sp = (new_sp - stacklen) & ~_IMM(0xFlu);
			stduint delta = new_sp - _IMM(usr_argstack);

			char** q = (char**)temp_stack;
			for (; *q != nullptr; q++, argc++) {
				*q = (char*)(_IMM(*q) + delta);
			}
			MemCopyP((void*)new_sp, new_pb->paging, temp_stack, kernel_paging, stacklen);
			argv_ptr = new_sp;
			delete[] temp_stack;
		}
	}
	else {
		new_sp = (new_sp - sizeof(stduint)) & ~_IMM(0xFlu);
		stduint null_ptr = 0;
		MemCopyP((void*)new_sp, new_pb->paging, &null_ptr, kernel_paging, sizeof(stduint));
		argv_ptr = new_sp;
	}

	// [!] Unconditionally construct the C standard call frame for _start
	#if (_MCCA & 0xFF00) == 0x8600

	// Construct standard C call frame for x86: [Dummy Return IP, argc, argv]
	struct C_CallFrame {
		stduint dummy_return_ip;
		stduint argc;
		stduint argv;
	};

	C_CallFrame frame = { 0, argc, argv_ptr };
	new_sp = new_sp & ~_IMM(0xFlu);
	// System V ABI : ESP % 16 == 12
	new_sp = new_sp - 20;// new_sp -= sizeof(call_frame);
	MemCopyP((void*)new_sp, new_pb->paging, (void*)&frame, kernel_paging, sizeof(frame));
	new_pb->main_thread->context.SP = new_sp;	// esp = new stack pointer

	#elif (_MCCA & 0xFF00) == 0x1000

	new_pb->main_thread->context.a0 = argc; // a0 = argc
	new_pb->main_thread->context.a1 = argv_ptr; // a1 = argv
	new_pb->main_thread->context.sp = new_sp; // sp = new stack pointer

	#endif
	new_pb->focus_tty = parent_pb->focus_tty;
	Taskman::Append(new_pb);
	Taskman::AppendThread(new_pb->main_thread);
	return new_pb;
}

ProcessBlock* Taskman::Exet(stduint parent, rostr usr_fullpath, void* usr_argstack, stduint stacklen)
{
	char buf_fullpath[_TEMP 32];
	auto current_pb = Locate(parent); // In EXET, caller is the parent (itself)

	// Resolve path from old user space
	StrCopyP(buf_fullpath, kernel_paging, usr_fullpath, current_pb->paging, sizeof(buf_fullpath));
	auto label = StrIndexCharRight(buf_fullpath, '/');
	if (!label) label = buf_fullpath; else label++;

	ploginfo("%s: PID %u replacing image with \"%s\"", __FUNCIDEN__, parent, buf_fullpath);

	// Open the executable file (Mirroring CreateFile logic)
	vfs_dentry* d = Filesys::Index(buf_fullpath);
	if (!d || !d->d_inode || !d->d_inode->i_sb || !d->d_inode->i_sb->fs) {
		plogwarn("%s: File not found at %s", __FUNCIDEN__, buf_fullpath);
		return nullptr;
	}
	FileBlockBridge loop_device(d->d_inode->i_sb->fs, d->d_inode->internal_handler, d->d_inode->i_size, 512);

	// Read and Verify ELF Header
	auto block_buffer = new byte[512];
	struct ELF_Header_t header;
	loop_device.Read(0, &header, sizeof(header), block_buffer);
	if (MemCompare((const char*)header.e_ident, "\x7F""ELF", 4)) {
		delete[] block_buffer;
		plogerro("%s: Invalid ELF File Magic Number", __FUNCIDEN__);
		return nullptr;
	}

	// Backup arguments BEFORE destroying the old address space
	byte* temp_stack = nullptr;
	if (stacklen > 0 && usr_argstack != nullptr) {
		temp_stack = new byte[stacklen];
		if (!temp_stack) {
			delete[] block_buffer;
			plogerro("%s: alloc temp_stack fail", __FUNCIDEN__);
			return nullptr;
		}
		MemCopyP(temp_stack, kernel_paging, usr_argstack, current_pb->paging, stacklen);
	}

	// ==========================================
	// From here, the old process image is destroyed.
	// ==========================================

	// Destroy Old User Space Segments (Using your continuous memory logic)
	for0a(i, current_pb->load_slices) {
		if (!current_pb->load_slices[i].address) break;
		if (current_pb->load_slices[i].length >= PAGE_SIZE &&
			(byte*)current_pb->paging[(current_pb->load_slices[i].address) & ~0xFFF] + PAGE_SIZE ==
			(byte*)current_pb->paging[(current_pb->load_slices[i].address + PAGE_SIZE) & ~0xFFF]) {
			delete (byte*)current_pb->paging[(current_pb->load_slices[i].address) & ~0xFFF];
		}
		else while (current_pb->load_slices[i].length) {
			delete (byte*)current_pb->paging[(current_pb->load_slices[i].address) & ~0xFFF];
			current_pb->load_slices[i].address += 0x1000;
			current_pb->load_slices[i].length -= minof(0x1000, current_pb->load_slices[i].length);
		}
		current_pb->load_slices[i].address = 0;
		current_pb->load_slices[i].length = 0;
	}

	// Reset Paging but PRESERVE the physical address of the stacks
	stduint stack_norm_phy;
	if (current_pb->main_thread->stack_lineaddr == current_pb->main_thread->stack_levladdr) {
		stack_norm_phy = _IMM(current_pb->main_thread->stack_levladdr);
	}
	else {
		stack_norm_phy = _IMM(current_pb->paging[_IMM(current_pb->main_thread->stack_lineaddr) & ~0xFFF]);
	}

	current_pb->paging.~Paging(); // Destroy old page tables

	#if (_MCCA & 0xFF00) == 0x8600
	current_pb->main_thread->context.CR3 = _Taskman_Create_Paging(current_pb, current_pb->ring, stack_norm_phy);
	#elif _MCCA == 0x1032 || _MCCA == 0x1064
	current_pb->main_thread->context.satp = _Taskman_Create_Paging(current_pb, current_pb->ring, stack_norm_phy);
	#endif

	// Load New ELF Image into current_pb
	current_pb->main_thread->context.IP = _IMM(header.e_entry);
	stduint load_slice_p = 0;
	for0(i, header.e_phnum) {
		struct ELF_PHT_t ph;
		stduint ph_offset = header.e_phoff + header.e_phentsize * i;
		loop_device.Read(ph_offset, &ph, sizeof(ph), block_buffer);
		if (ph.p_type == PT_LOAD && ph.p_memsz) {
			_CreateELF_Carry((char*)ph.p_vaddr, ph.p_memsz, &loop_device, ph.p_offset, ph.p_filesz, current_pb->paging, block_buffer);
			if (load_slice_p < numsof(current_pb->load_slices)) {
				current_pb->load_slices[load_slice_p].address = ph.p_vaddr;
				current_pb->load_slices[load_slice_p].length = ph.p_memsz;
				load_slice_p++;
			}
		}
	}
	delete[] block_buffer;

	// 8. Setup New Stack & C ABI Call Frame
	stduint argc = 0;
	stduint argv_ptr = 0;
	const stduint stack_loc_top = _IMM(current_pb->main_thread->stack_lineaddr) + current_pb->main_thread->stack_size;
	stduint new_sp = stack_loc_top;

	if (temp_stack) {
		new_sp = (new_sp - stacklen) & ~_IMM(0xFlu);
		stduint delta = new_sp - _IMM(usr_argstack);

		char** q = (char**)temp_stack;
		for (; *q != nullptr; q++, argc++) {
			*q = (char*)(_IMM(*q) + delta);
		}
		MemCopyP((void*)new_sp, current_pb->paging, temp_stack, kernel_paging, stacklen);
		argv_ptr = new_sp;
		delete[] temp_stack;
	}
	else {
		new_sp = (new_sp - sizeof(stduint)) & ~_IMM(0xFlu);
		stduint null_ptr = 0;
		MemCopyP((void*)new_sp, current_pb->paging, &null_ptr, kernel_paging, sizeof(stduint));
		argv_ptr = new_sp;
	}

	#if (_MCCA & 0xFF00) == 0x8600

	struct C_CallFrame {
		stduint dummy_return_ip;
		stduint argc;
		stduint argv;
	};
	C_CallFrame frame = { 0, argc, argv_ptr };
	// System V ABI 16-byte alignment + margin
	new_sp = new_sp & ~_IMM(0xFlu);
	new_sp = new_sp - 48;
	MemCopyP((void*)new_sp, current_pb->paging, &frame, kernel_paging, sizeof(frame));
	current_pb->main_thread->context.AX = 0;
	current_pb->main_thread->context.CX = 0;
	current_pb->main_thread->context.SP = new_sp;
	treat<uint32>(&current_pb->main_thread->context.floating_point_context[24]) = 0x1F80; // Reset FPU
	SetSegment(&current_pb->main_thread->context);
	current_pb->main_thread->unsolved_msg = nullptr;

	#elif (_MCCA & 0xFF00) == 0x1000

	new_sp = (new_sp & ~_IMM(0xFlu)) - 0x10;
	current_pb->main_thread->context.a0 = argc;
	current_pb->main_thread->context.a1 = argv_ptr;
	current_pb->main_thread->context.sp = new_sp;
	#endif
	
	using TBS = ThreadBlock::State;
	if (current_pb->main_thread->state != TBS::Ready) {
		current_pb->main_thread->state = TBS::Ready;
	}
	// DumpTask(current_pb);
	Taskman::EnqueueReady(current_pb->main_thread);
	return current_pb;
}

