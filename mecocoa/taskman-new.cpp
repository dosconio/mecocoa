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
		ppb->paging.Map(_IMM(ppb->stack_lineaddr), (stack_norm), ppb->stack_size, PAGESIZE_4KB, PGPROP_present | PGPROP_writable | PGPROP_user_access);
		ppb->paging.Map(_NORMAL_RINGSTACK, _IMM(ppb->stack_levladdr), ppb->stack_size, PAGESIZE_4KB, PGPROP_present | PGPROP_writable);
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
	ppb->paging.Map(_IMM(ppb->stack_lineaddr), _IMM(stack_norm), ppb->stack_size, PAGESIZE_4KB, PGPROP_present | PGPROP_writable | PGPROP_user_access);
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
	min_available_left = chain.Append(kernel_task);
	kernel_task->state = ProcessBlock::State::Running;
	kernel_task->paging.root_level_page = kernel_paging.root_level_page;
	pcurrent[cpuid] = 0;
	//
	chain.Compare_f = ProcCmp;
	min_available_pid = 1;

	#if (_MCCA & 0xFF00) == 0x1000
	setMSCRATCH _IMM(&kernel_task->context);
	setMIE(getMIE() | _MIE_MSIE);// software interrupts
	kernel_task->context.kernel_sp = (usize)kernel_stack_top_cpu0;
	#endif
	kernel_task->priority = 12;
	kernel_task->time_slice = 4;
	kernel_task->focus_tty = vttys[0];
	Taskman::EnqueueReady(kernel_task);
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

ProcessBlock* Taskman::Create(void* entry, byte ring)
{
	auto ppb = AllocateTask();
	if (!ppb) return nullptr;
	ppb->ring = ring;
	ppb->parent_id = Task_Kernel;
	#if (_MCCA & 0xFF00) == 0x8600
	ppb->paging.root_level_page = (PageEntry *)getCR3();
	auto& new_ctx = ppb->context;
	new_ctx.IP = _IMM(entry);
	new_ctx.CR3 = getCR3();
	new_ctx.RING = ring;
	SetSegment(&ppb->context);
	treat<uint32>(&new_ctx.floating_point_context[24]) = 0x1F80;// ban all MXCSR exception
	ppb->stack_size = DEFAULT_STACK_SIZE;
	ppb->stack_lineaddr = (byte*)mempool.allocate(ppb->stack_size, 12);
	ppb->stack_levladdr = ring != RING_M ? (byte*)mempool.allocate(ppb->stack_size, 12) : ppb->stack_lineaddr;
	const stduint stack_top = _IMM(ppb->stack_lineaddr) + DEFAULT_STACK_SIZE;
	new_ctx.SP = (stack_top & ~0xFlu) - 8;

	// TSS->CR3 = _Taskman_Create_Paging(ppb, ring, _IMM(ppb->stack_lineaddr));
	// if (ring == RING_U) {
	// 	ppb->paging.Map(0x00010000, 0x00010000, 0x003F0000, PAGESIZE_4KB, PGPROP_present | PGPROP_writable | PGPROP_user_access | PGPROP_weak);
	// }

	#elif (_MCCA & 0xFF00) == 0x1000
	ppb->stack_size = DEFAULT_STACK_SIZE;
	ppb->stack_levladdr = (byte*)mempool.allocate(ppb->stack_size, PAGESIZE_4KB);
	auto& ctx = ppb->context;
	ctx.sp = (stduint)mempool.allocate(DEFAULT_STACK_SIZE, 12) + DEFAULT_STACK_SIZE - 0x10;
	ctx.IP = _IMM(entry);
	ctx.mstatus = (ring << 11) | _MSTATUS_MPIE;
	ctx.kernel_sp = _IMM(ppb->stack_levladdr) + ppb->stack_size - 0x10;

	if (ring == RING_U) {
		_TODO// Paging
	}

	#endif

	ppb->priority = (ring == RING_U) ? 3 : 0;
	ppb->time_slice = (ring == RING_U) ? 3 : 4;
	ppb->focus_tty = vttys[0];
	Append(ppb);
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

	pb->stack_size = HIGHER_STACK_SIZE;
	auto stack_norm = (byte*)mempool.allocate(pb->stack_size, PAGESIZE_4KB);
	auto stack_ring = ring != RING_M ? (byte*)mempool.allocate(pb->stack_size, PAGESIZE_4KB) : stack_norm;
	pb->stack_lineaddr = (byte*)0x1000;
	pb->stack_levladdr = stack_ring;

	// ---- CR3 Mapping ---- //
	stduint kernel_size = _TEMP 0x00400000;
	#if (_MCCA & 0xFF00) == 0x8600
	pb->context.CR3 = _Taskman_Create_Paging(pb, ring, _IMM(stack_norm));
	#elif _MCCA == 0x1032 || _MCCA == 0x1064
	pb->context.satp = _Taskman_Create_Paging(pb, ring, _IMM(stack_norm));
	#endif

	pb->context.IP = _IMM(header.e_entry);

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
	const stduint stack_loc_top = _IMM(pb->stack_lineaddr) + pb->stack_size;

	#if (_MCCA & 0xFF00) == 0x8600
	pb->context.RING = ring;
	pb->context.SP = (stack_loc_top & ~0xFlu) - 8 - 0x10;// single stack
	treat<uint32>(&pb->context.floating_point_context[24]) = 0x1F80;// ban all MXCSR exception
	SetSegment(&pb->context);

	#elif _MCCA == 0x1032 || _MCCA == 0x1064
	constexpr stduint floating_support = (1 << 13);
	pb->context.sp = (stack_loc_top & ~0xFlu) - 0x10;
	pb->context.IP = _IMM(header.e_entry);
	pb->context.mstatus = (ring << 11) | floating_support | _MSTATUS_MPIE;
	pb->context.kernel_sp = _IMM(pb->stack_levladdr) + pb->stack_size - 0x10;

	#endif

	pb->priority = (ring == RING_U) ? 4 : 0;
	pb->time_slice = (ring == RING_U) ? 3 : 4;
	pb->focus_tty = vttys[0];

	// Taskman::Append(pb);
	return pb;
	#endif
}

