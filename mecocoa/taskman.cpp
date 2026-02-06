// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST

#include <c/task.h>
#include <c/consio.h>
#include <cpp/interrupt>
#include <c/format/ELF.h>
#include <c/driver/keyboard.h>
#include "../include/taskman.hpp"

use crate uni;

static SysMessage _BUF_Message[64];
Queue<SysMessage> message_queue(_BUF_Message, numsof(_BUF_Message));


#ifdef _ARC_x86 // x86:
#include "../include/atx-x86-flap32.hpp"
#include "../include/filesys.hpp"

bool task_switch_enable = true;//{} spinlk
stduint ProcessBlock::cpu0_task;
stduint ProcessBlock::cpu0_rest;

_TEMP
ProcessBlock* pblocks[16]; stduint pnumber = 0;
stduint TaskNumber = TaskCount;

//// ---- ---- SCHEDULE ---- ---- ////

#define T_pid2tss(pid) (SegTSS0 + 16 * pid)
#define T_tss2pid(tssid) ((tssid - SegTSS0) / 16)

void switch_halt() {
	if (ProcessBlock::cpu0_task == 0) {
		return;
	}
	ProcessBlock::cpu0_rest = ProcessBlock::cpu0_task;
	auto pb_src = TaskGet(ProcessBlock::cpu0_task);
	auto pb_des = TaskGet(0);
	// stduint esp = 0;
	// _ASM("movl %%esp, %0" : "=r"(esp));
	// ploginfo("switch halt %d->0, esp: %[32H]", ProcessBlock::cpu0_task, esp);
	if (pb_des->state == ProcessBlock::State::Uninit)
		pb_des->state = ProcessBlock::State::Ready;
	if ((pb_src->state == ProcessBlock::State::Running || pb_src->state == ProcessBlock::State::Pended) && (
		pb_des->state == ProcessBlock::State::Ready)) {
		if (pb_src->state == ProcessBlock::State::Running)
			pb_src->state = ProcessBlock::State::Ready;
		pb_des->state = ProcessBlock::State::Running;
	}
	else {
		plogerro("task halt error.");
	}
	ProcessBlock::cpu0_task = 0;// kernel
	task_switch_enable = true;
	//
	//stduint save = pb_src->TSS.PDBR;
	//pb_src->TSS.PDBR = getCR3();// CR3 will not save in TSS? -- phina, 20250728
	jmpTask(SegTSS0/*, T_pid2tss(ProcessBlock::cpu0_rest)*/);
	//pb_src->TSS.PDBR = save;
}


// by only PIT
void switch_task() {
	if (ProcessBlock::cpu0_task == TaskCount) return;
	using PBS = ProcessBlock::State;

	task_switch_enable = false;//{TODO} Lock
	stduint last_proc = ProcessBlock::cpu0_task;
	auto pb_src = TaskGet(ProcessBlock::cpu0_task);

	stduint cpu0_new = ProcessBlock::cpu0_task;
	do if (ProcessBlock::cpu0_rest) {
		cpu0_new = (ProcessBlock::cpu0_rest + 1) % pnumber;
		ProcessBlock::cpu0_rest = 0;
		if (cpu0_new == 0) {
			cpu0_new++;
		}
	}
	else {
		++cpu0_new %= pnumber;
	}
	while (cpu0_new == ProcessBlock::cpu0_task || (TaskGet(cpu0_new)->state != PBS::Ready && TaskGet(cpu0_new)->state != PBS::Uninit));
	ProcessBlock::cpu0_task = cpu0_new;
	auto pb_des = TaskGet(ProcessBlock::cpu0_task);

	if (pb_des->state == PBS::Uninit) pb_des->state = PBS::Ready;
	if ((pb_src->state == PBS::Running) && (pb_des->state == PBS::Ready)) {
		pb_src->state = PBS::Ready;
		pb_des->state = PBS::Running;
	}
	else {
		printlog(_LOG_FATAL, "task switch error (PID%u, State%u, PCnt%u).", pb_des->getID(), _IMM(pb_des->state), pnumber);
	}
	task_switch_enable = true;//{TODO} Unlock
	jmpTask(T_pid2tss(ProcessBlock::cpu0_task));
}

//// ---- ---- x86 ITEM ---- ---- ////


