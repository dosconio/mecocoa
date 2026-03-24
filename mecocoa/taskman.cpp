// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <c/task.h>
#include <cpp/interrupt>
#include <c/format/ELF.h>
#include <c/driver/keyboard.h>

#include "../include/taskman.hpp"

void* higher_stacks[PCU_CORES_MAX];// when 1 core 1 stack
static SysMessage _BUF_Message[64];
Queue<SysMessage> message_queue(_BUF_Message, numsof(_BUF_Message));


#if _MCCA == 0x8632
constexpr unsigned ldt_entry_cnt = 8;
alignas(16) static descriptor_t _LDT[ldt_entry_cnt];
static void make_LDT(descriptor_t* ldt_alias, byte ring);
#endif

#if (_MCCA & 0xFF00) == 0x8600

static int ProcCmp(pureptr_t a, pureptr_t b) {
	return treat<ProcessBlock>(((Dnode*)a)->offs).pid -
		treat<ProcessBlock>(((Dnode*)b)->offs).pid;
}
void Taskman::Initialize(stduint cpuid) {
	if (cpuid || PCU_CORES_TSS[0]) return; // already initialized
	PCU_CORES = PCU_CORES_MAX;
	auto addr = (TSS_t*)mem.allocate(sizeof(TSS_t) * PCU_CORES);
	MemSet(addr, 0, sizeof(TSS_t) * PCU_CORES);
	for0(i, PCU_CORES) {
		PCU_CORES_TSS[i] = addr;
		#if _MCCA == 0x8664
		higher_stacks[i] = (mem.allocate(0x1000, PAGESIZE_4KB));
		addr->RSP0 = 0xFFFFFFFFC0001000ull + HIGHER_STACK_SIZE - 8;// for user-app in cpu0
		#else
		addr->ESP0 = _IMM(mem.allocate(0x1000));
		#endif
		addr++;
	}
	#if _MCCA == 0x8664
	//{TODO} Map more cores, cpu1 at 0x0000FFFFFFFF8000ull...
	kernel_paging.Map(0x0000FFFFFFFFF000ull, _IMM(higher_stacks[0]), 0x1000, PAGESIZE_4KB, PGPROP_present | PGPROP_writable);// High Part
	
	#elif _MCCA == 0x8632
	make_LDT(_LDT, 3);
	const auto LDTLength = sizeof(_LDT) - 1;
	descriptor_t* const GDT = (descriptor_t*)mecocoa_global->gdt_ptr;
	Descriptor32Set(&GDT[SegGLDT / 8], mglb(&_LDT), LDTLength, _Dptr_LDT, 0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */);// [0x00408200]

	#endif
	// x64 do not use LDT
	
	// Type = 10x1 + L=0 +  D/B=0 + 16B  64-bit TSS
	//             + L=1 OR D/B=1        32-bit TSS
	mecocoa_global->gdt_ptr->tss.setRange(mglb(PCU_CORES_TSS[0]), sizeof(TSS_t) - 1);
	auto kernel_task = AllocateTask();
	min_available_left = chain.Append(kernel_task);
	kernel_task->state = ProcessBlock::State::Running;
	kernel_task->paging.root_level_page = kernel_paging.root_level_page;
	Taskman::EnqueueReady(kernel_task);

	loadTask(SegTSS0);
	//
	pcurrent[cpuid] = 0;// kernel
	//
	chain.Compare_f = ProcCmp;
	min_available_pid = 1;

}

#endif

// ---- . ----

auto Taskman::AllocateTask() -> ProcessBlock* {
	// auto ppb = zalcof(ProcessBlock);
	auto ppb = (ProcessBlock*)mempool.allocate(sizeof(ProcessBlock), 4);
	MemSet(ppb, 0, sizeof(ProcessBlock));
	if (!ppb) {
		plogerro("Taskman::AllocateTask() failed");
		return nullptr;
	}
	return ppb;
}

#if _MCCA == 0x8632
#define FLAT_CODE_R3_PROP 0x00CFFA00
#define FLAT_DATA_R3_PROP 0x00CFF200
static void make_LDT(descriptor_t* ldt_alias, byte ring) {
	for0(i, 4) {
		ldt_alias[i]._data = (uint64(FLAT_CODE_R3_PROP) << 32) | 0x0000FFFF;
		ldt_alias[i].DPL = i;
		ldt_alias[i + 4]._data = (uint64(FLAT_DATA_R3_PROP) << 32) | 0x0000FFFF;
		ldt_alias[i + 4].DPL = i;
	}
	// EXPERIENCE: Although the stack segment is not Expand-down, the ESP always decreases. The property of GDTE is just for boundary check.
}

