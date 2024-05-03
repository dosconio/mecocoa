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

#undef memalloc

#define MccaGDT (*(descriptor_t**)0x80000506) //{TODO} Mecocoa::GDT in C++
#define MccaIDT (*(descriptor_t**)0x8000050C) //{TODO} Mecocoa::IDT in C++
#define MccaIDTable ((gate_t *)0x80000800)

#ifdef __cplusplus
extern "C" {
#endif
	void *memalloc(stduint size);


	// multask.h
	word UserTaskLoadFromELF32(pureptr_t rawdata);
#ifdef __cplusplus
}
#endif

#define getTaskEntry(task) (((TSS_t*)DescriptorBaseGet(&MccaGDT[task]))->EIP)

#endif