// LocaleDescriptor32SetFromELF32
// 0
// 1 code: 00000000\~FFFFFFFF
// 2 data: 00000000\~FFFFFFFF
// 3
// 4 ss-0: 00000000\~FFFFFFFF
// 5 ss-1: 00000000\~FFFFFFFF
// 6 ss-2: 00000000\~FFFFFFFF
// 7 ss-3: 00000000\~7FFFFFFF
#define FLAT_CODE_R3_PROP 0x00CFFA00
#define FLAT_DATA_R3_PROP 0x00CFF200
#define FLAT_DATA_R2_PROP 0x00CFD200
#define FLAT_DATA_R1_PROP 0x00CFB200
#define FLAT_DATA_R0_PROP 0x00CF9200
/*
* 0x0000 Local Descriptor Table
* 0x0100 Process Control Block
* 0x1000 Stack R0
* 0x1400 Stack R1
* 0x1800 Stack R2
* 0x1C00 Stack R3
*/

static ProcessBlock krnl_tss_cpu0;
void Taskman::Initialize(stduint cpuid) {
	if (cpuid == 0) {
		new (&krnl_tss_cpu0) ProcessBlock;
		krnl_tss_cpu0.TSS.CR3 = getCR3();
		krnl_tss_cpu0.TSS.LDTDptr = 0;
		krnl_tss_cpu0.TSS.STRC_15_T = 0;
		krnl_tss_cpu0.TSS.IO_MAP = sizeof(TSS_t) - 1;
		krnl_tss_cpu0.focus_tty_id = 0;
		krnl_tss_cpu0.state = ProcessBlock::State::Running;
		mecocoa_global->gdt_ptr->tss.setRange((dword)&krnl_tss_cpu0.TSS, sizeof(TSS_t) - 1);
		TaskAdd(&krnl_tss_cpu0);
		loadTask(SegTSS0);
	}
}


static void make_LDT(descriptor_t* ldt_alias, byte ring) {
	ldt_alias[0]._data = 0;
	// code32
	ldt_alias[1]._data = (uint64(FLAT_CODE_R3_PROP) << 32) | 0x0000FFFF;
	ldt_alias[1].DPL = ring;
	// data
	ldt_alias[2]._data = (uint64(FLAT_DATA_R3_PROP) << 32) | 0x0000FFFF;
	ldt_alias[2].DPL = ring;
	ldt_alias[3]._data = 0;// kept for RONL
	ldt_alias[4]._data = (uint64(FLAT_DATA_R0_PROP) << 32) | 0x0000FFFF;
	ldt_alias[5]._data = (uint64(FLAT_DATA_R1_PROP) << 32) | 0x0000FFFF;
	ldt_alias[6]._data = (uint64(FLAT_DATA_R2_PROP) << 32) | 0x0000FFFF;
	ldt_alias[7]._data = (uint64(FLAT_DATA_R3_PROP) << 32) | 0x0000FFFF;
	// EXPERIENCE: Although the stack segment is not Expand-down, the ESP always decreases. The property of GDTE is just for boundary check.
	/*
	normal stack:
		ADD EAX, ECX; STACK BASE FROM HIGH END
		MOV EBX, 0xFFFFE; 4K
		MOV ECX, 00C09600H; [4KB][32][KiSys][DATA][R0][RW^/RE^]
	*/
}

//// ---- ---- REGISTER ---- ---- ////


