// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <c/format/ELF.h>

#include "../include/filesys.hpp"

void* higher_stacks[PCU_CORES_MAX];// when 1 core 1 stack

struct AuxVector {
	stduint type;
	stduint value;
};

static stduint _Taskman_Setup_Stack(ProcessBlock* pb, ProcessBlock* parent, char** usr_argv, char** usr_envp, stduint entry, stduint phdr, stduint phnum, stduint phent) {
	stduint argc = 0, envc = 0;
	stduint str_len = 0;
	stduint* argv_ptrs = nullptr, * envp_ptrs = nullptr;

	auto count_and_size = [&](char** usr_ptr, stduint& count) {
		if (!usr_ptr) return;
		while (true) {
			stduint ptr;
			MemCopyP(&ptr, kernel_paging, (void*)((stduint)usr_ptr + count * sizeof(stduint)), parent->paging, sizeof(stduint));
			if (!ptr) break;
			char buf[256];
			stduint len = StrCopyP(buf, kernel_paging, (rostr)ptr, parent->paging, sizeof(buf));
			str_len += len + 1;
			count++;
		}
	};

	// Check if we should inject default environment variables for non-ring0 user space task
	bool use_default_env = false;
	if (pb->ring == RING_U) {
		stduint temp_envc = 0;
		if (usr_envp) {
			stduint ptr = 0;
			MemCopyP(&ptr, kernel_paging, usr_envp, parent->paging, sizeof(stduint));
			if (ptr) {
				temp_envc = 1;
			}
		}
		if (temp_envc == 0) {
			use_default_env = true;
		}
	}

	count_and_size(usr_argv, argc);
	if (use_default_env) {
		envc = 3;
		str_len += StrLength("?=0") + 1 + StrLength("PATH=/md0:/mnt34/apps") + 1 + StrLength("USER=root") + 1;
	} else {
		count_and_size(usr_envp, envc);
	}

	constexpr stduint auxc = 6; // PHDR, PHENT, PHNUM, PAGESZ, ENTRY, NULL
	stduint ptr_count = 1 + (argc + 1) + (envc + 1) + (auxc * 2); // argc, argv[], envp[], auxv[]
	stduint total_len = ptr_count * sizeof(stduint) + str_len;
	total_len = (total_len + 15) & ~15; // Align

	byte* temp_buf = new byte[total_len];
	MemSet(temp_buf, 0, total_len);

	stduint* ptr_area = (stduint*)temp_buf;
	char* str_area = (char*)(temp_buf + ptr_count * sizeof(stduint));

	stduint new_sp = (stduint)pb->main_thread->stack_lineaddr + pb->main_thread->stack_size - total_len;
	new_sp &= ~0xFlu;
	stduint delta = new_sp;

	*ptr_area++ = argc;
	auto copy_strings = [&](char** usr_ptr, stduint count) {
		for (stduint i = 0; i < count; i++) {
			stduint ptr;
			MemCopyP(&ptr, kernel_paging, (void*)((stduint)usr_ptr + i * sizeof(stduint)), parent->paging, sizeof(stduint));
			char* dest = str_area;
			stduint len = StrCopyP(dest, kernel_paging, (rostr)ptr, parent->paging, 0x1000); // assume max 4k
			*ptr_area++ = delta + (stduint)((byte*)dest - temp_buf);
			str_area += len + 1;
		}
		*ptr_area++ = 0; // NULL terminator
	};

	copy_strings(usr_argv, argc);
	if (use_default_env) {
		const char* defaults[] = { "?=0", "PATH=/md0:/mnt34/apps", "USER=root" };
		for (stduint i = 0; i < 3; i++) {
			char* dest = str_area;
			StrCopy(dest, defaults[i]);
			stduint len = StrLength(defaults[i]);
			*ptr_area++ = delta + (stduint)((byte*)dest - temp_buf);
			str_area += len + 1;
		}
		*ptr_area++ = 0; // NULL terminator
	} else {
		copy_strings(usr_envp, envc);
	}


	auto add_aux = [&](stduint type, stduint val) {
		*ptr_area++ = type;
		*ptr_area++ = val;
	};
	add_aux(AT_PHDR, phdr);
	add_aux(AT_PHENT, phent);
	add_aux(AT_PHNUM, phnum);
	add_aux(AT_PAGESZ, 4096);
	add_aux(AT_ENTRY, entry);
	add_aux(AT_NULL, 0);

	MemCopyP((void*)new_sp, pb->paging, temp_buf, kernel_paging, total_len);
	free(temp_buf);
	return new_sp;
}

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
		for0(cpu_i, PCU_CORES_MAX) {
			if (!Taskman::PCU_CORES_PERCORE[cpu_i]) continue;
			ppb->paging.Map(
				PERCORE_VBASE + cpu_i * PERCORE_STRIDE,
				_IMM(Taskman::PCU_CORES_PERCORE[cpu_i]),
				PERCORE_STRIDE,
				PAGESIZE_4KB,
				PGPROP_present | PGPROP_writable
			);
		}
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
		if (cpuid || PCU_CORES_PERCORE[0]) return; // already initialized
		PCU_CORES = PCU_CORES_MAX;
		for0(i, PCU_CORES) {
			auto percore = (PERCORE*)mem.allocate(sizeof(PERCORE), PAGESIZE_4KB);
			MemSet(percore, 0, sizeof(PERCORE));
			PCU_CORES_PERCORE[i] = percore;
			higher_stacks[i] = (mem.allocate(0x1000, PAGESIZE_4KB));
			#if _MCCA == 0x8664
			percore->tss.RSP0 = _NORMAL_RINGSTACK + HIGHER_STACK_SIZE - 8;// for user-app in cpu0
			#else
			percore->tss.ESP0 = _IMM(mem.allocate(0x1000));
			#endif
			//{} TEMP GDT_Alloc and tss.setRange
			if (i == 0)
			{
				mecocoa_global->gdt_ptr->tss.setRange(mglb(&PCU_CORES_PERCORE[i]->tss), sizeof(TSS_t) - 1);
				#if _MCCA == 0x8632
				const stduint stack_lev_top = _NORMAL_RINGSTACK + HIGHER_STACK_SIZE;
				PCU_CORES_PERCORE[i]->tss.ESP0 = stack_lev_top - 0x10;
				PCU_CORES_PERCORE[i]->tss.SS0 = SegData;
				PCU_CORES_PERCORE[i]->tss.ESP1 = stack_lev_top - 0x10;
				PCU_CORES_PERCORE[i]->tss.SS1 = 8 * 5 + 4 + 1;// 4:LDT 8*5:SS1 1:Ring1
				PCU_CORES_PERCORE[i]->tss.ESP2 = stack_lev_top - 0x10;
				PCU_CORES_PERCORE[i]->tss.SS2 = 8 * 6 + 4 + 2;// 4:LDT 8*6:SS2 2:Ring2
				//
				PCU_CORES_PERCORE[i]->tss.LDTDptr = SegGLDT + 3; // LDT yo GDT
				PCU_CORES_PERCORE[i]->tss.LDTLength = 8 * 8 - 1;
				PCU_CORES_PERCORE[i]->tss.STRC_15_T = 0;
				PCU_CORES_PERCORE[i]->tss.IO_MAP = sizeof(TSS_t) - 1;
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
	// min_available_thleft = thchain.Append(kernel_thread);
	kernel_task->state = ProcessBlock::State::Active;
	kernel_task->paging.root_level_page = kernel_paging.root_level_page;
	
	kernel_thread->tid = 0;
	current_thread(cpuid) = kernel_thread;
	#if _MCCA == 0x8664
	PCU_CORES_PERCORE[cpuid]->current_thread = kernel_thread;
	PCU_CORES_PERCORE[cpuid]->kernel_rsp = _NORMAL_RINGSTACK + HIGHER_STACK_SIZE;
	PCU_CORES_PERCORE[cpuid]->tss.RSP0 = PCU_CORES_PERCORE[cpuid]->kernel_rsp;
	#endif
	//
	chain.Compare_f = ProcCmp;
	min_available_pid = 1;
	min_available_tid = 1;
	kernel_task->cwd = Filesys::getRoot(); // Set kernel CWD
	kernel_task->root = Filesys::getRoot();

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
		idle_thread(cpuid) = idle_task->main_thread;
		#if _MCCA == 0x8664
		PCU_CORES_PERCORE[cpuid]->idle_thread = idle_task->main_thread;
		#endif
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
	ppb->focus_tty = nullptr;
	ppb->cwd = Filesys::getRoot(); // Default to root
	ppb->root = Filesys::getRoot();
	
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
	#if (_MCCA & 0xFF00) == 0x8600
	// Initialize FPU/SSE context to a safe state (masked exceptions)
	// Offset 0: FPU Control Word (0x037F = Mask all exceptions)
	treat<uint16>(&new_ctx.floating_point_context[0]) = 0x037F;
	// Offset 24: MXCSR (0x1F80 = Mask all SSE exceptions)
	treat<uint32>(&new_ctx.floating_point_context[24]) = 0x1F80;
	#endif
	tb->stack_size = DEFAULT_STACK_SIZE;
	tb->stack_lineaddr = (byte*)mempool.allocate(tb->stack_size, 12);
	tb->stack_levladdr = ring != RING_M ? (byte*)mempool.allocate(tb->stack_size, 12) : tb->stack_lineaddr;
	const stduint stack_top = _IMM(tb->stack_lineaddr) + DEFAULT_STACK_SIZE;
	new_ctx.SP = (stack_top & ~0xFlu) - 0x10 - sizeof(stduint);

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
		free(block_buffer);
		plogerro("%s: Invalid ELF File Magic Number", __FUNCIDEN__);
		return nullptr;
	}
	
	ProcessBlock* pb = Taskman::AllocateTask();
	pb->ring = ring;
	pb->parent_id = Task_Kernel;
	pb->focus_tty = nullptr;
	
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
	stduint max_seg_end = 0;
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
			stduint seg_end = ph.p_vaddr + ph.p_memsz;
			if (seg_end > max_seg_end) {
				max_seg_end = seg_end;
			}
		}
	}
	free(block_buffer);

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

	#if (_MCCA & 0xFF00) == 0x8600
	tb->context.RING = ring;
	tb->context.SP = (stack_loc_top & ~0xFlu) - 0x10 - sizeof(stduint); // Ensure (ESP+4) is 16-byte aligned
	SetSegment(&tb->context);
	#if (_MCCA & 0xFF00) == 0x8600
	// Initialize FPU/SSE context to a safe state (masked exceptions)
	// Offset 0: FPU Control Word (0x037F = Mask all exceptions)
	treat<uint16>(&tb->context.floating_point_context[0]) = 0x037F;
	// Offset 24: MXCSR (0x1F80 = Mask all SSE exceptions)
	treat<uint32>(&tb->context.floating_point_context[24]) = 0x1F80;
	#endif

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
	pb->cwd = fo->cwd;
	pb->root = fo->root;
	auto tb = AllocateThread();
	pb->main_thread = tb;
	pb->thread_list_head = tb;
	tb->parent_process = pb;

	#if _MCCA == 0x8632 || _MCCA == 0x8664
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
	// - Heap Area Mapping
	tb->context = target_fo_thread->context;
	//{TEMP} use simple method
	tb->context.CR3 = _Taskman_Create_Paging(pb, ring, _IMM(stack_norm));

	// Copy Heap VMAs and mapped pages
	pb->heapbtm = fo->heapbtm;
	pb->heaptop = fo->heaptop;
	pb->vmas = fo->vmas;
	for (stduint i = 0; i < pb->vmas.Count(); i++) {
		auto& vma = pb->vmas[i];
		if (vma.vm_type == VMA_FILE && vma.vfile) {
			uni::vfs_file* nvfile = (uni::vfs_file*)malc(sizeof(uni::vfs_file));
			if (nvfile) {
				*nvfile = *(vma.vfile);
				if (nvfile->f_inode) {
					nvfile->f_inode->ref_count++;
				}
				vma.vfile = nvfile;
			}
		}
	}
	for (stduint i = 0; i < fo->vmas.Count(); i++) {
		const auto& vma = fo->vmas[i];
		for (stduint addr = vma.vm_start; addr < vma.vm_end; addr += 0x1000) {
			void* parent_phy = fo->paging[addr];
			if (parent_phy != (void*)~_IMM0) {
				void* child_phy = mempool.allocate(0x1000, 12);
				MemSet((void*)mglb(child_phy), 0, 0x1000); // Zero-fill child page
				MemCopyP((void*)mglb(child_phy), kernel_paging, (const void*)mglb(parent_phy), kernel_paging, 0x1000);
				pb->paging.Map(addr, (stduint)child_phy, 0x1000, PAGESIZE_4KB, PGPROP_present | PGPROP_writable | PGPROP_user_access);
			}
		}
	}
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
	#if _MCCA == 0x8664
	tb->context.GPR[8] = frame->r8;
	tb->context.GPR[9] = frame->r9;
	tb->context.GPR[10] = frame->r10;
	tb->context.GPR[11] = frame->r11;
	tb->context.GPR[12] = frame->r12;
	tb->context.GPR[13] = frame->r13;
	tb->context.GPR[14] = frame->r14;
	tb->context.GPR[15] = frame->r15;
	#endif
	(tb->context.FLAG) |= 0x200;// IF

	// ---- File ---- //
	fo->sys_lock.Acquire();
	pb->pfiles.Clear();
	for (stduint i = 0; i < fo->pfiles.Count(); i++) {
		if (fo->pfiles[i]) {
			pb->pfiles.Append(FileDescriptor_Clone(fo->pfiles[i]));
		} else {
			pb->pfiles.Append(nullptr);
		}
	}
	fo->sys_lock.Release();

	Taskman::Append(pb);
	Taskman::AppendThread(tb);

	if (pb->focus_tty && pb->focus_tty->type) {
		auto pblock = (vtty_type_t*)pb->focus_tty->type;
		pblock->proc_group.Append(pb->pid);
	}
	return pb;

	#else
	return nullptr;
	#endif
}

