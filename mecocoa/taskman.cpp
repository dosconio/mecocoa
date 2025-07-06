// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _DEBUG
#include <c/task.h>
#include <c/consio.h>
#include <cpp/interrupt>
#include <c/driver/keyboard.h>
#include "../include/atx-x86-flap32.hpp"

use crate uni;
#ifdef _ARC_x86 // x86:

bool task_switch_enable = true;
stduint cpu0_task;

word TaskRegister(void* entry)
{
	char* page = (char*)Memory::physical_allocate(0x1000);
	TaskFlat_t task = { 0 };
	task.LDTSelector = GDT_Alloc() / 8;
	task.TSSSelector = GDT_Alloc() / 8;
	outsfmt("LDTSel %d, TSSSel %d\n\r", task.LDTSelector, task.TSSSelector);
	task.parent = 8 * 7;
	task.LDT = (descriptor_t*)(page);// (0x100);
	task.TSS = (TSS_t*)(page + 0x100);// (0x100);
	task.ring = 3;
	task.esp0 = (dword)(page + 0x200) + 0x200;// 0x200
	task.esp1 = (dword)(page + 0x400) + 0x200;// 0x200
	task.esp2 = (dword)(page + 0x600) + 0x200;// 0x200
	task.esp3 = (dword)(page + 0x800) + 0x200;// 0x200
	// use half page
	task.entry = (dword)entry;
	descriptor_t* desc = (descriptor_t*)mecocoa_global->gdt_ptr;
	TaskFlatRegister(&task, desc);

	task.TSS->EFLAGS |= 0x0200;// IF
	task.TSS->CR3 = _TEMP _IMM(Paging::page_directory);
	//{TODO} PDBR PagTable={UsrPart: UsrSegLimit=0x80000000 without KrnPart}
	return task.TSSSelector;
}


#endif
