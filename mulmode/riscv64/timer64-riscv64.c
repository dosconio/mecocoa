// ASCII C/C++ TAB4 CRLF
// LastCheck: 20241019
// AllAuthor: @dosconio
// ModuTitle: Timer
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "kernel-riscv64.h"
#include "rustsbi.h"

uint64 get_cycle()
{
	return getTIME();
}

void timer_init()
{
	// Enable supervisor timer interrupt
	setSIE(getSIE() | _SIE_STIE);//{} use Reference class
	set_next_timer();
}

void set_next_timer()
{
	const uint64 timebase = CPU_FREQ / TICKS_PER_SEC;
	set_timer(get_cycle() + timebase);//{TEMP} SBI
}

