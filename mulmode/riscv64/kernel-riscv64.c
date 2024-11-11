// ASCII C++ TAB4 CRLF
// LastCheck: 20240504
// AllAuthor: @dosconio
// ModuTitle: Mecocoa riscv-64
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "logging.h"
#include "trap.h"
#include "console.h"
#include "appload-riscv64.h"
#include "process-riscv64.h"
#include "kernel-riscv64.h"
#include <c/ustring.h>

int* this_panic(void* _serious, ...);
int* befo_logging(void* v, ...);
void init()
{
	_pref_pani = "[panic !]";
	_pref_erro = "[error  ]";
	_pref_warn = "[warn   ]";
	_pref_info = "[inform ]";
	_pref_dbug = "[debug  ]";
	_pref_trac = "[trace  ]";
	_call_serious = this_panic;
	_befo_logging = befo_logging;

	//: if add this, uniLog won't work
	if (0) MemSet(s_bss, 0, e_bss - s_bss);// clear bss
	// do not use threadid here
}

void main()
{
	init();
	proc_init();
	loader_init();
	trap_init();
	timer_init();
	run_all_app();
	log_info("start scheduler~");
	scheduler();
	log_panic("unreached!!!");
}

_TEMP void* malloc(stduint n) { return (void*)(n - n); }

int* this_panic(void* _serious, ...) {
	Letvar(serious, loglevel_t, _serious);
	switch (serious)
	{
	case _LOG_PANIC:
		shutdown();
		break;
	default: break;
	}
	return NULL;
}
int* befo_logging(void* v, ...) {
	outsfmt("TID(%d) ", threadid());
	return NULL;
}

