// ASCII C++ TAB4 LF
// LastCheck: 20240505
// AllAuthor: @dosconio
// ModuTitle: RustSBI Interface
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#ifndef _RiscV64
#define _RiscV64
#endif
#include <c/stdinc.h>
#include "rustsbi.h"

int inline sbicall(SBI_Identifiers opt, uint64 arg0, uint64 arg1, uint64 arg2)
{
	uint64 which = (uint64)opt;
	register uint64 a0 asm("a0") = arg0;
	register uint64 a1 asm("a1") = arg1;
	register uint64 a2 asm("a2") = arg2;
	register uint64 a7 asm("a7") = which;
	asm volatile("ecall"
		     : "=r"(a0)
		     : "r"(a0), "r"(a1), "r"(a2), "r"(a7)
		     : "memory");
	return a0;
}

void sysoutc(int c)
{
	sbicall(SBI_CONSOLE_PUTCHAR, c, 0, 0);
}

int sysgetc()
{
	return sbicall(SBI_CONSOLE_GETCHAR, 0, 0, 0);
}

void shutdown()
{
	sbicall(SBI_SHUTDOWN, 0, 0, 0);
}
