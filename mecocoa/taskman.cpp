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
#include <c/driver/keyboard.h>
#include "../include/atx-x86-flap32.hpp"

use crate uni;
#ifdef _ARC_x86 // x86:

bool task_switch_enable = true;
stduint ProcessBlock::cpu0_task;

_TEMP
ProcessBlock* pblocks[8]; stduint pnumber = 0;

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
		pb->paging.MapWeak(0x80000000, 0x00000000, 0x00400000, true, _Comment(R0) false);// include IDT
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

	if (true) {
		dword* ldt_alias = (dword*)LDT;
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
