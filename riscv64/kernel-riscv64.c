// ASCII C++ TAB4 CRLF
// LastCheck: 20240504
// AllAuthor: @dosconio
// ModuTitle: Mecocoa riscv-64
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "logging.h"
#include "trap.h"
#include "console.h"
#include "appload-riscv64.h"
#include "kernel-riscv64.h"

int threadid()
{
	return 0;
}

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

	log_trace("Ciallo!", 0);
	log_error("stext: %p", s_text);
	log_warn("etext: %p", e_text);
	log_info("sroda: %p", s_rodata);
	log_debug("eroda: %p", e_rodata);
	log_debug("sdata: %p", s_data);
	log_info("edata: %p", e_data);
	log_warn("sbss : %p", s_bss);
	log_error("ebss : %p", e_bss);
}

void main()
{
	init();
	trap_init();
	loader_init();
	if (0) log_panic("test", 0);
	run_next_app();
	if (0) shutdown(); else log_panic("ALL DONE", 0); while (1);
}

