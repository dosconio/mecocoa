// ASCII C TAB4 CRLF
// Attribute: 
// LastCheck: 20240216
// AllAuthor: @dosconio
// ModuTitle: Task Manager
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include <c/stdinc.h>
#include <c/task.h>
#include <c/format/ELF.h>
#include "mecocoa.h"

word MccaAlocGDT(void);
word MccaLoadGDT(void);

//{GROWING}
// User task: Ring-3
// return: TSS of the new task
word UserTaskLoadFromELF32(pureptr_t rawdata)
{
	void *entry;
	stduint blocks_loaded =
		ELF32_LoadExecFromMemory(rawdata, &entry);
	TaskFlat_t task = {
		.LDTSelector = MccaAlocGDT(),
		.TSSSelector = MccaAlocGDT(),
		.parent = 8 * 7,
		.LDT = (descriptor_t *)memalloc(0x100),
		.TSS = (TSS_t *)memalloc(0x100),
		.ring = 3,
		.esp0 = (dword)memalloc(0x200)+0x200,
		.esp1 = (dword)memalloc(0x200)+0x200,
		.esp2 = (dword)memalloc(0x200)+0x200,
		.esp3 = (dword)memalloc(0x200)+0x200,
		.entry = (dword)entry,
	};
	TaskFlatRegister(&task, MccaGDT);
	//{TODO} PDBR
	// PagTable={UsrPart: UsrSegLimit=0x80000000 without KrnPart}
	return task.TSSSelector;
}


