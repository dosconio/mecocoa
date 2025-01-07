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
#include <c/format/FAT12.h>
#include <c/format/ELF.h>
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
	char _buf[65]; String ker_buf;
	stduint tmp;
	struct { word u_16; dword u_32; } tmp48;
}
extern void page_init();

statin rostr text_brand() {
	CpuBrand(ker_buf.reflect());
	ker_buf.reflect()[_CPU_BRAND_SIZE] = 0;
	return ker_buf.reference();
}

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


_sign_entry() {
	__asm("movl $0x1E00, %esp");// mov esp, 0x1E00; set stack
	clear_bss();
	temp_init();
	Console.FormatShow("\xFF\x07");
	Console.FormatShow("CPU Brand: %s\n\r", text_brand());

	// Check Memory size and update allocator
	Console.FormatShow("Mem Avail: %s\n\r", text_memavail());
	Console.FormatShow("Mcc Globl: %d (0x%x/0x100) bytes\n\r",
		byteof(mecocoa_global_t), byteof(mecocoa_global_t));

	// Read a Kernel.Elf (~>30KB)
	__asm("push %ecx");
	__asm("push %ebx");
	__asm("mov $0x100000, %ebx");
	__asm("mov $0, %eax");
	__asm("mov $1, %ecx");
	__asm("call HdiskLBA28Load");
	__asm("pop %ebx");
	__asm("pop %ecx");

	for0p(byte, p, 0x100000, 0x200) Console.FormatShow("%[8H] ", *p);
	//{TODO} HDISK + FAT + ELF

	__asm("cli \n hlt");
}

extern "C" { void* __dso_handle = 0; }
extern "C" { void __cxa_atexit(void) {} }
extern "C" { void __gxx_personality_v0(void) {} }
extern "C" { void __stack_chk_fail(void) {} }