ProcessBlock* TaskRegister(void* entry, byte ring)
{
	word parent = SegTSS0;// Kernel Task

	word LDTSelector = GDT_Alloc() / 8;
	word TSSSelector = GDT_Alloc() / 8;
	// outsfmt("LDTSel %d, TSSSel %d\n\r", LDTSelector, TSSSelector);
	char* page = (char*)Memory::physical_allocate(0x3000);
	ProcessBlock* pb = (ProcessBlock*)(page); new (pb) ProcessBlock();
	descriptor_t* LDT = (descriptor_t*)(pb->LDT);
	descriptor_t* GDT = (descriptor_t*)mecocoa_global->gdt_ptr;

	TSS_t* TSS = &pb->TSS;
	TSS->LastTSS = parent;
	TSS->NextTSS = 0;
	TSS->ESP0 = (dword)(page + 0x1000) + 0x800;
	TSS->SS0 = 8 * 4 + 4 + 0;// 4:LDT 8*4:SS0 0:Ring0 
	TSS->Padding0 = 0;
	TSS->ESP1 = (dword)(page + 0x1400) + 0x800;
	TSS->SS1 = 8 * 5 + 4 + 1;// 4:LDT 8*5:SS1 1:Ring1
	TSS->Padding1 = 0;
	TSS->ESP2 = (dword)(page + 0x1800) + 0x800;
	TSS->SS2 = 8 * 6 + 4 + 2;// 4:LDT 8*6:SS2 2:Ring2
	TSS->Padding2 = 0;

	TSS->CR3 = getCR3();
	_TEMP if (ring == 3) {
		pb->paging.Reset();
		TSS->CR3 = _IMM(pb->paging.root_level_page);
		pb->paging.MapWeak(0x00000000, 0x00000000, 0x00400000, true, _Comment(R0) true);//{TEMP}
		pb->paging.MapWeak(0x80000000, 0x00000000, 0x00080000, true, _Comment(R0) false);// 0x80 pages
	}
	else {
		pb->paging.root_level_page = (PageDirectory*)TSS->CR3;
	}

	TSS->EIP = _IMM(entry);
	TSS->EFLAGS = getFlags();
	TSS->EAX = TSS->ECX = TSS->EDX = TSS->EBX = TSS->EBP = TSS->ESI = TSS->EDI = 0;
	switch (ring)
	{
	case 0: TSS->ESP = TSS->ESP0;
		break;
	case 1: TSS->ESP = TSS->ESP1;
		break;
	case 2: TSS->ESP = TSS->ESP2;
		break;
	default: 
	case 3: TSS->ESP = (dword)(page + 0x2800) + 0x800;
		break;
	}
	//{} stack over-check - single segment

	if (false) outsfmt("TSS %d at 0x%[32H], Entry 0x%[32H]->0x%[32H], SP=0x%[32H]\n\r",
		TSSSelector, page,
		entry, pb->paging[_IMM(entry)],
		TSS->ESP);

	//{TODO} allow IOMap Version
	TSS->ES = 8*2 + 0b100 + ring;
	TSS->Padding3 = 0;
	TSS->CS = 8*1 + 0b100 + ring;
	TSS->Padding4 = 0;
	TSS->SS = 8*(4+ring) + 0b100 + ring;
	TSS->Padding5 = 0;
	TSS->DS = TSS->ES;
	TSS->Padding6 = 0;
	TSS->FS = TSS->ES;
	TSS->Padding7 = 0;
	TSS->GS = TSS->ES;
	TSS->Padding8 = 0;
	TSS->LDTDptr = (LDTSelector * 8) + 3; // LDT yo GDT
	TSS->LDTLength = 8 * 8 - 1;
	TSS->STRC_15_T = 0;
	TSS->IO_MAP = sizeof(TSS_t) - 1; // TaskFlat->IOMap? TaskFlat->IOMap-TSS: sizeof(TSS_t)-1;
	// Then register LDT in GDT
	Descriptor32Set(&GDT[LDTSelector], _IMM(LDT), TSS->LDTLength, _Dptr_LDT, 0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */	);// [0x00408200]
	Descriptor32Set(&GDT[TSSSelector], _IMM(TSS), sizeof(TSS_t)-1, _Dptr_TSS386_Available, 0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */ );// TSS [0x00408900]

	make_LDT(LDT, ring);

	TSS->EFLAGS |= 0x0200;// IF
	if (ring <= 1) TSS->EFLAGS |= _IMM1 << 12;
	TaskAdd(pb);
	return pb;
}

static void TaskLoad_Carry(char* vaddr, stduint length, char* phy_src, Paging& pg)
{
	// if page !exist, map it; write it.
	stduint compensation = _IMM(vaddr) & 0xFFF;
	stduint v_start1 = _IMM(vaddr) & ~_IMM(0xFFF);
	for0 (i, (length + compensation + 0xFFF) / 0x1000) {
		auto page = pg.IndexPage _IMM(v_start1);
		stduint phy;
		if (!page->isPresent(pg)) {
			phy = _IMM(Memory::physical_allocate(0x1000));
			// ploginfo("mapping %[32H] -> %[32H]", v_start1, phy);
			pg.Map(v_start1, phy, 0x1000, true, true);
			if (!page->getEntry(pg)->P)//(!page.isPresent(pg))
			{
				plogerro("Mapping failed %[32H] -> %[32H], entry%[32H]", v_start1, phy, page);
			}
		}
		else phy = _IMM(page->getEntry(pg)->address) << 12;
		MemCopyN((void*)(phy + compensation), phy_src, 0x1000 - compensation);
		phy_src += 0x1000 - compensation;
		v_start1 += 0x1000;
		compensation = 0;
	}
}

