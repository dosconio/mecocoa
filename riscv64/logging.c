// ASCII C++ TAB4 LF
// LastCheck: 20240504
// AllAuthor: @dosconio
// ModuTitle: Logging for Mecocoa riscv-64
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#ifndef _RiscV64
#define _RiscV64
#endif
#include "logging.h"
#include "console.h"

_TEMP void* malloc(stduint n){ return (void*)(n-n); }
#undef log_panic
void log_panic(const char* fmt, ...)
{
	Letpara(paras, fmt);
	printlogx(_LOG_PANIC, fmt, paras);
	shutdown();
}