#endif

ProcessBlock* Taskman::Create(void* entry, byte ring)
{
	auto ppb = AllocateTask();
	if (!ppb) return nullptr;
	#if _MCCA == 0x8664
	auto& new_ctx = ppb->context;
	new_ctx.IP = _IMM(entry);
	new_ctx.DI = 0;// para0
	new_ctx.SI = 0;// para1
	new_ctx.CR3 = getCR3();
	new_ctx.FLAG = 0x202;
	new_ctx.CS = SegCo64;
	new_ctx.DS = SegData;
	new_ctx.ES = SegData;
	new_ctx.FS = SegData;
	new_ctx.GS = SegData;
	new_ctx.SS = SegData;
	auto stack = mempool.allocate(DEFAULT_STACK_SIZE, 12);
	const stduint stack_top = _IMM(stack) + DEFAULT_STACK_SIZE;
	new_ctx.SP = (stack_top & ~0xFlu) - 8;
	new_ctx.RING = ring;
	treat<uint32>(&new_ctx.floating_point_context[24]) = 0x1F80;// ban all MXCSR exception
	ppb->ring = ring;
	ppb->stack_size = DEFAULT_STACK_SIZE;
	ppb->stack_lineaddr = (byte*)stack _TEMP;
	#elif _MCCA == 0x8632
	//
	word parent = SegTSS0;// Kernel Task

	word TSSSelector = GDT_Alloc() / 8;//{TODO} - One Global LDT
	ppb->ring = ring;
	ppb->stack_size = 0x1000;
	ppb->stack_lineaddr = (byte*)mempool.allocate(ppb->stack_size, 12);
	ppb->stack_levladdr = ring ? (byte*)mempool.allocate(ppb->stack_size, 12) : ppb->stack_lineaddr;

	descriptor_t* LDT = (descriptor_t*)(ppb->LDT);
	descriptor_t* GDT = (descriptor_t*)mecocoa_global->gdt_ptr;

	TSS_t* TSS = &ppb->TSS;
	TSS->LastTSS = parent;
	TSS->NextTSS = 0;
	TSS->ESP0 = (dword)(ppb->stack_levladdr) + 0xFF0;
	TSS->SS0 = 8 * 4 + 4 + 0;// 4:LDT 8*4:SS0 0:Ring0 
	TSS->Padding0 = 0;
	TSS->ESP1 = (dword)(ppb->stack_levladdr) + 0xFF0;
	TSS->SS1 = 8 * 5 + 4 + 1;// 4:LDT 8*5:SS1 1:Ring1
	TSS->Padding1 = 0;
	TSS->ESP2 = (dword)(ppb->stack_levladdr) + 0xFF0;
	TSS->SS2 = 8 * 6 + 4 + 2;// 4:LDT 8*6:SS2 2:Ring2

	ppb->priority = (ring == 3) ? 4 : 0;
	ppb->time_slice = (ring == 3) ? 3 : 4;
	TSS->Padding2 = 0;

	if (ring == 3) {
		ppb->paging.Reset();
		ppb->paging.Map(0x00000000, 0x00000000, 0x00400000, PAGESIZE_4KB, PGPROP_present | PGPROP_writable | PGPROP_user_access | PGPROP_weak);//{TEMP}
		ppb->paging.Map(0x80000000, 0x00000000, 0x08000000, PAGESIZE_4KB, PGPROP_present | PGPROP_writable | PGPROP_weak);// 0x80 pages
	}
	else {
		ppb->paging.root_level_page = (PageEntry*)getCR3();
	}
	TSS->CR3 = _IMM(ppb->paging.root_level_page);

	TSS->EIP = _IMM(entry);
	TSS->EFLAGS = getFlags();
	TSS->EAX = TSS->ECX = TSS->EDX = TSS->EBX = TSS->EBP = TSS->ESI = TSS->EDI = 0;
	TSS->ESP = ring ? _IMM(ppb->stack_lineaddr + 0xFF0) : TSS->ESP0;
	//{} stack over-check - single segment

	// now not allow IOMap
	TSS->ES = 8 * 7 + 0b100 + ring;
	TSS->Padding3 = 0;
	TSS->CS = ring ? 8 * ring + 0b100 + ring : SegCo32;
	TSS->Padding4 = 0;
	TSS->SS = 8 * (4 + ring) + 0b100 + ring;
	TSS->Padding5 = 0;
	TSS->DS = TSS->ES;
	TSS->Padding6 = 0;
	TSS->FS = TSS->ES;
	TSS->Padding7 = 0;
	TSS->GS = TSS->ES;
	TSS->Padding8 = 0;
	TSS->LDTDptr = SegGLDT + ring; // LDT yo GDT
	TSS->LDTLength = 8 * 8 - 1;
	TSS->STRC_15_T = 0;
	TSS->IO_MAP = sizeof(TSS_t) - 1; // TaskFlat->IOMap? TaskFlat->IOMap-TSS: sizeof(TSS_t)-1;

	Descriptor32Set(&GDT[TSSSelector], _IMM(TSS), sizeof(TSS_t)-1, _Dptr_TSS386_Available, 0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */ );// TSS [0x00408900]


	cast<REG_FLAG_t>(TSS->EFLAGS).IF = 1;
	cast<REG_FLAG_t>(TSS->EFLAGS).IOPL = 0x1;

	if (0) outsfmt("TSS %d at 0x%[x], SP=0x%[32H]\n\r", TSSSelector, ppb->stack_lineaddr, TSS->ESP);

	#endif
	Append(ppb);
	return ppb;
}

