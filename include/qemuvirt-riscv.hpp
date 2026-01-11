#ifndef _QEMU_VIRT_RISCV_
#define _QEMU_VIRT_RISCV_
#include <c/stdinc.h>
// ---- ---- ---- ---- board-independent ---- ---- ---- ----

struct TaskContext {
	// ignore x0
	stduint ra;
	stduint sp;
	stduint gp;
	stduint tp;
	stduint t0;
	stduint t1;
	stduint t2;
	stduint s0;
	stduint s1;
	stduint a0;
	stduint a1;
	stduint a2;
	stduint a3;
	stduint a4;
	stduint a5;
	stduint a6;
	stduint a7;
	stduint s2;
	stduint s3;
	stduint s4;
	stduint s5;
	stduint s6;
	stduint s7;
	stduint s8;
	stduint s9;
	stduint s10;
	stduint s11;
	stduint t3;
	stduint t4;
	stduint t5;
	stduint t6;
};


// ---- ---- ---- ---- qemuvirt ---- ---- ---- ----
#if __BITS__==32
#include "qemuvirt-r32.def.h"
#elif __BITS__==64
#include "qemuvirt-r64.def.h"
#endif

#endif
