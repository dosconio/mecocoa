// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#include <stddef.h>
#include <new>
#include <c/stdinc.h>
#include <c/consio.h>
#include <c/cpuid.h>
#include <c/graphic/color.h>
#include <c/datime.h>
#include <c/proctrl/x86/x86.h>
#include <cpp/string>
#include <stdnoreturn.h>
#include "../../include/memoman.hpp"

#include "../../include/atx-x86-flap32.hpp"



extern "C" byte BSS_ENTO, BSS_ENDO;
void clear_bss() {
	MemSet(&BSS_ENTO, &BSS_ENDO - &BSS_ENTO, 0);
}
_ESYM_C{
	char _buf[257]; String ker_buf;
	stduint tmp;
	struct { word u_16; dword u_32; } tmp48;
}
extern void page_init();

statin rostr text_memavail() {
	usize mem = Memory::evaluate_size();
	char unit[]{ ' ', 'K', 'M', 'G', 'T' };
	int level = 0;
	// assert mem != 0
	while (!(mem % 1024)) {
		level++;
		mem /= 1024;
	}
	ker_buf.Format(level ? "%d %cB" : "0x%[32H] B", mem, unit[level]);
	return ker_buf.reference();
}

void temp_init() {
	new (&ker_buf) String(_buf, byteof(_buf));
	mecocoa_global = (mecocoa_global_t*)0x500;
	//
	_physical_allocate = Memory::physical_allocate;
}
/*

#define mapglb(x) (*(usize*)&(x) |= 0x80000000)
#define mglb(x) (_IMM(x) | 0x80000000)

Handler_t syscalls[_TEMP 1];

extern "C" void call_gate() { // noreturn

	const usize callid = *(usize*)mglb(&mecocoa_global->syscall_id);
	if (callid < numsof(syscalls)) {

	}
	else {
		rostr test_msg = (rostr)"\xFF\x70[Syscalls]\xFF\x02 Test OK!\xFF\x07";
		Console.FormatShow("%s\n\r", test_msg + 0x80000000);
	}

	__asm("iret");
	__asm("callgate_endo:");
	loop;
}
*/

_sign_entry() {
	__asm("movl $0x1E00, %esp");// mov esp, 0x1E00; set stack
	clear_bss();
	temp_init();
	Console.FormatShow("\xFF\x07");

/*
	// Check Memory size and update allocator
	Console.FormatShow("Mem Avail: %s\n\r", text_memavail());
	Console.FormatShow("Mcc Globl: %d (0x%x/0x100) bytes\n\r",
		byteof(mecocoa_global_t), byteof(mecocoa_global_t));

	// Align Memory
	mecocoa_global->gdt_len = byteof(mec_gdt) - 1;
	mecocoa_global->gdt_ptr = (mec_gdt*)Memory::physical_allocate(byteof(mec_gdt));
	Console.FormatShow("Mem Throw: 0x%[32H]\n\r", Memory::align_basic_4k());

	// Kernel Paging
	page_init();

	//{TODO} Move GDT and Append Routines and IVT
	// 8*1 Code R0, 8*2 Data R0, 8*3 Gate R3
	// 8*4 Codu R3, 8*5 Datu R3  (User)
	mapglb(mecocoa_global->gdt_ptr);
	__asm("sgdt _buf");
	void* last_gdt_ptr = (void*)*((dword*)(&_buf[2]));
	word last_gdt_len = *((word*)_buf);
	MemCopyN(mecocoa_global->gdt_ptr, last_gdt_ptr, last_gdt_len + 1);
	__asm("lea callgate_endo, %eax");
	__asm("lea call_gate, %ebx");
	__asm("sub %ebx, %eax");
	__asm("mov %eax, tmp");
	//gate_t& callgate = *(gate_t*)&mecocoa_global->gdt_ptr[3];
	//callgate.setModeCall(_IMM(&call_gate) | 0x80000000, tmp);
	//{TODO}
	for0(i, 4) {
		Console.FormatShow(">> %x %x\n\r", ((dword*)mecocoa_global->gdt_ptr)[i*2], ((dword*)mecocoa_global->gdt_ptr)[i*2 + 1]);
	}
	Console.FormatShow("GDT Globl: %x\n\r", mecocoa_global->gdt_ptr);
	Console.FormatShow("IVT Globl: %x\n\r", mecocoa_global->ivt_ptr);
	//Console.FormatShow(">>>: %x\n\r", &callgate);
	//Console.FormatShow(": 0x%x 0x%x\n\r", *(dword*)&(*(mecocoa_global->gdt_ptr)).rout, ((dword*)&(*(mecocoa_global->gdt_ptr)).rout)[1]);
	

	//{TODO} Create Kernel TSS

	//{TODO} Load Shell (FAT + ELF)
	

	//free(new int);//{TODO} malc-with-align + mapping
*/
	__asm("cli \n hlt");
	// Done
	__asm("sti \n mcca_done: hlt \n jmp mcca_done");
	loop;
}

extern "C" { void* __dso_handle = 0; }
extern "C" { void __cxa_atexit(void) {} }
extern "C" { void __gxx_personality_v0(void) {} }
extern "C" { void __stack_chk_fail(void) {} }
