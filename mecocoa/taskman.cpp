// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _DEBUG
#include <new>
#include <c/task.h>
#include <c/consio.h>
#include <cpp/interrupt>
#include <c/format/ELF.h>
#include <c/driver/keyboard.h>
#include "../include/atx-x86-flap32.hpp"

use crate uni;
#ifdef _ARC_x86 // x86:

bool task_switch_enable = true;
stduint ProcessBlock::cpu0_task;
stduint ProcessBlock::cpu0_rest;

_TEMP
ProcessBlock* pblocks[8]; stduint pnumber = 0;



_TEMP stduint TasksAvailableSelectors[4]{
	SegTSS, 8 * 9, 8 * 11, 8 * 13
};
void switch_halt() {
	if (ProcessBlock::cpu0_task == 0) {
		return;
	}
	ProcessBlock::cpu0_rest = ProcessBlock::cpu0_task;
	auto pb_src = TaskGet(ProcessBlock::cpu0_task);
	auto pb_des = TaskGet(0);
	// printlog(_LOG_TRACE, "switch halt");
	if (pb_des->state == ProcessBlock::State::Uninit)
		pb_des->state = ProcessBlock::State::Ready;
	if (pb_src->state == ProcessBlock::State::Running && (
		pb_des->state == ProcessBlock::State::Ready)) {
		pb_src->state = ProcessBlock::State::Ready;
		pb_des->state = ProcessBlock::State::Running;
	}
	else {
		plogerro("task halt error.");
	}
	ProcessBlock::cpu0_task = 0;// kernel
	task_switch_enable = true;
	//
	pb_src->TSS.PDBR = getCR3();// CR3 will not save in TSS? -- phina, 20250728
	jmpFar(0, SegTSS);
}

// by only PIT
void switch_task() {
	task_switch_enable = false;//{TODO} Lock
	auto pb_src = TaskGet(ProcessBlock::cpu0_task);

	if (ProcessBlock::cpu0_rest) {
		ProcessBlock::cpu0_task = (ProcessBlock::cpu0_rest + 1) % numsof(TasksAvailableSelectors);
		ProcessBlock::cpu0_rest = 0;
	}
	else ++ProcessBlock::cpu0_task %= numsof(TasksAvailableSelectors);
	// ProcessBlock::cpu0_task %= 2;ProcessBlock::cpu0_task++;

	// printlog(_LOG_TRACE, "switch task %d", ProcessBlock::cpu0_task);
	auto pb_des = TaskGet(ProcessBlock::cpu0_task);

	if (pb_des->state == ProcessBlock::State::Uninit)
		pb_des->state = ProcessBlock::State::Ready;
	if (pb_src->state == ProcessBlock::State::Running && (
		pb_des->state == ProcessBlock::State::Ready)) {
		pb_src->state = ProcessBlock::State::Ready;
		pb_des->state = ProcessBlock::State::Running;
	}
	else {
		plogerro("task switch error.");
	}
	task_switch_enable = true;//{TODO} Unlock
	jmpFar(0, TasksAvailableSelectors[ProcessBlock::cpu0_task]);
}

// LocaleDescriptor32SetFromELF32
// 0
// 1 code: 00000000\~7FFFFFFF
// 2 data: 00000000\~7FFFFFFF
// 3
// 4 ss-0: 00000000\~FFFFFFFF
// 5 ss-1: 00000000\~FFFFFFFF
// 6 ss-2: 00000000\~FFFFFFFF
// 7 ss-3: 00000000\~7FFFFFFF
#define FLAT_CODE_R3_PROP 0x00C7FA00
#define FLAT_DATA_R3_PROP 0x00C7F200
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