bool Taskman::ExitCurrent(stduint code) {
	auto pid = CurrentPID();
	ploginfo("AppExit: %[u] with code 0x%[x]", pid, code);

	// Ver 1
	// _ASM("STI");
	// loop HALT();

	// Ver 2
	auto pb = Locate(pid);
	DequeueReady(pb);
	pb->Block(ProcessBlock::BlockReason::BR_Waiting);
	Schedule(true);
	//{} Add to zombie vector

	return true;
}

#ifdef _ARC_x86 // x86:
#include "../include/filesys.hpp"
bool task_switch_enable = true;//{} spinlk
#endif



#if (_MCCA & 0xFF00) == 0x8600


static void TaskLoad_Carry(char* vaddr, stduint length, char* phy_src, Paging& pg)
{
	// if page !exist, map it; write it.
	stduint compensation = _IMM(vaddr) & 0xFFF;
	stduint v_start1 = _IMM(vaddr) & ~_IMM(0xFFF);
	if (_IMM(vaddr) < 0x10000) {
		plogerro("TaskLoad_Carry");
	}
	#if _MCCA == 0x8632
	if (_IMM(vaddr) >= 0x80000000) {
		plogerro("TaskLoad_Carry");
	}
	#elif _MCCA == 0x8664
	if (_IMM(vaddr) >= 0xFFFFC0000000) {
		plogerro("TaskLoad_Carry");
	}
	#endif
	// ploginfo("TaskLoad_Carry(%[x], %[x], %[x])  pg(%[x])", vaddr, length, phy_src, pg);
	for0(i, (length + compensation + 0xFFF) / 0x1000) {
		auto page_entry = pg.getEntry(_IMM(v_start1));
		stduint phy;
		if (_IMM(page_entry) == ~_IMM0 || !page_entry->isPresent()) {
			phy = _IMM(mempool.allocate(0x1000, 12));
			// ploginfo("mapping %[x] -> %[x]", v_start1, phy);
			pg.Map(v_start1, phy, 0x1000, PAGESIZE_4KB, PGPROP_present | PGPROP_writable | PGPROP_user_access);
			page_entry = pg.getEntry(_IMM(v_start1));
			if (_IMM(page_entry) == ~_IMM0 || !page_entry->isPresent())
			{
				plogerro("Mapping failed %[x] -> %[x], entry%[x]", v_start1, phy, page_entry);
			}
		}
		else phy = _IMM(page_entry->address) << 12;
		MemCopyN((void*)(phy + compensation), phy_src, 0x1000 - compensation);
		phy_src += 0x1000 - compensation;
		v_start1 += 0x1000;
		compensation = 0;
	}
}

