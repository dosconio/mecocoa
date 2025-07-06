// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _DEBUG
#include <c/consio.h>
#include <cpp/interrupt>
#include <c/driver/keyboard.h>
#include "../include/atx-x86-flap32.hpp"

use crate uni;
#ifdef _ARC_x86 // x86:

//{TOFIX} _pushad _popad _returni
// no-pic

_TEMP stduint TasksAvailableSelectors[3]{
	SegTSS, 8 * 9, 8 * 11
};


extern BareConsole* BCONS0;// TTY0

// or make into "{callback(); iret();}"
void Handint_PIT()
{
	// auto push flag by intterrupt module
	// 1000Hz
	__asm("push %eax; push %ebx; push %ecx; push %edx; push %esi; push %edi;");
	mecocoa_global->system_time.mic += 1000;// 1k us = 1ms
	if (mecocoa_global->system_time.mic >= 1000000) {
		mecocoa_global->system_time.mic -= 1000000;
		// mecocoa_global->system_time.sec++;
	}
	static unsigned time = 0;
	time++;
	if (time >= 1000) {
		time = 0;
		Letvar(p, char*, 0xB8001);
		*p ^= 0x70;// make it blink
	}
	outpb(0x20, ' ' /*EOI*/);// master
	static unsigned time_slice = 0;
	time_slice++;
	if (time_slice >= 50) { // switch task
		time_slice = 0;
		if (task_switch_enable) {
			++cpu0_task %= numsof(TasksAvailableSelectors);
			if (false) printlog(_LOG_TRACE, "switch task %d", cpu0_task);
			jmpFar(0, TasksAvailableSelectors[cpu0_task]);
		}
	}
	__asm("pop  %edi; pop  %esi; pop  %edx; pop  %ecx; pop  %ebx; pop  %eax;");
	__asm("leave");
	__asm("iret");
}

void Handint_RTC()
{
	// 1Hz
	// auto push flag by interrupt module
	__asm("push %eax; push %ebx; push %ecx; push %edx; push %esi; push %edi;");
	static unsigned time = 0;
	mecocoa_global->system_time.sec++;
	if (1) {
		Letvar(p, char*, 0xB8003);
		*p ^= 0x70;// make it blink
	}
	outpb(0xA0, ' '); // slaver
	outpb(0x20, ' '); // master
	// OPEN NMI AFTER READ REG-C, OR ONLY INT ONCE
	outpb(0x70, 0x0C);
	innpb(0x71);
	__asm("pop  %edi; pop  %esi; pop  %edx; pop  %ecx; pop  %ebx; pop  %eax;");
	__asm("leave");
	__asm("iret");
}

_TEMP
word kbd_buf[64];// HIG:STATE, LOW:ASCII
unsigned kbd_buf_p = 0;


static keyboard_state_t kbd_state = { 0 };
void Handint_KBD() // Keyboard: move to buffer and deal with global state
{
	__asm("push %eax; push %ebx; push %ecx; push %edx; push %esi; push %edi;");
	byte loc_buf[3];
	static int pref_e0 = 0;
	outpb(0xA0, ' '); // slaver
	outpb(0x20, ' '); // master
	loc_buf[0] = innpb(PORT_KBD_BUFFER);
	if (loc_buf[0] == 0xE0)
	{
		//{} invalid for VMware
		pref_e0 = 1;
		loc_buf[1] = innpb(PORT_KBD_BUFFER);
		if (loc_buf[1] == 0x48 && BCONS0->crtline > 0) {// UP
			BCONS0->setStartLine(--BCONS0->crtline);
		}
		else if (loc_buf[1] == 0x50 && BCONS0->crtline < BCONS0->area_total.y - BCONS0->area_show.height) {// DOWN
			BCONS0->setStartLine(++BCONS0->crtline);
		}
	}
	else if (loc_buf[0] < 0x80) { // key down
		loc_buf[0] = _tab_keycode2ascii[loc_buf[0]].ascii_usual;
		if (loc_buf[0] > 1) {
			outsfmt("%c", loc_buf[0]);
		}
	}
	__asm("pop  %edi; pop  %esi; pop  %edx; pop  %ecx; pop  %ebx; pop  %eax;");
	__asm("leave");
	__asm("iret");
}

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

void ERQ_Handler(sdword iden, dword para) {
	bool have_para = true;
	if (iden < 0)// do not have para
		iden = ~iden;
	if (iden >= 0x20)
		printlog(_LOG_FATAL, "#ELSE");
	switch (iden) {
	case ERQ_Invalid_Opcode:
	{
		// first #UD is for TEST
		static bool first_done = false;
		if (!first_done) {
			first_done = true;
			rostr test_page = (rostr)"\xFF\x70[Mecocoa]\xFF\x02 Exception #UD Test OK!\xFF\x07" + 0x80000000;
			if (opt_test) Console.OutFormat("%s\n\r", test_page);
		}
		else {
			printlog(_LOG_FATAL, " %s", ExceptionDescription[iden]);// no-para
		}
		break;
	}
	default:
		printlog(_LOG_FATAL, have_para ? "%s with 0x%[32H]" : "%s",
			ExceptionDescription[iden], para); // printlog will call halt machine
		break;
	}
}

#endif