static void make_LDT(dword* ldt_alias, byte ring) {
	//{TODO} Dynamic Stack Area
	ldt_alias[0] = 0x00000000;
	ldt_alias[1] = 0x00000000;

	ldt_alias[2] = 0x0000FFFF;
	ldt_alias[3] = FLAT_CODE_R3_PROP;
	descriptor_t* code = (descriptor_t*)&ldt_alias[2];
	code->DPL = ring;

	ldt_alias[4] = 0x0000FFFF;
	ldt_alias[5] = FLAT_DATA_R3_PROP;
	descriptor_t* data = (descriptor_t*)&ldt_alias[4];
	data->DPL = ring;

	// kept for RONL
	ldt_alias[6] = 0x00000000;
	ldt_alias[7] = 0x00000000;

	ldt_alias[8] = 0x0000FFFF;
	ldt_alias[9] = FLAT_DATA_R0_PROP;
	ldt_alias[10] = 0x0000FFFF;
	ldt_alias[11] = FLAT_DATA_R1_PROP;
	ldt_alias[12] = 0x0000FFFF;
	ldt_alias[13] = FLAT_DATA_R2_PROP;
	ldt_alias[14] = 0x0000FFFF;
	ldt_alias[15] = FLAT_DATA_R3_PROP;
	// EXPERIENCE: Although the stack segment is not Expand-down, the ESP always decreases. The property of GDTE is just for boundary check.
	/*
	normal stack:
		ADD EAX, ECX; STACK BASE FROM HIGH END
		MOV EBX, 0xFFFFE; 4K
		MOV ECX, 00C09600H; [4KB][32][KiSys][DATA][R0][RW^/RE^]
	*/
}

