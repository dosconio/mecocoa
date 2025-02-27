// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _DEBUG
#include <c/consio.h>
#include <c/driver/keyboard.h>
#include "../include/atx-x86-flap32.hpp"

use crate uni;
#ifdef _ARC_x86 // x86:

Handler_t syscalls[_TEMP 1];

//{TODO} Syscall class
//{TODO} Use callgate-para (but register nor kernel-area) to pass parameters

void call_gate() { // noreturn
	__asm("push %ds; push %es; push %fs; push %gs");
	__asm("mov  $8*1, %eax");
	__asm("mov %eax, %ds; mov %eax, %es; mov %eax, %fs; mov %eax, %gs");
	const syscall_t callid = *(syscall_t*)mglb(&mecocoa_global->syscall_id);
	switch (callid) {
	case syscall_t::OUTC:
		outtxt((rostr)&mecocoa_global->syspara_0, 1);
		break;
	case syscall_t::EXIT:
		TaskReturn();
		break;
	case syscall_t::TIME:
		
		break;
	case syscall_t::TEST:
		if (mecocoa_global->syspara_0 == 'T' &&
			mecocoa_global->syspara_1 == 'E' &&
			mecocoa_global->syspara_2 == 'S' &&
			mecocoa_global->syspara_3 == 'T') {
			rostr test_msg = "\xFF\x70[Mecocoa]\xFF\x02 Syscalls Test OK!\xFF\x07";
			Console.OutFormat("%s\n\r", test_msg + 0x80000000);
		}
		break;
	default:
		printlog(_LOG_ERROR, "Bad syscall: 0x%[32H]", _IMM(callid));
		break;
	}
	__asm("pop  %gs; pop %fs; pop %es; pop %ds");
	__asm("mov  %ebp, %esp");
	__asm("pop  %ebp      ");
	__asm("jmp returnfar");
	__asm("callgate_endo:");
	loop;
}

void* call_gate_entry() {
	return (void*)call_gate;
}

void syscall(syscall_t callid, stduint paracnt, ...) {
	Letpara(args, paracnt);
	mecocoa_global->syscall_id = _IMM(callid);
	//{TODO} syspara_4
	MIN(paracnt, 4);
	stduint* paras = &mecocoa_global->syspara_0;
	for0(i, paracnt) {
		paras[i] = para_next(args, stduint);
	}
	CallFar(0, SegCall);
}

#endif