ProcessBlock* TaskLoad(BlockTrait* source, void* addr, byte ring)
{
	_TODO source;//{TODO} (,mem,ring) ==> (fs_fullpath, ring)
	word parent = SegTSS0;// Kernel Task
	//
	ProcessBlock* pb = Taskman::AllocateTask();
	pb->ring = ring;
	
	pb->stack_size = 0x4000;
	auto stack_norm = (byte*)mempool.allocate(pb->stack_size, 12);
	auto stack_ring = ring ? (byte*)mempool.allocate(pb->stack_size, 12) : stack_norm;
	pb->stack_lineaddr = (byte*)0x1000;
	pb->stack_levladdr = stack_ring;

	
	#if _MCCA == 0x8632
	word TSSSelector = GDT_Alloc() / 8;
	descriptor_t* LDT = (descriptor_t*)(pb->LDT);
	descriptor_t* GDT = (descriptor_t*)mecocoa_global->gdt_ptr;
	TSS_t* TSS = &pb->TSS;
	TSS->LastTSS = parent;
	TSS->NextTSS = 0;
	#endif

	// ---- CR3 Mapping ---- //
	// [PHINA]: should include LDT in Paging if use jmp-tss: pb->paging.Map(_IMM(page), _IMM(page), allocsize, PAGESIZE_4KB, PGPROP_present | PGPROP_writable);
	// keep 0x00000000 default empty page
	pb->paging.Reset();
	stduint kernel_size = _TEMP 0x00400000;
	#if _MCCA == 0x8632
	TSS->CR3 = _IMM(pb->paging.root_level_page);
	pb->paging.Map(_IMM(pb->stack_lineaddr), _IMM(stack_norm), pb->stack_size, PAGESIZE_4KB, PGPROP_present | PGPROP_writable | PGPROP_user_access);
	pb->paging.Map(0x1000 + 0x4000, _IMM(pb->stack_levladdr), pb->stack_size, PAGESIZE_4KB, PGPROP_present | PGPROP_writable);// !!!  TEMP !!!!
	// pb->paging.Map(0xFFF..., _IMM(pb->stack_levladdr), pb->stack_size, PAGESIZE_4KB, PGPROP_present | PGPROP_writable);
	pb->paging.Map(0x80000000, 0x00000000, 0x04000000, PAGESIZE_4KB, PGPROP_present | PGPROP_writable);// should include LDT
	// pb->paging.Map(0x80000000, 0x00000000, kernel_size, true, _Comment(R0) false);// should include LDT
	#else
	pb->context.CR3 = _IMM(pb->paging.root_level_page);
	pb->paging.Map(0x00001000, _IMM(stack_norm), pb->stack_size, PAGESIZE_4KB, PGPROP_present | PGPROP_writable | PGPROP_user_access);

	pb->paging.Map(0x0000FFFFC0000000ull,
		0x0000000000000000ull,
		0x40000000ull - _IMM1S(PAGESIZE_2MB),
		PAGESIZE_2MB, PGPROP_present | PGPROP_writable
	);// High Part
	//{TODO} Map more cores, cpu1 at 0x0000FFFFFFFF8000ull...
	pb->stack_levladdr = (byte*)mempool.allocate(HIGHER_STACK_SIZE, 12);
	pb->paging.Map(0xFFFFFFFFC0001000ull, _IMM(pb->stack_levladdr), // _IMM(higher_stacks[0]),
		HIGHER_STACK_SIZE, PAGESIZE_4KB, PGPROP_present | PGPROP_writable
	);// High Part. overlap with kernel 0..4KB
	pb->paging.Map(0x0000FFFFFFFFF000ull, _IMM(higher_stacks[0]), 0x1000, PAGESIZE_4KB, PGPROP_present | PGPROP_writable);// High Part
	#endif

	Letvar(header, struct ELF_Header_t*, addr);
	#if _MCCA == 0x8632
	TSS->EIP = _IMM(header->e_entry);
	#else
	pb->context.IP = _IMM(header->e_entry);
	#endif
	stduint load_slice_p = 0;
	for0(i, header->e_phnum) {
		Letvar(ph, struct ELF_PHT_t*, (byte*)addr + header->e_phoff + header->e_phentsize * i);
		if (ph->p_type == PT_LOAD && ph->p_memsz)//{TEMP}VAddress
		{
			//{TODO} RW of ph->p_flags;
			TaskLoad_Carry((char*)ph->p_vaddr, ph->p_memsz, (char*)addr + ph->p_offset, pb->paging);
			if (load_slice_p < numsof(pb->load_slices)) {
				pb->load_slices[load_slice_p].address = ph->p_vaddr;
				pb->load_slices[load_slice_p].length = ph->p_memsz;
				load_slice_p++;
			}
			else {
				plogwarn("[] TaskLoad LoadSlice Overflow");
			}
		}
	}
	#if _MCCA == 0x8632
	if (false) outsfmt("TSS %d, Entry 0x%[32H]->0x%[32H], CR3=0x%[32H]\n\r",
		TSSSelector,
		header->e_entry, pb->paging.getEntry(header->e_entry) ? _IMM(pb->paging.getEntry(header->e_entry)->address) << 12 : 0,
		TSS->CR3);
	#endif
	
	// ---- Stack and Gen.Regis ---- //
	//{TEMP} ring1~3: use self paging and 0x1000
	const stduint stack_loc_top = _IMM(pb->stack_lineaddr) + pb->stack_size;
	const stduint stack_lev_top = _TEMP 0x1000 + pb->stack_size * 2;
	#if _MCCA == 0x8632
	TSS->ESP0 = stack_lev_top - 0x10;
	TSS->SS0 = 8 * 4 + 4 + 0;// 4:LDT 8*4:SS0 0:Ring0 
	TSS->Padding0 = 0;
	TSS->ESP1 = stack_lev_top - 0x10;
	TSS->SS1 = 8 * 5 + 4 + 1;// 4:LDT 8*5:SS1 1:Ring1
	TSS->Padding1 = 0;
	TSS->ESP2 = stack_lev_top - 0x10;
	TSS->SS2 = 8 * 6 + 4 + 2;// 4:LDT 8*6:SS2 2:Ring2
	TSS->Padding2 = 0;
	TSS->EFLAGS = getFlags();
	// TSS->EAX = TSS->ECX = TSS->EDX = TSS->EBX = TSS->EBP = TSS->ESI = TSS->EDI = 0;
	#else
	pb->context.SP = (stack_loc_top & ~0xFlu) - 8;// single stack
	pb->context.SS = SegDaR3 | ring;
	pb->context.FLAG = 0x202;
	#endif
	pb->priority = (ring == 3) ? 4 : 0;
	pb->time_slice = (ring == 3) ? 3 : 4;

	#if _MCCA == 0x8632
	TSS->ESP = ring ? stack_loc_top - 0x10 : TSS->ESP0/* unchk ring0: should paging with kernel.cr3 ? */;
	#endif

	//{TODO} Stack Over Check - single segment
	//{TODO} allow IOMap Version
	#if _MCCA == 0x8632
	TSS->ES = 8 * 7 + 0b100 + ring;
	TSS->Padding3 = 0;
	TSS->CS = ring ? 8 * ring + 0b100 + ring : SegCo32;
	TSS->Padding4 = 0;
	TSS->SS = 8 * (4 + ring) + 0b100 + ring;
	TSS->Padding5 = 0;
	TSS->DS = TSS->ES;
	TSS->Padding6 = 0;
	TSS->FS = TSS->ES;
	TSS->Padding7 = 0;
	TSS->GS = TSS->ES;
	TSS->Padding8 = 0;
	TSS->LDTDptr = SegGLDT + ring; // LDT yo GDT
	TSS->LDTLength = 8 * 8 - 1;
	TSS->STRC_15_T = 0;
	TSS->IO_MAP = sizeof(TSS_t) - 1; // TaskFlat->IOMap? TaskFlat->IOMap-TSS: sizeof(TSS_t)-1;

	Descriptor32Set(&GDT[TSSSelector], _IMM(TSS) + 0x80000000, sizeof(TSS_t)-1, _Dptr_TSS386_Available, 0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */ );// TSS [0x00408900]

	cast<REG_FLAG_t>(TSS->EFLAGS).IF = 1;
	if (ring <= 1) TSS->EFLAGS |= _IMM1 << 12;
	#else
	pb->context.CS = SegCoR3 | 3;
	pb->context.DS = SegDaR3 | 3;
	pb->context.ES = SegDaR3 | 3;
	pb->context.FS = SegDaR3 | 3;
	pb->context.GS = SegDaR3 | 3;
	pb->context.RING = ring;
	treat<uint32>(&pb->context.floating_point_context[24]) = 0x1F80;// ban all MXCSR exception
	pb->stack_size = 0x4000;
	pb->stack_lineaddr = (byte*)0x1000;// stack*4
	#endif

	#if _MCCA == 0x8632
	Taskman::Append(pb);
	#endif
	return pb;
}