//

ProcessBlock* Taskman::CreateFile(const char* path, byte ring, stduint parent, vfs_dentry* base) {
	//{} ELF
	auto label = StrIndexCharRight(path, '/');
	if (!label) label = path; else label++;
	vfs_dentry* d = Filesys::Index(path, base);
	if (d && d->d_inode && d->d_inode->i_sb && d->d_inode->i_sb->fs) {
		FileBlockBridge loop_device(d->d_inode->i_sb->fs, d->d_inode->internal_handler, d->d_inode->i_size, 512);
		if (auto task = Taskman::CreateELF(&loop_device, ring)) {
			task->parent_id = parent;
			ProcessBlock* pparent = Taskman::Locate(parent);
			if (pparent) {
				task->cwd = pparent->cwd;
				task->root = pparent->root;
			}
			printlog(_LOG_INFO, "Loaded %s from %s", label, path);
			return task;
		}
		else plogerro("%s: ELF Load Fail", label);
	}
	else plogwarn("%s: Not found at %s", label, path);
	return nullptr;
};

//

ProcessBlock* Taskman::Exec(stduint parent, rostr usr_fullpath, char** usr_argv, char** usr_envp)
{
	static char buf_fullpath[_TEMP 512];
	auto parent_pb = Locate(parent);
	StrCopyP(buf_fullpath, kernel_paging, usr_fullpath, parent_pb->paging, 512);

	auto new_pb = Taskman::CreateFile(buf_fullpath, RING_U, parent, parent_pb->cwd);
	if (!new_pb) return nullptr;

	vfs_dentry* d = Filesys::Index(buf_fullpath, parent_pb->cwd);
	if (!d) return nullptr;
	FileBlockBridge loop_device(d->d_inode->i_sb->fs, d->d_inode->internal_handler, d->d_inode->i_size, 512);
	ELF_Header_t header;
	auto block_buffer = new byte[512];
	loop_device.Read(0, &header, sizeof(header), block_buffer);

	stduint phdr_addr = 0;
	for (stduint i = 0; i < header.e_phnum; i++) {
		struct ELF_PHT_t ph;
		loop_device.Read(header.e_phoff + i * header.e_phentsize, &ph, sizeof(ph), block_buffer);
		if (ph.p_type == PT_PHDR) phdr_addr = ph.p_vaddr;
		if (ph.p_type == PT_LOAD && ph.p_offset == 0 && !phdr_addr) phdr_addr = ph.p_vaddr + header.e_phoff;
	}
	delete[] block_buffer;

	stduint new_sp = _Taskman_Setup_Stack(new_pb, parent_pb, usr_argv, usr_envp, (stduint)header.e_entry, phdr_addr, header.e_phnum, header.e_phentsize);

	#if (_MCCA & 0xFF00) == 0x8600
	new_pb->main_thread->context.SP = new_sp;
	#if (_MCCA & 0xFF00) == 0x8600
	treat<uint16>(&new_pb->main_thread->context.floating_point_context[0]) = 0x037F;
	treat<uint32>(&new_pb->main_thread->context.floating_point_context[24]) = 0x1F80;
	#endif
	SetSegment(&new_pb->main_thread->context);
	#elif (_MCCA & 0xFF00) == 0x1000
	new_pb->main_thread->context.sp = new_sp;
	#endif

	new_pb->main_thread->unsolved_msg = nullptr;
	new_pb->main_thread->block_reason = ThreadBlock::BlockReason::BR_None;

	new_pb->cwd = parent_pb->cwd;
	new_pb->root = parent_pb->root;
	new_pb->focus_tty = parent_pb->focus_tty;
	if (new_pb->focus_tty) {
		new_pb->Open("/dev/tty", O_RDWR); // stdin
		new_pb->Open("/dev/tty", O_RDWR); // stdout
		new_pb->Open("/dev/tty", O_RDWR); // stderr
	}
	Taskman::Append(new_pb);
	Taskman::AppendThread(new_pb->main_thread);
	if (new_pb->focus_tty && new_pb->focus_tty->type) {
		auto pblock = (vtty_type_t*)new_pb->focus_tty->type;
		pblock->proc_group.Append(new_pb->pid);
	}
	return new_pb;
}


