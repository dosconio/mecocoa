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
static const byte syscall_paracnts[0x100] = {
	1, //OUTC
	3, //INNC
	0, //EXIT
	0, //TIME
	0,0,0,0, 0,0,0,0,0,0,0,0,// 0XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// 1XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// 2XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// 3XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// 4XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// 5XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// 6XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// 7XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,//	8XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// 9XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// AXH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// BXH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// CXH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// DXH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// EXH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,
	3, //TEST // FXH
};
static stduint call_body(const syscall_t callid, ...) {
	Letpara(paras, callid);
	stduint para[3];
	stduint ret = 0;
	if (_IMM(callid) >= numsof(syscall_paracnts)) return ~_IMM0;
	byte pcnt = syscall_paracnts[_IMM(callid)];
	for0(i, pcnt) {
		para[i] = para_next(paras, stduint);
	}
	auto task_switch_enable_old = task_switch_enable;//{MUTEX for multi-Proc}
	switch (callid) {
	case syscall_t::OUTC:
		outtxt((rostr)&para[0], 1);
		break;
	case syscall_t::EXIT:
		TaskReturn();
		break;
	case syscall_t::TIME:

		break;
	case syscall_t::TEST:
		//{TODO} ISSUE 20250706 Each time subapp (Ring3) print %d or other integer by outsfmt() will panic, but OutInteger() or Kernel Ring0 is OK.
		task_switch_enable = false;
		if (para[0] == 'T' && para[1] == 'E' && para[2] == 'S') {
			rostr test_msg = "Syscalls Test OK!";
			Console.OutFormat("\xFF\x70[Mecocoa]\xFF\x02 PID");
			Console.OutInteger(cpu0_task, +10);
			Console.OutFormat(": %s\xFF\x07\n\r", test_msg + 0x80000000);
		}
		else {
			rostr test_msg = "\xFF\x70[Mecocoa]\xFF\x04 Syscalls Test FAIL!\xFF\x07";
			Console.OutFormat("%s %x %x %x\n\r", test_msg + 0x80000000,
				para[0], para[1], para[2]);
		}
		ret = cpu0_task;
		task_switch_enable = task_switch_enable_old;//{MUTEX for multi-Proc}
		return ret; break;
	default:
		printlog(_LOG_ERROR, "Bad syscall: 0x%[32H]", _IMM(callid));
		break;
	}
	return 0;
}

void call_gate() { // noreturn
	stduint para[4];// a c d b
	__asm("push %ds; push %es; push %fs; push %gs");
	__asm("mov  %%eax, %0" : "=m"(para[0]));
	__asm("mov  %%ecx, %0" : "=m"(para[1]));
	__asm("mov  %%edx, %0" : "=m"(para[2]));
	__asm("mov  %%ebx, %0" : "=m"(para[3]));
	__asm("push %ebx");// pushad
	__asm("push %ecx");
	__asm("push %edx");
	__asm("push %ebp");
	__asm("push %esi");
	__asm("push %edi");
	__asm("mov  $8*1, %eax");
	__asm("mov %eax, %ds; mov %eax, %es; mov %eax, %fs; mov %eax, %gs");
	stduint ret = call_body((syscall_t)para[0], para[1], para[2], para[3]);
	__asm("mov  %0, %%eax" : : "m"(ret));
	__asm("pop %edi");// popad
	__asm("pop %esi");
	__asm("pop %ebp");
	__asm("pop %edx");
	__asm("pop %ecx");
	__asm("pop %ebx");
	__asm("pop  %gs; pop %fs; pop %es; pop %ds");
	__asm("mov  %ebp, %esp");
	__asm("pop  %ebp      ");
	__asm("jmp returnfar");
	__asm("callgate_endo:");
	loop;
}

void call_intr() {
	stduint para[4];// a c d b
	__asm("push %ds; push %es; push %fs; push %gs");
	__asm("mov  %%eax, %0" : "=m"(para[0]));
	__asm("mov  %%ecx, %0" : "=m"(para[1]));
	__asm("mov  %%edx, %0" : "=m"(para[2]));
	__asm("mov  %%ebx, %0" : "=m"(para[3]));
	__asm("pushal");// pushad
	__asm("mov  $8*1, %eax");
	__asm("mov %eax, %ds; mov %eax, %es; mov %eax, %fs; mov %eax, %gs");
	call_body((syscall_t)para[0], para[1], para[2], para[3]);
	__asm("popal");// popad
	__asm("pop  %gs; pop %fs; pop %es; pop %ds");
	__asm("mov  %ebp, %esp");
	__asm("pop  %ebp      ");
	__asm("iretl");// iretd
}

void* call_gate_entry() {
	return (void*)call_gate;
}

static stduint syscall0(syscall_t callid) {
	stduint ret;
	__asm("mov  %0, %%eax" : : "m"(callid));
	CallFar(0, SegCall);
	__asm("mov  %%eax, %0" : "=m"(ret));
	return ret;
}
static stduint syscall1(syscall_t callid, stduint para1) {
	stduint ret;
	__asm("mov  %0, %%eax" : : "m"(callid));
	__asm("mov  %0, %%ecx" : : "m"(para1));
	CallFar(0, SegCall);
	__asm("mov  %%eax, %0" : "=m"(ret));
	return ret;
}
static stduint syscall2(syscall_t callid, stduint para1, stduint para2) {
	stduint ret;
	__asm("mov  %0, %%eax" : : "m"(callid));
	__asm("mov  %0, %%ecx" : : "m"(para1));
	__asm("mov  %0, %%edx" : : "m"(para2));
	CallFar(0, SegCall);
	__asm("mov  %%eax, %0" : "=m"(ret));
	return ret;
}
static stduint syscall3(syscall_t callid, stduint para1, stduint para2, stduint para3) {
	stduint ret;
	__asm("mov  %0, %%eax" : : "m"(callid));
	__asm("mov  %0, %%ecx" : : "m"(para1));
	__asm("mov  %0, %%edx" : : "m"(para2));
	__asm("mov  %0, %%ebx" : : "m"(para3));
	CallFar(0, SegCall);
	__asm("mov  %%eax, %0" : "=m"(ret));
	return ret;
}

stduint syscall(syscall_t callid, ...) {
	// GCC style
	Letpara(paras, callid);
	stduint p1, p2, p3;
	switch (syscall_paracnts[_IMM(callid)])
	{
	case 0:
		return syscall0(callid);
		break;
	case 1:
		p1 = para_next(paras, stduint);
		return syscall1(callid, p1);
		break;
	case 2:
		p1 = para_next(paras, stduint);
		p2 = para_next(paras, stduint);
		return syscall2(callid, p1, p2);
		break;
	case 3:
		p1 = para_next(paras, stduint);
		p2 = para_next(paras, stduint);
		p3 = para_next(paras, stduint);
		return syscall3(callid, p1, p2, p3);
		break;
	default:
		break;
	}
	return ~_IMM0;
}

#endif