#endif
#ifdef _ARC_x86

// return pid of child (zero if no child or failure)
static stduint task_fork(ProcessBlock* fo, const CallgateFrame* frame)
{
	//{TODO} check undone and duplicate operations
	// 1. Copy the PB
	// 2. Copy segmants and stack
	// 3. FS Operation
	word parent = T_pid2tss(fo->getID());

	ProcessBlock* pb = Taskman::AllocateTask();

	auto ring = fo->ring;

	pb->stack_size = fo->stack_size;
	auto stack_norm = (byte*)mempool.allocate(pb->stack_size, 12);
	auto stack_ring = ring ? (byte*)mempool.allocate(pb->stack_size, 12) : stack_norm;
	pb->stack_lineaddr = (byte*)0x1000;
	pb->stack_levladdr = stack_ring;

	word TSSSelector = GDT_Alloc() / 8;//{TEMP}

	descriptor_t* GDT = (descriptor_t*)mecocoa_global->gdt_ptr;
	TSS_t* TSS = &pb->TSS;
	*TSS = fo->TSS;
	TSS->LastTSS = parent;
	TSS->NextTSS = 0;
	//
	TSS->LDTDptr = SegGLDT + ring;

	// ---- Copy CR3 Mapping ---- //
	// - Segments Mapping with coping
	// - Kernel-Area Mapping
	// - (TODO) Heap Area Mapping
	pb->paging.Reset();
	TSS->CR3 = _IMM(pb->paging.root_level_page);
	//{TEMP} use simple method
	pb->paging.Map(_IMM(pb->stack_lineaddr), _IMM(stack_norm), pb->stack_size, PAGESIZE_4KB, PGPROP_present | PGPROP_writable | PGPROP_user_access);
	pb->paging.Map(0x1000 + 0x4000, _IMM(pb->stack_levladdr), pb->stack_size, PAGESIZE_4KB, PGPROP_present | PGPROP_writable);// !!!  TEMP !!!!
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
	pb->paging.Map(0x80000000, 0x00000000, 0x8000000, PAGESIZE_4KB, PGPROP_present | PGPROP_writable);// should include LDT

	pb->priority = fo->priority;
	pb->time_slice = fo->time_slice;

	// ---- Stack and Gen.Regis ---- //
	const stduint stack_loc_top = _IMM(pb->stack_lineaddr) + pb->stack_size;
	const stduint stack_lev_top = _TEMP 0x1000 + pb->stack_size * 2;
	TSS->ESP0 = stack_lev_top - 0x10;
	TSS->ESP1 = stack_lev_top - 0x10;
	TSS->ESP2 = stack_lev_top - 0x10;
	TSS->SS0 = 8 * 4 + 4 + 0;// 4:LDT 8*4:SS0 0:Ring0 
	TSS->SS1 = 8 * 5 + 4 + 1;// 4:LDT 8*5:SS1 1:Ring1
	TSS->SS2 = 8 * 6 + 4 + 2;// 4:LDT 8*6:SS2 2:Ring2
	//
	TSS->ES = 8 * 7 + 0b100 + ring;
	TSS->CS = ring ? 8 * ring + 0b100 + ring : SegCo32;
	TSS->SS = 8 * (4 + ring) + 0b100 + ring;// ss0
	TSS->DS = TSS->ES;
	TSS->FS = TSS->ES;
	TSS->GS = TSS->ES;
	
	MemCopyP(pb->stack_lineaddr, pb->paging, fo->stack_lineaddr, fo->paging, pb->stack_size);
	MemCopyP(pb->stack_levladdr, kernel_paging, fo->stack_levladdr, kernel_paging, pb->stack_size);

	// Adapt BP (C++) and SP
	// if (TSS->EBP > 0x80000000)
	// 	TSS->EBP = fo->TSS.EBP - _IMM(fo->stack_levladdr) + _IMM(pb->stack_levladdr);
	// if (ring)
	// 	TSS->ESP = fo->TSS.ESP - _IMM(fo->stack_levladdr) + _IMM(pb->stack_levladdr);

	TSS->EAX = nil; // return from fork()
	TSS->ECX = frame->cx;
	TSS->EDX = frame->dx;
	TSS->EBX = frame->bx;
	TSS->ESI = frame->si;
	TSS->EDI = frame->di;
	TSS->EBP = frame->bp;
	TSS->EIP = frame->ip;
	TSS->ESP = frame->sp0;

	//	

	Descriptor32Set(&GDT[TSSSelector], _IMM(TSS) + 0x80000000, sizeof(TSS_t)-1, _Dptr_TSS386_Available, 0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */ );// TSS [0x00408900]

	// ---- File ---- //
	for0a(i, pb->pfiles) if (pb->pfiles[i]) {
		pb->pfiles[i] = fo->pfiles[i];
		pb->pfiles[i]->fd_inode->i_cnt++;
	}

	Taskman::Append(pb);
	return TSSSelector * 8;
}

