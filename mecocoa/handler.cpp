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

_TEMP stduint TasksAvailableSelectors[4]{
	SegTSS, 8 * 9, 8 * 11, 8 * 13
};


extern BareConsole* BCONS0;// TTY0

void switch_task() {
	task_switch_enable = false;//{TODO} Lock
	auto pb_src = TaskGet(ProcessBlock::cpu0_task);

	++ProcessBlock::cpu0_task %= numsof(TasksAvailableSelectors);
	// ProcessBlock::cpu0_task %= 2;ProcessBlock::cpu0_task++;

	if (false) printlog(_LOG_TRACE, "switch task %d", ProcessBlock::cpu0_task);
	auto pb_des = TaskGet(ProcessBlock::cpu0_task);

	if (pb_des->state == ProcessBlock::State::Uninit)
		pb_des->state = ProcessBlock::State::Ready;
	if (pb_src->state == ProcessBlock::State::Running && (
		pb_des->state == ProcessBlock::State::Ready)) {
		pb_src->state = ProcessBlock::State::Ready;
		pb_des->state = ProcessBlock::State::Running;
	}
	else {
		plogerro("task switch error.");
	}
	task_switch_enable = true;//{TODO} Unlock
	jmpFar(0, TasksAvailableSelectors[ProcessBlock::cpu0_task]);
}


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
	if (time_slice >= 20) { // switch task
		time_slice = 0;
		if (task_switch_enable) {
			switch_task();
		}
	}
	endo:
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

OstreamTrait* kbd_out;
void Handint_KBD() // Keyboard: move to buffer and deal with global state
{
	__asm("push %eax; push %ebx; push %ecx; push %edx; push %esi; push %edi;");
	char ch = innpb(PORT_KBD_BUFFER);
	// outsfmt("[%[8H]]", ch);
	asserv(kbd_out)->OutChar(ch);
	outpb(0xA0, ' '); // slaver
	outpb(0x20, ' '); // master
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

