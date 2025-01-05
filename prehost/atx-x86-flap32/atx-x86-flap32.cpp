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
#include "../../include/memoman.hpp"

#define statin static inline
#define _sign_entry() extern "C" void start()

use crate uni;

struct mec_gdt {
	_CPU_x86_descriptor null;
	_CPU_x86_descriptor data;
	_CPU_x86_descriptor code;
	_CPU_x86_descriptor rout;
};// on global linear area
struct mecocoa_global_t {
	word ip_flap32;
	word cs_flap32;
	word gdt_len;
	mec_gdt* gdt_ptr;
	word ivt_len;
	dword ivt_ptr;
	// 0x10
	qword zero;
	dword current_screen_mode;
	Color console_backcolor;
	Color console_fontcolor;
	timeval_t system_time;
}*mecocoa_global;

extern byte BSS_ENTO, BSS_ENDO;
void clear_bss() {
	MemSet(&BSS_ENTO, &BSS_ENDO - &BSS_ENTO, 0);
}

static char _buf[257]; String ker_buf;

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
}

extern "C" { void* __dso_handle = 0; }
extern "C" { void __cxa_atexit(void) {} }
extern "C" { void __gxx_personality_v0(void) {} }
extern "C" { void __stack_chk_fail(void) {} }

Paging kernel_paging;

#define mapglb(x) (*(usize*)&(x) |= 0x80000000)

_sign_entry() {
	__asm("movl $0x1E00, %esp");// mov esp, 0x1E00; set stack
	clear_bss();
	temp_init();
	Console.FormatShow("CPU Brand: %s\n\r", text_brand());
	// Check Memory size and update allocator
	Console.FormatShow("Mem Avail: %s\n\r", text_memavail());
	Console.FormatShow("MccGlobal: %d (0x%x/0x100) bytes\n\r",
		byteof(mecocoa_global_t), byteof(mecocoa_global_t));
	//{TODO} MMU Paging
	mecocoa_global->gdt_len = byteof(mec_gdt) - 1;
	mecocoa_global->gdt_ptr = (mec_gdt*)Memory::physical_allocate(byteof(mec_gdt));
	Console.FormatShow("Mem Throw: 0x%[32H]\n\r", Memory::align_basic_4k());
	if (_TEMP true) {
		_physical_allocate = Memory::physical_allocate;
		kernel_paging.page_directory = (PageDirectory*)Memory::physical_allocate(0x1000);
		kernel_paging.Reset();

		Console.FormatShow("PDT: 0x%[32H]\n\r", _IMM((*kernel_paging.page_directory)[0x3FF][1]));

		
		// first PT, 0 .. 0x00400_000
		//pg_table_kernel[_NUM_pg_table_entries - 1];// make loop PDT
		new int;
		new int;
		delete (void*)0x123;
		

	}
	//{TODO} Move GDT and Append Routines and IVT
	mapglb(mecocoa_global->gdt_ptr);
	//{TODO} Create Kernel TSS
	//{TODO} Load Shell (FAT + ELF)
	Console;
	__asm("cli \n hlt");
	// Done
	__asm("sti \n mcca_done: hlt \n jmp mcca_done");
	loop;
}