/* dump

	auto nod = Taskman::chain.root_node;
	while (nod) {
		auto pblock = (ProcessBlock*)nod->offs;
		Console.OutFormat("-- %u: (%u:%u) head %u, next %u, send_to_whom\n\r",
			pblock->pid, pblock->state, pblock->block_reason,
			pblock->queue_send_queuehead, pblock->queue_send_queuenext);
		nod = nod->next;
	}
break;

*/


static void cleanup(stduint pid)
{
	// MESSAGE msg2parent;
	// msg2parent.type = SYSCALL_RET;
	// msg2parent.PID = proc2pid(proc);
	// msg2parent.STATUS = proc->exit_status;
	// send_recv(SEND, proc->p_parent, &msg2parent);
	// proc->p_flags = FREE_SLOT;
	Taskman::DequeueReady(TaskGet(pid));
	TaskGet(pid)->state = ProcessBlock::State::Invalid;
	TaskGet(pid)->TSS.LastTSS = 0;
}
inline static bool is_waiting(ProcessBlock* p) {
	return p->state == ProcessBlock::State::Pended &&
		(_IMM(p->block_reason) & _IMM(ProcessBlock::BlockReason::BR_Waiting));
}
static void task_exit(stduint pid, stduint status)
{
	ProcessBlock* p = TaskGet(pid);
	auto parent_pid = T_tss2pid(p->TSS.LastTSS);
	ProcessBlock* pparent = TaskGet(parent_pid);
	stduint args[2] = { pid, status };

	// : fileman
	for0(i, numsof(p->pfiles)) if (p->pfiles[i]) {
		p->pfiles[i]->fd_inode->i_cnt--;
		p->pfiles[i] = 0;
	}

	//{} free_mem(pid);

	p->exit_status = status;

	using PBS = ProcessBlock::State;
	if (is_waiting(pparent)) {
		// cast<stduint>(pparent->block_reason) &= ~_IMM(ProcessBlock::BlockReason::BR_Waiting);
		pparent->Unblock(ProcessBlock::BlockReason::BR_Waiting);
		// ploginfo("sending to parent %u", parent_pid);
		syssend(parent_pid, &args, sizeof(args));
		cleanup(pid);
	}
	else {
		Taskman::DequeueReady(p);
		p->state = PBS::Hanging;
	}
	// if the proc has any child, make INIT the new parent, (or kill all the children >_<)
	auto nod = Taskman::chain.Root();
	while (nod) {
		auto taski = (ProcessBlock*)nod->offs;
		if (taski->pid && T_tss2pid(taski->TSS.LastTSS) == pid) {
			taski->TSS.LastTSS = T_pid2tss(Task_Init);
			if (is_waiting(TaskGet(Task_Init)) && taski->state == PBS::Hanging) {
				// cast<stduint>(pparent->block_reason) &= ~_IMM(ProcessBlock::BlockReason::BR_Waiting);
				pparent->Unblock(ProcessBlock::BlockReason::BR_Waiting);
				syssend(Task_Init, &args, sizeof(args));
				cleanup(taski->pid);
			}
		}
		nod = nod->next;
	}
}

