// ASCII C++ TAB4 CRLF
// LastCheck: 20240504
// AllAuthor: @dosconio
// ModuTitle: Mecocoa riscv-64
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#ifndef _MECOCOA_RISCV64
#define _MECOCOA_RISCV64

extern char s_text[];
extern char e_text[];
extern char s_rodata[];
extern char e_rodata[];
extern char s_data[];
extern char e_data[];
extern char s_bss[];
extern char e_bss[];

// timer related
#include <c/datime.h>
#define TICKS_PER_SEC 100
#define CPU_FREQ 12500000 // QEMU

uint64 get_cycle();
void timer_init();
void set_next_timer();


#endif
