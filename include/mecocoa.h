// ASCII C TAB4 LF
// Attribute: Bits(32)
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Flat-32 Console Text I/O Driver
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// Implement: mecocoa/drivers/conio/console.c

#ifndef _INC_MECOCOA
#define _INC_MECOCOA

#undef memalloc

#define MccaGDT (*(descriptor_t**)0x80000506) //{TODO} Mecocoa::GDT in C++

extern "C"
{
	void *memalloc(stduint size);
}

#endif