static stduint task_wait(stduint pid, stduint* usrarea_state)
{
	stduint children = 0;
	for(auto nod = Taskman::chain.Root(); nod; nod = nod->next) {
		auto taski = (ProcessBlock*)nod->offs;
		if (taski->pid && T_tss2pid(taski->TSS.LastTSS) == pid) {
			children++;
			if (taski->state == ProcessBlock::State::Hanging) {
				stduint args[2] = { pid, taski->exit_status };
				stduint child_pid = taski->pid;
				cleanup(child_pid);
				children = child_pid;
				syssend(pid, args, sizeof(args));// return 0
				return child_pid;
			}
		}
	}
	if (children) {// no child is HANGING
		ProcessBlock* pb = TaskGet(pid);
		// cast<stduint>(pb->block_reason) |= _IMM(ProcessBlock::BlockReason::BR_Waiting);
		pb->Block(ProcessBlock::BlockReason::BR_Waiting);
	}
	else { // no any child
		syssend(pid, &children, sizeof(children));// return 0
	}
	return ~_IMM0;
}

//

stduint task_exec(stduint pid, void* fullpath, void* argstack, stduint stacklen)// return nil if OK
{
	ploginfo("%s: %u %x %x %x", pid, fullpath, argstack, stacklen);
/*
	// get parameters from the message
	int name_len = mm_msg.NAME_LEN;	// length of filename
	int src = mm_msg.source;	// caller proc nr.
	assert(name_len < MAX_PATH);

	char pathname[MAX_PATH];
	phys_copy((void*)va2la(TASK_MM, pathname),
		(void*)va2la(src, mm_msg.PATHNAME),
		name_len);
	pathname[name_len] = 0;	// terminate the string

	// get the file size
	struct stat s;
	int ret = stat(pathname, &s);
	if (ret != 0) {
		printl("{MM} MM::do_exec()::stat() returns error. %s", pathname);
		return -1;
	}

	// read the file
	int fd = open(pathname, O_RDWR);
	if (fd == -1)
		return -1;
	assert(s.st_size < MMBUF_SIZE);
	read(fd, mmbuf, s.st_size);
	close(fd);

	// overwrite the current proc image with the new one
	Elf32_Ehdr* elf_hdr = (Elf32_Ehdr*)(mmbuf);
	int i;
	for (i = 0; i < elf_hdr->e_phnum; i++) {
		Elf32_Phdr* prog_hdr = (Elf32_Phdr*)(mmbuf + elf_hdr->e_phoff +
			(i * elf_hdr->e_phentsize));
		if (prog_hdr->p_type == PT_LOAD) {
			assert(prog_hdr->p_vaddr + prog_hdr->p_memsz <
				PROC_IMAGE_SIZE_DEFAULT);
			phys_copy((void*)va2la(src, (void*)prog_hdr->p_vaddr),
				(void*)va2la(TASK_MM,
					mmbuf + prog_hdr->p_offset),
				prog_hdr->p_filesz);
		}
	}

	// setup the arg stack
	int orig_stack_len = mm_msg.BUF_LEN;
	char stackcopy[PROC_ORIGIN_STACK];
	phys_copy((void*)va2la(TASK_MM, stackcopy),
		(void*)va2la(src, mm_msg.BUF),
		orig_stack_len);

	u8* orig_stack = (u8*)(PROC_IMAGE_SIZE_DEFAULT - PROC_ORIGIN_STACK);

	int delta = (int)orig_stack - (int)mm_msg.BUF;

	int argc = 0;
	if (orig_stack_len) {	// has args
		char** q = (char**)stackcopy;
		for (; *q != 0; q++, argc++)
			*q += delta;
	}

	phys_copy((void*)va2la(src, orig_stack),
		(void*)va2la(TASK_MM, stackcopy),
		orig_stack_len);

	proc_table[src].regs.ecx = argc;
	proc_table[src].regs.eax = (u32)orig_stack;

	// setup eip & esp
	proc_table[src].regs.eip = elf_hdr->e_entry; // @see _start.asm
	proc_table[src].regs.esp = PROC_IMAGE_SIZE_DEFAULT - PROC_ORIGIN_STACK;

	strcpy(proc_table[src].name, pathname);
*/
	return 0;
}



