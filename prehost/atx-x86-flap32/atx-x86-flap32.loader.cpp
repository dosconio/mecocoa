// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _DEBUG
#include <new>
#include <c/consio.h>
#include <c/cpuid.h>
#include <c/graphic/color.h>
#include <c/format/FAT12.h>
#include <c/format/ELF.h>
#include <c/storage/harddisk.h>
#include <c/datime.h>
#include <c/proctrl/x86/x86.h>
#include <cpp/string>
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
	_logstyle = _LOG_STYLE_NONE;
	_pref_info = "[Mecocoa] ";
}

statin void _start_assert() {
	if (byteof(mecocoa_global_t) > 0x100) {
		Console.FormatShow("Mcc Globl: %d bytes over size\n\r", byteof(mecocoa_global_t));
		loop;
	}
}

_sign_entry() {
	__asm("movl $0x1E00, %esp");// mov esp, 0x1E00; set stack
	clear_bss();
	temp_init();
	_start_assert();

	// Set Console Style
	Console.FormatShow("\xFF\x07");

	// Show CPU Info
	Console.FormatShow("CPU Brand: %s\n\r", text_brand());

	// Read Kernel.Elf (~>30KB)
	//{TODO FAT for HDISK} HDISK + FAT + ELF
	void (*entry_kernel)();
	printlog(_LOG_INFO, "Loading Kernel...");
	Harddisk_t hdisk(Harddisk_t::HarddiskType::LBA28);
	for0(i, 128) hdisk.Read(i, (void*)(0x100000 + 512 * i));// 64KB
	ELF32_LoadExecFromMemory((void*)0x100000, (void**)&entry_kernel);
	printlog(_LOG_INFO, "Loading Kernel from %[32H]", entry_kernel);
	entry_kernel();

	__asm("cli \n hlt");
}

extern "C" { void* __dso_handle = 0; }
extern "C" { void __cxa_atexit(void) {} }
extern "C" { void __gxx_personality_v0(void) {} }
extern "C" { void __stack_chk_fail(void) {} }