ProcessBlock* Taskman::Exet(stduint parent, rostr usr_fullpath, char** usr_argv, char** usr_envp)
{
	static char buf_fullpath[512];
	auto current_pb = Locate(parent);
	StrCopyP(buf_fullpath, kernel_paging, usr_fullpath, current_pb->paging, 512);

	vfs_dentry* d = Filesys::Index(buf_fullpath, current_pb->cwd);
	if (!d) return nullptr;
	FileBlockBridge loop_device(d->d_inode->i_sb->fs, d->d_inode->internal_handler, d->d_inode->i_size, 512);
	
	auto block_buffer = new byte[512];
	ELF_Header_t header;
	loop_device.Read(0, &header, sizeof(header), block_buffer);

	stduint phdr_addr = 0;
	for (stduint i = 0; i < header.e_phnum; i++) {
		struct ELF_PHT_t ph;
		loop_device.Read(header.e_phoff + i * header.e_phentsize, &ph, sizeof(ph), block_buffer);
		if (ph.p_type == PT_PHDR) phdr_addr = ph.p_vaddr;
		if (ph.p_type == PT_LOAD && ph.p_offset == 0 && !phdr_addr) phdr_addr = ph.p_vaddr + header.e_phoff;
	}

	// 1. Prepare stack frame in temporary kernel buffer while old paging is still active
	stduint new_sp = _Taskman_Setup_Stack(current_pb, current_pb, usr_argv, usr_envp, (stduint)header.e_entry, phdr_addr, header.e_phnum, header.e_phentsize);

	// 2. Destroy Old Image
	for (stduint i = 0; i < current_pb->vmas.Count(); i++) {
		const auto& vma = current_pb->vmas[i];
		if (vma.vm_type == VMA_FILE && vma.vfile && (vma.vm_flags & PGPROP_writable)) {
			for (stduint addr = vma.vm_start; addr < vma.vm_end; addr += 0x1000) {
				void* phys_addr = current_pb->paging[addr];
				if (phys_addr != (void*)~_IMM0) {
					stduint file_offset = addr - vma.vm_start + vma.file_offset;
					if (file_offset < vma.vfile->f_inode->i_size) {
						stduint write_len = minof(_IMM(0x1000), vma.vfile->f_inode->i_size - file_offset);
						vma.vfile->f_pos = file_offset;
						Filesys::Write(vma.vfile, (const void*)mglb(phys_addr), write_len);
					}
				}
			}
		}
		for (stduint addr = vma.vm_start; addr < vma.vm_end; addr += 0x1000) {
			void* phys_addr = current_pb->paging[addr];
			if (phys_addr != (void*)~_IMM0) {
				free(phys_addr);
			}
		}
		if (vma.vm_type == VMA_FILE && vma.vfile) {
			if (vma.vfile->f_inode) {
				vma.vfile->f_inode->ref_count--;
			}
			Filesys::Close(vma.vfile);
		}
	}
	current_pb->vmas.Clear();

	for0a(i, current_pb->load_slices) {
		if (!current_pb->load_slices[i].address) break;
		if (current_pb->load_slices[i].length >= PAGE_SIZE &&
			(byte*)current_pb->paging[(current_pb->load_slices[i].address) & ~0xFFF] + PAGE_SIZE ==
			(byte*)current_pb->paging[(current_pb->load_slices[i].address + PAGE_SIZE) & ~0xFFF]) {
			free((byte*)current_pb->paging[(current_pb->load_slices[i].address) & ~0xFFF]);
		}
		else while (current_pb->load_slices[i].length) {
			free((byte*)current_pb->paging[(current_pb->load_slices[i].address) & ~0xFFF]);
			current_pb->load_slices[i].address += 0x1000;
			current_pb->load_slices[i].length -= minof(0x1000, current_pb->load_slices[i].length);
		}
		current_pb->load_slices[i].address = 0;
		current_pb->load_slices[i].length = 0;
	}

	stduint stack_norm_phy;
	if (current_pb->main_thread->stack_lineaddr == current_pb->main_thread->stack_levladdr)
		stack_norm_phy = _IMM(current_pb->main_thread->stack_levladdr);
	else
		stack_norm_phy = _IMM(current_pb->paging[_IMM(current_pb->main_thread->stack_lineaddr) & ~0xFFF]);

	current_pb->paging.~Paging();

	// 3. Setup New Paging and Load ELF
	#if (_MCCA & 0xFF00) == 0x8600
	current_pb->main_thread->context.CR3 = _Taskman_Create_Paging(current_pb, current_pb->ring, stack_norm_phy);
	#elif (_MCCA & 0xFF00) == 0x1000
	current_pb->main_thread->context.satp = _Taskman_Create_Paging(current_pb, current_pb->ring, stack_norm_phy);
	#endif

	current_pb->main_thread->context.IP = _IMM(header.e_entry);
	stduint load_slice_p = 0;
	stduint max_seg_end = 0;
	for0(i, header.e_phnum) {
		struct ELF_PHT_t ph;
		loop_device.Read(header.e_phoff + i * header.e_phentsize, &ph, sizeof(ph), block_buffer);
		if (ph.p_type == PT_LOAD && ph.p_memsz) {
			_CreateELF_Carry((char*)ph.p_vaddr, ph.p_memsz, &loop_device, ph.p_offset, ph.p_filesz, current_pb->paging, block_buffer);
			if (load_slice_p < numsof(current_pb->load_slices)) {
				current_pb->load_slices[load_slice_p].address = ph.p_vaddr;
				current_pb->load_slices[load_slice_p].length = ph.p_memsz;
				load_slice_p++;
			}
			stduint seg_end = ph.p_vaddr + ph.p_memsz;
			if (seg_end > max_seg_end) {
				max_seg_end = seg_end;
			}
		}
	}
	delete[] block_buffer;

	if (max_seg_end > 0) {
		current_pb->heapbtm = (max_seg_end + 0xFFF) & ~_IMM(0xFFF);
		current_pb->heapbtm += 0x10000;
		current_pb->heaptop = current_pb->heapbtm;
	} else {
		current_pb->heapbtm = 0x08000000;
		current_pb->heaptop = current_pb->heapbtm;
	}

	#if (_MCCA & 0xFF00) == 0x8600
	current_pb->main_thread->context.SP = new_sp;
	current_pb->main_thread->context.AX = 0;
	current_pb->main_thread->context.CX = 0;
	SetSegment(&current_pb->main_thread->context);
	#elif (_MCCA & 0xFF00) == 0x1000
	current_pb->main_thread->context.sp = new_sp;
	#endif

	current_pb->main_thread->unsolved_msg = nullptr;
	current_pb->main_thread->block_reason = ThreadBlock::BlockReason::BR_None;
	
	using TBS = ThreadBlock::State;
	if (current_pb->main_thread->state != TBS::Ready) {
		current_pb->main_thread->state = TBS::Ready;
	}
	Taskman::EnqueueReady(current_pb->main_thread);
	return current_pb;
}
