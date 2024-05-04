// ASCII C++ TAB4 CRLF
// LastCheck: 20240504
// AllAuthor: @dosconio
// ModuTitle: Console for Mecocoa riscv-64
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#ifndef _RiscV64
#define _RiscV64
#endif
#include <c/stdinc.h>
#include "rustsbi.h"

void outtxt(const char* str, dword len)
{
	char ch;
	while (len-- && (ch = *str++))
		sysoutc(ch);
}

void curset(word posi) {}

word curget(void) { return nil; }

void scrrol(word lines) {}