//// ---- ---- SERVICE ---- ---- ////

void _Comment(R0) serv_task_loop()
{
	stduint to_args[8];// 8*4=32 bytes
	stduint sig_type = 0, sig_src, ret;
	while (true) {
		switch ((TaskmanMsg)sig_type)
		{
		case TaskmanMsg::TEST:
			// Nothing
			break;
		case TaskmanMsg::EXIT: // (pid, state)
			ploginfo("Taskman exit: %u %u", to_args[0], to_args[1]);
			task_exit(to_args[0], to_args[1]);
			break;
		case TaskmanMsg::FORK: // (pid, cframe)
			// ploginfo("Taskman fork: %u", to_args[0]);
			ret = task_fork(TaskGet(to_args[0]), (CallgateFrame*)to_args[1]);
			syssend(sig_src, &ret, sizeof(ret));
			break;
		case TaskmanMsg::WAIT: // (pid, &usr:state) -> (child_pid)
			// ploginfo("Taskman wait: %u %x", to_args[0], to_args[1]);
			task_wait(to_args[0], (stduint*)to_args[1]);
			break;
		case TaskmanMsg::EXEC:// (pid, &usr:fullpath, &usr:argstack, stacklen) -> 0(success)
			to_args[0] = sig_src;
			ret = task_exec(to_args[0], (void*)to_args[1], (void*)to_args[2], to_args[3]);
			syssend(sig_src, &ret, sizeof(ret));
			break;

		default:
			plogerro("Bad TYPE in %s %s", __FILE__, __FUNCIDEN__);
			break;
		}
		sysrecv(ANYPROC, to_args, byteof(to_args), &sig_type, &sig_src);
	}
}


#endif