ProcessBlock* TaskLoad(BlockTrait* source, void* addr, byte ring)
{
	_TODO source;//{TODO} (,mem,ring) ==> (fs_fullpath, ring)
	stduint stack_size = PAGE_SIZE;
	word parent = SegTSS0;// Kernel Task
	//
	word LDTSelector = GDT_Alloc() / 8;
	word TSSSelector = GDT_Alloc() / 8;
	stduint allocsize = vaultAlignHexpow(0x1000, sizeof(ProcessBlock)) + 0x4000 _Comment(STACK);
	char* page = (char*)Memory::physical_allocate(allocsize);
	ProcessBlock* pb = (ProcessBlock*)(page); new (pb) ProcessBlock();
	descriptor_t* LDT = (descriptor_t*)(pb->LDT);
	descriptor_t* GDT = (descriptor_t*)mecocoa_global->gdt_ptr;

	TSS_t* TSS = &pb->TSS;
	TSS->LastTSS = parent;
	TSS->NextTSS = 0;

	// ---- CR3 Mapping ---- //
	// keep 0x00000000 default empty page
	pb->paging.Reset();
	TSS->CR3 = _IMM(pb->paging.root_level_page);
	pb->paging.Map(0x00001000, _IMM(page), allocsize, true, _Comment(R3) true);// PB&STACK
	stduint kernel_size = _TEMP 0x00400000;
	pb->paging.Map(0x80000000, 0x00000000, kernel_size, true, _Comment(R0) false);// should include LDT
	// [PHINA]: should include LDT in Paging if use jmp-tss
	#if 0 // may conflict
	pb->paging.MapWeak(_IMM(page), _IMM(page), allocsize, true, _Comment(R0) false);
	#endif


	Letvar(header, struct ELF_Header_t*, addr);
	TSS->EIP = 0x10C146;//_IMM(header->e_entry);
	TSS->EIP = _IMM(header->e_entry);
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
				plogwarn("[pid %u] LoadSlice Overflow", T_tss2pid(TSSSelector * 8));
			}
		}
	}
	if (false) outsfmt("TSS %d at 0x%[32H], Entry 0x%[32H]->0x%[32H], CR3=0x%[32H]\n\r",
		TSSSelector, page,
		header->e_entry, pb->paging[header->e_entry],
		TSS->CR3);

	// ---- Stack and Gen.Regis ---- //
	page = (char*)0x1000 + vaultAlignHexpow(0x1000, sizeof(ProcessBlock));
	TSS->ESP0 = (dword)(page + stack_size * 1) - 0x10;
	TSS->SS0 = 8 * 4 + 4 + 0;// 4:LDT 8*4:SS0 0:Ring0 
	TSS->Padding0 = 0;
	TSS->ESP1 = (dword)(page + stack_size * 2) - 0x10;
	TSS->SS1 = 8 * 5 + 4 + 1;// 4:LDT 8*5:SS1 1:Ring1
	TSS->Padding1 = 0;
	TSS->ESP2 = (dword)(page + stack_size * 3) - 0x10;
	TSS->SS2 = 8 * 6 + 4 + 2;// 4:LDT 8*6:SS2 2:Ring2
	TSS->Padding2 = 0;
	//
	TSS->EFLAGS = getFlags();
	TSS->EAX = TSS->ECX = TSS->EDX = TSS->EBX = TSS->EBP = TSS->ESI = TSS->EDI = 0;
	switch (ring)
	{
	case 0: TSS->ESP = TSS->ESP0;
		break;
	case 1: TSS->ESP = TSS->ESP1;
		break;
	case 2: TSS->ESP = TSS->ESP2;
		break;
	default: 
	case 3: TSS->ESP = (dword)(page + stack_size * 4) - 0x10;
		break;
	}
	//{TODO} Stack Over Check - single segment
	//{TODO} allow IOMap Version
	TSS->ES = 8*2 + 0b100 + ring;
	TSS->Padding3 = 0;
	TSS->CS = 8*1 + 0b100 + ring;
	TSS->Padding4 = 0;
	TSS->SS = 8*(4+ring) + 0b100 + ring;
	TSS->Padding5 = 0;
	TSS->DS = TSS->ES;
	TSS->Padding6 = 0;
	TSS->FS = TSS->ES;
	TSS->Padding7 = 0;
	TSS->GS = TSS->ES;
	TSS->Padding8 = 0;
	TSS->LDTDptr = (LDTSelector * 8) + 3; // LDT yo GDT
	TSS->LDTLength = 8 * 8 - 1;
	TSS->STRC_15_T = 0;
	TSS->IO_MAP = sizeof(TSS_t) - 1; // TaskFlat->IOMap? TaskFlat->IOMap-TSS: sizeof(TSS_t)-1;

	// Then register LDT in GDT, do not add 0x80000000
	Descriptor32Set(&GDT[LDTSelector], _IMM(LDT) + 0x80000000, TSS->LDTLength, _Dptr_LDT, 0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */	);// [0x00408200]
	Descriptor32Set(&GDT[TSSSelector], _IMM(TSS) + 0x80000000, sizeof(TSS_t)-1, _Dptr_TSS386_Available, 0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */ );// TSS [0x00408900]

	make_LDT(LDT, ring);

	cast<REG_FLAG_t>(TSS->EFLAGS).IF = 1;
	if (ring <= 1) TSS->EFLAGS |= _IMM1 << 12;
	TaskAdd(pb);
	return pb;
}


