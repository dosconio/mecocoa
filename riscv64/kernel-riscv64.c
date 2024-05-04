// ASCII C++ TAB4 CRLF
// LastCheck: 20240504
// AllAuthor: @dosconio
// ModuTitle: Mecocoa riscv-64
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#ifndef _RiscV64
#define _RiscV64
#endif
#include "log.h"
#include "console.h"

extern char s_text[];
extern char e_text[];
extern char s_rodata[];
extern char e_rodata[];
extern char s_data[];
extern char e_data[];
extern char s_bss[];
extern char e_bss[];

int threadid()
{
	return 0;
}

void main()
{
	// clear bss
	for (char* p = s_bss; p < e_bss; ++p)
		*p = 0;
	outsfmt("\n");
	outsfmt("Ciallo!\n");
	log_error("stext: %p", s_text);
	log_warn("etext: %p", e_text);
	log_info("sroda: %p", s_rodata);
	log_debug("eroda: %p", e_rodata);
	log_debug("sdata: %p", s_data);
	log_info("edata: %p", e_data);
	log_warn("sbss : %p", s_bss);
	log_error("ebss : %p", e_bss);
	log_trace("Oyasminasaiii~");
	if (1) shutdown(); else log_panic("ALL DONE");
	while (1);
}

