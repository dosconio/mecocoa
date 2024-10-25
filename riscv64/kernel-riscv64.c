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


void init()
{
	_pref_pani = "[PANIC !]";
	_pref_erro = "[ERROR  ]";
	_pref_warn = "[WARN   ]";
	_pref_info = "[INFORMA]";
	_pref_dbug = "[DEBUG  ]";
	_pref_trac = "[TRACE  ]";


	// clear bss
	for (char* p = s_bss; p < e_bss; ++p)
		*p = 0;
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
	log_info("start scheduler!", 0);
	scheduler();
}

