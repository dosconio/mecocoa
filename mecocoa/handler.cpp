// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST

#include <c/consio.h>
#include <cpp/interrupt>
#include <c/driver/mouse.h>
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

extern KeyboardBridge kbdbridge;
QueueLimited* queue_mouse;
void Handint_KBD() {
	kbdbridge.OutChar(innpb(PORT_KEYBOARD_DAT));
}
static bool fa_mouse = false;
static byte mouse_buf[4] = { 0 };
static Size2dif mouse_acc(0, 0);
static byte last_status = 0;
static byte next_status = 0;
static stduint last_msecond = 0;
static void process_mouse(byte ch) {
	mouse_buf[mouse_buf[3]++] = ch;
	mouse_buf[3] %= 3;
	if (!mouse_buf[3]) {
		// outsfmt(" %[8H]-%[8H]-%[8H] ", mouse_buf[0], mouse_buf[1], mouse_buf[2]);
		auto next_msecond = mecocoa_global->system_time.mic;
		if (last_status != next_status ||
			absof(next_msecond - last_msecond) > 10000 && mouse_acc.x && mouse_acc.y)
		{
			// outsfmt(" %c(%d,%d) ", last_status != next_status ? '~' : ' ', mouse_acc.x, mouse_acc.y);
			if (Cursor::global_cursor) global_layman.Domove(Cursor::global_cursor, mouse_acc);//{TEMP}
			mouse_acc.x = 0;
			mouse_acc.y = 0;
			last_status = next_status;
			last_msecond = next_msecond;
		}
		else {
			mouse_acc.x += cast<char>(mouse_buf[1]);
			mouse_acc.y += -cast<char>(mouse_buf[2]);
		}
	}
	else if (mouse_buf[3] == 1) {
		MouseMessage& mm = *(MouseMessage*)mouse_buf;
		if (!mm.HIGH) mouse_buf[3] = 0;
		next_status = mouse_buf[0] & 0b111;
	}
}
void Handint_MOU() {
	byte state = innpb(PORT_KEYBOARD_CMD);
	if (state & 0x20); else return;//{} check AUX, give KBD
	byte ch = innpb(PORT_KEYBOARD_DAT);
	// if (ch != (byte)0xFA)// 0xFA is ready signal
	if (!fa_mouse && ch == (byte)0xFA) {
		fa_mouse = true;
		return;
	}
	process_mouse(ch);// asserv(queue_mouse)->OutChar(ch);
	while ((innpb(PORT_KEYBOARD_CMD) & 0x21) == 0x21)// 0x01 for OBF, 0x20 for AUX
	{
		process_mouse(innpb(PORT_KEYBOARD_DAT));
	}
	// ! XX-7F-81 problem
}

// void Handint_HDD()...

#endif





// ---- ---- ---- ---- EXCEPTION ---- ---- ---- ---- //

#if (_MCCA & 0xFF00) == 0x8600
static rostr ExceptionDescription[] = {
	"#DE Divide Error",
	"#DB Step",
	"#NMI",
	"#BP Breakpoint",
	"#OF Overflow",
	"#BR BOUND Range Exceeded",
	"#UD Invalid Opcode",
	"#NM Device Not Available",
	"#DF Double Fault",
	"#   Cooperative Processor Segment Overrun",
	"#TS Invalid TSS",
	"#NP Segment Not Present",
	"#SS Stack-Segment Fault",
	"#GP General Protection",
	"#PF Page Fault",
	"#   (Intel reserved)",
	// 0x10
	"#MF x87 FPU Floating-Point Error",
	"#AC Alignment Check",
	"#MC Machine-Check",
	"#XM SIMD Floating-Point Exception"
};
#endif


#if (_MCCA & 0xFF00) == 0x8600

_ESYM_C
void exception_handler(sdword iden, dword para) {
	const bool have_para = iden >= 0;
	if (iden < 0) iden = ~iden;

	dword tmp;
	if (iden >= 0x20)
		printlog(_LOG_FATAL, "#ELSE %x %x", iden, para);
	switch (iden) {
	case ERQ_Invalid_Opcode:// 6
	{
		// first #UD is for TEST
		static bool first_done = false;
		if (!first_done) {
			first_done = true;
			rostr test_page = (rostr)"\xFF\x70[Mecocoa]\xFF\x27 Exception #UD Test OK!\xFF\x07";
			outsfmt("%s\n\r", test_page);
		}
		else {
			printlog(_LOG_FATAL, " %s", ExceptionDescription[iden]);// no-para
		}
		break;
	}

	case ERQ_Coprocessor_Not_Available:// 7
		// needed by jmp-TSS method
		if (!(getCR0() & 0b1110)) {
			printlog(_LOG_FATAL, " %s", ExceptionDescription[iden]);// no-para
		}
		EnableSSE();
		break;

	case ERQ_Page_Fault:// 14
		printlog(_LOG_FATAL, have_para ? "%s with 0x%[32H], vaddr: 0x%[32H]" : "%s",
			ExceptionDescription[iden], para, getCR2()); // printlog will call halt machine
		break;

	default:
		printlog(_LOG_FATAL, have_para ? "%s with 0x%[32H]" : "%s",
			ExceptionDescription[iden], para); // printlog will call halt machine
		break;
	}
}

// No dynamic core

#endif

