// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#undef _DEBUG
#define _DEBUG
#include <c/consio.h>
#include <cpp/interrupt>
#include <c/driver/keyboard.h>

use crate uni;
#ifdef _ARC_x86 // x86:
#include "../include/atx-x86-flap32.hpp"

void blink2();
void Handint_PIT()
{
	// auto push flag by intterrupt module
	// 1000Hz
	mecocoa_global->system_time.mic += 1000;// 1k us = 1ms
	if (mecocoa_global->system_time.mic >= 1000000) {
		mecocoa_global->system_time.mic -= 1000000;
		// mecocoa_global->system_time.sec++;
	}
	static unsigned time = 0;
	time++;
	if (time >= 1000) {
		time = 0;
		// mecocoa_global->system_time.sec++;//{TEMP} help RTC	
		if (!ento_gui) {
			Letvar(p, char*, 0xB8001);
			*p ^= 0x70;// make it blink
		}
		else {
			blink2();
		}
	}
	static unsigned time_slice = 0;
	time_slice++;
	if (time_slice >= 20) { // switch task
		time_slice = 0;
		if (task_switch_enable) {
			switch_task();
		}
	}
}

void blink();
void Handint_RTC()
{
	// 1Hz
	// auto push flag by interrupt module
	// OPEN NMI AFTER READ REG-C, OR ONLY INT ONCE
	outpb(IRQ_RTC, 0x0C);
	innpb(0x71);
	mecocoa_global->system_time.sec++;

	if (!ento_gui) {
		Letvar(p, char*, 0xB8003);
		*p ^= 0x70;// make it blink
	}
	else blink();
	rupt_proc(2, IRQ_RTC);
}

OstreamTrait* kbd_out;
void Handint_KBD() {
	asserv(kbd_out)->OutChar(innpb(PORT_KBD_BUFFER));
}

// void Handint_HDD()...

#endif
#ifdef _ARC_x86 // x86:

static rostr ExceptionDescription[] = {
	"#DE Divide Error",
	"#DB Step (reserved)",
	"#NMI",
	"#BP Breakpoint",
	"#OF Overflow",
	"#BR BOUND Range Exceeded",
	"#UD Invalid Opcode",
	"#NM Device Not Available",
	//
	"#DF Double Fault",
	"Cooperative Processor Segment Overrun (reserved)",
	"#TS Invalid TSS",
	"#NP Segment Not Present",
	"#SS Stack-Segment Fault",
	"#GP General Protection",
	"#PF Page Fault",
	"(Intel reserved)",
	//
	"#MF x87 FPU Floating-Point Error (reserved)",
	"#AC Alignment Check",
	"#MC Machine-Check",
	"#XF SIMD Floating-Point Exception"
};

_ESYM_C void PG_PUSH(), PG_POP();

void ERQ_Handler(sdword iden, dword para) {
	bool have_para = true;
	dword tmp;
	dword regs[6];
	__asm("mov %%eax, %0" : "=m" (regs[0]));
	__asm("mov %%ebx, %0" : "=m" (regs[1]));
	__asm("mov %%ecx, %0" : "=m" (regs[2]));
	__asm("mov %%edx, %0" : "=m" (regs[3]));
	__asm("mov %%esi, %0" : "=m" (regs[4]));
	__asm("mov %%edi, %0" : "=m" (regs[5]));
	PG_PUSH();
	__asm("mov %0, %%eax" : "=m" (regs[0]));
	__asm("mov %0, %%ebx" : "=m" (regs[1]));
	__asm("mov %0, %%ecx" : "=m" (regs[2]));
	__asm("mov %0, %%edx" : "=m" (regs[3]));
	__asm("mov %0, %%esi" : "=m" (regs[4]));
	__asm("mov %0, %%edi" : "=m" (regs[5]));
	
	// __asm("mov %cr3, %eax");
	//__asm("mov %%eax, %0" : "=m" (cr3));
	if (iden < 0)// do not have para
	{
		iden = ~iden;
		have_para = false;
	}
	if (iden >= 0x20)
		printlog(_LOG_FATAL, "#ELSE");
	switch (iden) {
	case ERQ_Invalid_Opcode:
	{
		// first #UD is for TEST
		static bool first_done = false;
		if (!first_done) {
			first_done = true;
			rostr test_page = (rostr)"\xFF\x70[Mecocoa]\xFF\x27 Exception #UD Test OK!\xFF\x07" + 0x80000000;
			if (opt_test) Console.OutFormat("%s\n\r", test_page);
		}
		else {
			printlog(_LOG_FATAL, " %s", ExceptionDescription[iden]);// no-para
		}
		break;
	}
	case ERQ_Coprocessor_Not_Available:
		// needed by jmp-TSS method
		__asm("mov %cr0, %eax");
		__asm("mov %%eax, %0" : "=m" (tmp));
		if (!(tmp & 0b1110)) {
			printlog(_LOG_FATAL, " %s", ExceptionDescription[iden]);// no-para
		}
		__asm("mov %cr0, %eax");
		__asm("and $0xFFFFFFF1, %eax");// TS(3) EM(2) MP(1)
		__asm("mov %eax, %cr0");// enable FPU/MMX/SSE
		break;
	case ERQ_Page_Fault:
		__asm("mov %cr2, %eax");
		__asm("mov %%eax, %0" : "=m" (tmp));
		printlog(_LOG_FATAL, have_para ? "%s with 0x%[32H], vaddr: 0x%[32H]" : "%s",
			ExceptionDescription[iden], para, tmp); // printlog will call halt machine
		break;
	default:
		printlog(_LOG_FATAL, have_para ? "%s with 0x%[32H]" : "%s",
			ExceptionDescription[iden], para); // printlog will call halt machine
		break;
	}
	PG_POP();
}

// No dynamic core

#endif

