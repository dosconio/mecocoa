// ASCII C++ TAB4 LF
// LastCheck: 20240504
// AllAuthor: @dosconio
// ModuTitle: Console for Mecocoa riscv-64
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include <c/proctrl/RISCV/riscv64.h>
#include "rustsbi.h"

void outtxt(const char* str, dword len)
{
	char ch;
	for0 (i, len)
		sysoutc(*str++);
	_crt_out_cnt += len;
}

void curset(word posi) {}

word curget(void) { return nil; }

void scrrol(word lines) {}


