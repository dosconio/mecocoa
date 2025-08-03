// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _DEBUG
#include <c/consio.h>
#include <c/format/ELF.h>
#include <c/format/FAT12.h>
#include <c/proctrl/x86/x86.h>
#include <c/storage/harddisk.h>
#include "../../include/atx-x86-flap32.hpp"

extern "C" byte BSS_ENTO, BSS_ENDO;
statin void clear_bss() { MemSet(&BSS_ENTO, &BSS_ENDO - &BSS_ENTO, 0); }

void temp_init() {
	_logstyle = _LOG_STYLE_NONE;
	_pref_info = "[Mecocoa] ";
}

//{TODO FAT for HDISK} HDISK + FAT + ELF with fixed name "kernel-atx-x86"

BareConsole* BCONS0;// TTY0
_sign_entry() {
	__asm("movl $0x1E00, %esp");// mov esp, 0x1E00; set stack
	clear_bss();
	temp_init();
	void (*entry_kernel)();
	BareConsole Console(80, 50, 0xB8000); BCONS0 = &Console;
	Console.setShowY(0, 25);
	printlog(_LOG_INFO, "Loading Kernel...");
	Harddisk_PATA hdisk(Harddisk_PATA::HarddiskType::LBA28);
	for0(i, 128) hdisk.Read(i, (void*)(0x100000 + 512 * i));// 64KB
	ELF32_LoadExecFromMemory((void*)0x100000, (void**)&entry_kernel);
	printlog(_LOG_INFO, "Transfer to Kernel at 0x%[32H]", entry_kernel);
	entry_kernel();// noreturn
}

extern "C" { void* __dso_handle = 0; }
extern "C" { void __cxa_atexit(void) {} }
extern "C" { void __gxx_personality_v0(void) {} }
extern "C" { void __stack_chk_fail(void) {} }