// return pid of child (zero if no child or failure)
static stduint task_fork(ProcessBlock* fo)
{
	// 1. Copy the PB
	// 2. Copy segmants and stack
	// 3. FS Operation
	stduint stack_size = PAGE_SIZE;
	word parent = T_pid2tss(fo->getID());
	word LDTSelector = GDT_Alloc() / 8;
	word TSSSelector = GDT_Alloc() / 8;
	stduint allocsize = vaultAlignHexpow(PAGE_SIZE, sizeof(ProcessBlock)) + stack_size * 4 _Comment(STACK);
	char* page = (char*)Memory::physical_allocate(allocsize);

	ProcessBlock* pb = (ProcessBlock*)(page); new (pb) ProcessBlock();
	descriptor_t* LDT = (descriptor_t*)(pb->LDT);
	descriptor_t* GDT = (descriptor_t*)mecocoa_global->gdt_ptr;
	Letvar(fo_code, descriptor_t*, &fo->LDT[2]);
	stduint ring = fo_code->DPL;
	TSS_t* TSS = &pb->TSS;
	*TSS = fo->TSS;
	TSS->LastTSS = parent;
	TSS->NextTSS = 0;
	TSS->LDTDptr = LDTSelector * 8 + 3;
	//
	TSS->EAX = nil; // return from fork()
	TSS->ECX = fo->before_syscall_ecx;
	TSS->EDX = fo->before_syscall_edx;
	TSS->EBX = fo->before_syscall_ebx;
	TSS->ESI = fo->before_syscall_esi;
	TSS->EDI = fo->before_syscall_edi;
	TSS->EBP = fo->before_syscall_ebp;
	TSS->EIP = fo->before_syscall_code_pointer;
	if (ring) TSS->ESP = fo->before_syscall_data_pointer;
	// ploginfo("set eip=%[32H] esp=%[32H] EBP=%[32H]", TSS->EIP, TSS->ESP, TSS->EBP);

	// ---- Copy CR3 Mapping ---- //
	// - Segments Mapping with coping
	// - Kernel-Area Mapping
	// - (TODO) Heap Area Mapping
	pb->paging.Reset();
	TSS->CR3 = _IMM(pb->paging.root_level_page);
	pb->paging.Map(0x00001000, _IMM(page), allocsize, true, _Comment(R3) true);// PB&STACK
	for0a(i, fo->load_slices) {
		if (!fo->load_slices[i].length) break;
		pb->load_slices[i] = fo->load_slices[i];
		stduint appendix = fo->load_slices[i].address & _IMM(PAGE_SIZE - 1);
		stduint pagesize = vaultAlignHexpow(PAGE_SIZE, fo->load_slices[i].length + appendix);
		stduint newaddr = (stduint)Memory::physical_allocate(pagesize);
		stduint mapsrc = fo->load_slices[i].address & ~_IMM(PAGE_SIZE - 1);
		// ploginfo("fork.map: 0x%x->0x%x(0x%x)", mapsrc, newaddr, pagesize);
		pb->paging.Map(mapsrc, newaddr, pagesize, _TEMP true, true);// Map and allocation
		// ploginfo("memcpyp: %x+%x, ., %x, ., %x", mapsrc, appendix, fo->load_slices[i].address, fo->load_slices[i].length);
		MemCopyP((char*)mapsrc + appendix, pb->paging,
			(void*)fo->load_slices[i].address, fo->paging,
			fo->load_slices[i].length);
	}
	stduint kernel_size = _TEMP 0x00400000;
	pb->paging.Map(0x80000000, 0x00000000, kernel_size, true, _Comment(R0) false);// should include LDT
	// [PHINA]: should include LDT in Paging if use jmp-tss
	#if 0 // may conflict
	pb->paging.MapWeak(_IMM(page), _IMM(page), allocsize, true, _Comment(R0) false);
	#endif

	// ---- Stack and Gen.Regis ---- //
	page = (char*)0x1000 + vaultAlignHexpow(PAGE_SIZE, sizeof(ProcessBlock));
	TSS->ESP0 = (dword)(page + stack_size * 1) - 0x10;
	MemCopyP(page, pb->paging, page, fo->paging, stack_size * 4);
	TSS->SS0 = 8 * 4 + 4 + 0;// 4:LDT 8*4:SS0 0:Ring0 
	TSS->SS1 = 8 * 5 + 4 + 1;// 4:LDT 8*5:SS1 1:Ring1
	TSS->SS2 = 8 * 6 + 4 + 2;// 4:LDT 8*6:SS2 2:Ring2
	//
	TSS->ES = 8 * 2 + 0b100 + ring;
	TSS->CS = 8*1 + 0b100 + ring;
	TSS->SS = 8*(4+ring) + 0b100 + ring;
	TSS->DS = TSS->ES;
	TSS->FS = TSS->ES;
	TSS->GS = TSS->ES;

	// Then register LDT in GDT, do not add 0x80000000
	Descriptor32Set(&GDT[LDTSelector], _IMM(LDT) + 0x80000000, TSS->LDTLength, _Dptr_LDT, 0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */	);// [0x00408200]
	Descriptor32Set(&GDT[TSSSelector], _IMM(TSS) + 0x80000000, sizeof(TSS_t)-1, _Dptr_TSS386_Available, 0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */ );// TSS [0x00408900]


	make_LDT(LDT, ring);

	// ---- File ---- //
	for0a(i, pb->pfiles) if (pb->pfiles[i]) {
		pb->pfiles[i] = fo->pfiles[i];
		pb->pfiles[i]->fd_inode->i_cnt++;
	}

	cast<REG_FLAG_t>(TSS->EFLAGS).IF = true;
	if (ring <= 1) TSS->EFLAGS |= _IMM1 << 12;

	TaskAdd(pb);
	return TSSSelector * 8;
}

