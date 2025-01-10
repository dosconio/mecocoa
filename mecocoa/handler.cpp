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

void Handint_PIT()
{
	// auto push flag by intterrupt module
	// 1000Hz
	__asm("push %eax; push %ebx; push %ecx; push %edx; push %esi; push %edi;");
	mecocoa_global->system_time.mic += 1000;// 1k us = 1ms
	if (mecocoa_global->system_time.mic >= 1000000) {
		mecocoa_global->system_time.mic -= 1000000;
		mecocoa_global->system_time.sec++;
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
		static unsigned i = 0;
		if (false) printlog(_LOG_TRACE, "switch task %d", (i+1) % numsof(TasksAvailableSelectors));
		jmpFar(0, TasksAvailableSelectors[(++i) % numsof(TasksAvailableSelectors)]);
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

void Handint_KBD() // Keyboard
{
	__asm("push %eax; push %ebx; push %ecx; push %edx; push %esi; push %edi;");
	byte loc_buf[3];
	static int pref_e0 = 0;
	outpb(0xA0, ' '); // slaver
	outpb(0x20, ' '); // master
	loc_buf[0] = innpb(PORT_KBD_BUFFER);
	if (loc_buf[0] == 0xE0)
		pref_e0 = 1;
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