ProcessBlock* TaskRegister(void* entry, byte ring)
{
	word parent = 8 * 7;// Kernel Task

	word LDTSelector = GDT_Alloc() / 8;
	word TSSSelector = GDT_Alloc() / 8;
	outsfmt("LDTSel %d, TSSSel %d\n\r", LDTSelector, TSSSelector);
	char* page = (char*)Memory::physical_allocate(0x2000);
	ProcessBlock* pb = (ProcessBlock*)(page); new (pb) ProcessBlock();
	descriptor_t* LDT = (descriptor_t*)(pb->LDT);
	descriptor_t* GDT = (descriptor_t*)mecocoa_global->gdt_ptr;

	TSS_t* TSS = &pb->TSS;
	TSS->LastTSS = 0;// parent;
	TSS->NextTSS = 0;
	TSS->ESP0 = (dword)(page + 0x1000) + 0x400;
	TSS->SS0 = 8 * 4 + 4 + 0;// 4:LDT 8*4:SS0 0:Ring0 
	TSS->Padding0 = 0;
	TSS->ESP1 = (dword)(page + 0x1400) + 0x400;
	TSS->SS1 = 8 * 5 + 4 + 1;// 4:LDT 8*5:SS1 1:Ring1
	TSS->Padding1 = 0;
	TSS->ESP2 = (dword)(page + 0x1800) + 0x400;
	TSS->SS2 = 8 * 6 + 4 + 2;// 4:LDT 8*6:SS2 2:Ring2
	TSS->Padding2 = 0;

	TSS->CR3 = getCR3();
	_TEMP if (ring == 3) {
		pb->paging.Reset();
		TSS->CR3 = _IMM(pb->paging.page_directory);
		pb->paging.MapWeak(0x00000000, 0x00000000, 0x00400000, true, _Comment(R0) true);//{TEMP}
		pb->paging.MapWeak(0x80000000, 0x00000000, 0x00080000, true, _Comment(R0) false);// 0x80 pages
	}

	TSS->EIP = _IMM(entry);
	TSS->EFLAGS = getEflags();
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
	case 3: TSS->ESP = (dword)(page + 0x1C00) + 0x400;
		break;
	}
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
	GlobalDescriptor32Set(&GDT[LDTSelector], _IMM(LDT), TSS->LDTLength, _Dptr_LDT, 0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */	);// [0x00408200]
	GlobalDescriptor32Set(&GDT[TSSSelector], _IMM(TSS), sizeof(TSS_t)-1, _Dptr_TSS386_Available, 0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */ );// TSS [0x00408900]

	make_LDT((dword*)LDT, ring);

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
			ploginfo("mapping %[32H] -> %[32H]", v_start1, phy);
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
	_TODO source; sizeof(Paging);
	word parent = 8 * 7;// Kernel Task

	word LDTSelector = GDT_Alloc() / 8;
	word TSSSelector = GDT_Alloc() / 8;
	stduint allocsize = 0x2000;
	char* page = (char*)Memory::physical_allocate(allocsize);
	ProcessBlock* pb = (ProcessBlock*)(page); new (pb) ProcessBlock();
	descriptor_t* LDT = (descriptor_t*)(pb->LDT);
	descriptor_t* GDT = (descriptor_t*)mecocoa_global->gdt_ptr;

	TSS_t* TSS = &pb->TSS;
	TSS->LastTSS = 0;// parent;
	TSS->NextTSS = 0;

	// CR3
	pb->paging.Reset();
	TSS->CR3 = _IMM(pb->paging.page_directory);

	// keep 0x00000000 default empty page
	pb->paging.Map(0x00001000, _IMM(page), allocsize, true, _Comment(R3) true);

	pb->paging.Map(0x80000000, 0x00000000, 0x00400000, true, _Comment(R0) false);// should include LDT

	//{WHY!!!} Found should include LDT. Do "JMP-TSS" need keep it, and its PDT?
	// pb->paging.MapWeak(0x00108000, 0x00108000, 0x00002000, true, _Comment(R0) false);
	pb->paging.MapWeak(_IMM(page), _IMM(page), allocsize, true, _Comment(R0) false);
	//{TODO} using DIY switch procedure but J-TSS
	//{ASSUME} no conflict with:


	Letvar(header, struct ELF_Header_t*, addr);
	TSS->EIP = 0x10C146;//_IMM(header->e_entry);
	TSS->EIP = _IMM(header->e_entry);
	for0(i, header->e_phnum) {
		Letvar(ph, struct ELF_PHT_t*, (byte*)addr + header->e_phoff + header->e_phentsize * i);
		if (ph->p_type == PT_LOAD)//{TEMP}VAddress
		{
			//{TODO} RW of ph->p_flags;
			TaskLoad_Carry((char*)ph->p_vaddr, ph->p_memsz, (char*)addr + ph->p_offset, pb->paging);
		}
	}
	outsfmt("TSS %d at 0x%[32H], Entry 0x%[32H]->0x%[32H], CR3=0x%[32H]\n\r",
		TSSSelector, page,
		header->e_entry, pb->paging[header->e_entry],
		TSS->CR3);

	page = (char*)0x00001000;
	
	TSS->ESP0 = (dword)(page + 0x1000) + 0x400;
	TSS->SS0 = 8 * 4 + 4 + 0;// 4:LDT 8*4:SS0 0:Ring0 
	TSS->Padding0 = 0;
	TSS->ESP1 = (dword)(page + 0x1400) + 0x400;
	TSS->SS1 = 8 * 5 + 4 + 1;// 4:LDT 8*5:SS1 1:Ring1
	TSS->Padding1 = 0;
	TSS->ESP2 = (dword)(page + 0x1800) + 0x400;
	TSS->SS2 = 8 * 6 + 4 + 2;// 4:LDT 8*6:SS2 2:Ring2
	TSS->Padding2 = 0;

	TSS->EFLAGS = getEflags();
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
	case 3: TSS->ESP = (dword)(page + 0x1C00) + 0x400 - 0x10;
		break;
	}
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
	GlobalDescriptor32Set(&GDT[LDTSelector], _IMM(LDT) + 0x80000000, TSS->LDTLength, _Dptr_LDT, 0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */	);// [0x00408200]
	GlobalDescriptor32Set(&GDT[TSSSelector], _IMM(TSS), sizeof(TSS_t)-1, _Dptr_TSS386_Available, 0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */ );// TSS [0x00408900]

	make_LDT((dword*)LDT, ring);

	TSS->EFLAGS |= 0x0200;// IF
	if (ring <= 1) TSS->EFLAGS |= _IMM1 << 12;
	TaskAdd(pb);
	return pb;
}

stduint TaskAdd(ProcessBlock* task) {
	pblocks[pnumber++] = task;
	return pnumber - 1;
}

ProcessBlock* TaskGet(stduint taskid) {
	return pblocks[taskid];
}

#endif