ProcessBlock* Taskman::CreateFork(ProcessBlock* fo, const CallgateFrame* frame) {
	ProcessBlock* pb = Taskman::AllocateTask();
	if (!pb) return 0;

	auto ring = fo->ring;
	pb->ring = ring;
	pb->parent_id = fo->getID();

	#if _MCCA == 0x8632
	// check undone and duplicate operations
	// 1. Context
	// 2. Copy segmants and stack
	// 3. FS Operation

	// ---- Stack ---- //
	pb->stack_size = fo->stack_size;
	auto stack_norm = (byte*)mempool.allocate(pb->stack_size, 12);
	auto stack_ring = ring ? (byte*)mempool.allocate(pb->stack_size, 12) : stack_norm;
	pb->stack_lineaddr = fo->stack_lineaddr;
	pb->stack_levladdr = stack_ring;

	// ---- Copy CR3 Mapping ---- //
	// - Segments Mapping with coping
	// - Kernel-Area Mapping
	// - (TODO) Heap Area Mapping
	pb->context = fo->context;
	//{TEMP} use simple method
	pb->context.CR3 = _Taskman_Create_Paging(pb, ring, _IMM(stack_norm));
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

	pb->priority = fo->priority;
	pb->time_slice = fo->time_slice;
	pb->focus_tty = fo->focus_tty;

	// ---- Stack and Gen.Regis ---- //
	const stduint stack_loc_top = _IMM(pb->stack_lineaddr) + pb->stack_size;
	const stduint stack_lev_top = _TEMP 0x1000 + pb->stack_size * 2;
	//
	pb->context.ES = SegDaR3 | ring;
	pb->context.CS = getCS(ring);
	pb->context.SS = getSS(ring);
	pb->context.DS = pb->context.ES;
	pb->context.FS = pb->context.ES;
	pb->context.GS = pb->context.ES;
	
	MemCopyP(pb->stack_lineaddr, pb->paging, fo->stack_lineaddr, fo->paging, pb->stack_size);
	MemCopyP(pb->stack_levladdr, kernel_paging, fo->stack_levladdr, kernel_paging, pb->stack_size);

	pb->context.AX = nil; // return from fork()
	pb->context.CX = frame->cx;
	pb->context.DX = frame->dx;
	pb->context.BX = frame->bx;
	pb->context.SI = frame->si;
	pb->context.DI = frame->di;
	pb->context.BP = frame->bp;
	pb->context.IP = frame->ip;
	pb->context.SP = frame->sp0;

	// ---- File ---- //
	for0a(i, pb->pfiles) if (pb->pfiles[i]) {
		pb->pfiles[i] = fo->pfiles[i];
		pb->pfiles[i]->vfile->f_inode->ref_count++;
	}

	Taskman::Append(pb);
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

