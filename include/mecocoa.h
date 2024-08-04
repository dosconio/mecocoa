// ASCII C TAB4 LF
// Attribute: Bits(32)
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Flat-32 Console Text I/O Driver
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// Implement: mecocoa/drivers/conio/console.c

#ifndef _INC_MECOCOA
#define _INC_MECOCOA

#include <c/stdinc.h>
#include <c/format/ELF.h>
#include <c/task.h>

#undef memalloc

#define MccaGDT (*(descriptor_t**)0x80000506) //{TODO} Mecocoa::GDT in C++
#define MccaIDT (*(descriptor_t**)0x8000050C) //{TODO} Mecocoa::IDT in C++
#define MccaIDTable ((gate_t *)0x80000800)

#define ReadyFlag1 (*(dword*)0x80000534)
#define ReadyFlag1_MASK_SwitchTask 0x80000000

#define TasksAvailableSelectors (*(word**)0x80000538)


#ifdef _INC_CPP
extern "C" 
#endif
void hand_cycle_1s();

void *memalloc(stduint size);

#define getTaskEntry(task) (((TSS_t*)DescriptorBaseGet(&MccaGDT[task]))->EIP)

// ---- kernel for shell: multask.h ----
word UserTaskLoadFromELF32(pureptr_t rawdata);

// ---- console.h ----

// ---- interrupt.h ----




#endif
