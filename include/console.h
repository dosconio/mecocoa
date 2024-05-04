// ASCII C TAB4 LF
// Attribute: Bits(32)
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Flat-32 Console Text I/O Driver
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// Implement: mecocoa/drivers/conio/console.c

#ifndef _INC_CONSOLE
#define _INC_CONSOLE

#include <c/consio.h>

#if defined(_RiscV64)

enum CON_FORECOLOR {
	CON_FORE_RED = 31,
	CON_FORE_GREEN = 32,
	CON_FORE_BLUE = 34,
	CON_FORE_GRAY = 90,
	CON_FORE_YELLOW = 93,
};


#else

#include <c/port.h>

#define CON_DarkIoWhite "\xFF\x70"
#define CON_None "\xFF\xFF"

#endif
#endif