/* dump

for0(i, pnumber) {
	Console.OutFormat("-- %u: (%u:%u) head %u, next %u, send_to_whom\n\r",
		i, pblocks[i]->state, pblocks[i]->block_reason,
		pblocks[i]->queue_send_queuehead, pblocks[i]->queue_send_queuenext);
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
		p->state = PBS::Hanging;
	}
	// if the proc has any child, make INIT the new parent, (or kill all the children >_<)
	for1(i, pnumber - 1) if (auto taski = TaskGet(i)) if (T_tss2pid(taski->TSS.LastTSS) == pid) {
		taski->TSS.LastTSS = T_pid2tss(Task_Init);
		if (is_waiting(TaskGet(Task_Init)) && taski->state == PBS::Hanging) {
			// cast<stduint>(pparent->block_reason) &= ~_IMM(ProcessBlock::BlockReason::BR_Waiting);
			pparent->Unblock(ProcessBlock::BlockReason::BR_Waiting);
			syssend(Task_Init, &args, sizeof(args));
			cleanup(i);
		}
	}
}

static stduint task_wait(stduint pid, stduint* usrarea_state)
{
	stduint children = 0;
	for1(i, pnumber - 1) if (auto taski = TaskGet(i)) if (T_tss2pid(taski->TSS.LastTSS) == pid) {
		children++;
		if (taski->state == ProcessBlock::State::Hanging) {
			stduint args[2] = { pid, taski->exit_status };
			cleanup(i);
			children = i;
			syssend(pid, args, sizeof(args));// return 0
			return i;
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



// ---- MANAGE ----

stduint TaskAdd(ProcessBlock* task) {
	pblocks[pnumber++] = task;
	return pnumber - 1;
}

ProcessBlock* TaskGet(stduint taskid) {
	return pblocks[taskid];
}

stduint ProcessBlock::getID()
{
	for0a(i, pblocks) if(pblocks[i] == this) return i;
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
		case TaskmanMsg::FORK: // (pid)
			// ploginfo("Taskman fork: %u", to_args[0]);
			ret = task_fork(TaskGet(to_args[0]));
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
