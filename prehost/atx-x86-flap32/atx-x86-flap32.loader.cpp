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
#include <c/format/filesys/FAT.h>
#include <c/proctrl/x86/x86.h>
#include <c/storage/harddisk.h>
#include "cpp/Device/Storage/HD-DEPEND.h"
#include "../../include/atx-x86-flap32.hpp"

extern "C" byte BSS_ENTO, BSS_ENDO;
statin void clear_bss() { MemSet(&BSS_ENTO, &BSS_ENDO - &BSS_ENTO, 0); }

void temp_init() {
	_logstyle = _LOG_STYLE_NONE;
	_pref_info = "[Mecocoa] ";
}

// HDISK + FAT + ELF with fixed name "mx86.elf"
//{TODO} make lighter boot for Partition Table

OstreamTrait* con0_out;// TTY0

#define ROOT_DEV_FAT0 (MINOR_hd6a + 2)
#define single_sector  ((byte*)0x100000)
#define fatable_sector ((byte*)0x101000)
#define hdinfo_addr   0x110000
static FAT_FileHandle filhan;
Harddisk_PATA* pdisk;



void body() {
	temp_init();
	void (*entry_kernel)();
	BareConsole Console(80, 50, 0xB8000); con0_out = &Console;
	Console.setShowY(0, 25);
	printlog(_LOG_INFO, "Loading Kernel...");
	Harddisk_PATA hdisk(0x01);
	pdisk = &hdisk;

	DiscPartition part_fat0(hdisk, ROOT_DEV_FAT0);
	FilesysFAT pfs_fat0(32, part_fat0, single_sector, ROOT_DEV_FAT0);
	pfs_fat0.buffer_fatable = fatable_sector;
	
	stduint a[2] = { _IMM(&filhan)/*, _IMM(&filinf) */ };
	MemSet((void*)hdinfo_addr, 0, sizeof(HD_Info));
	part_fat0.Partition(*(HD_Info*)hdinfo_addr, single_sector, 5);

	HD_Info& hdi = *(HD_Info*)hdinfo_addr;
	if (!pfs_fat0.loadfs()) {
		plogerro("FAT0 loadfs failed"); _ASM("hlt");
	}
	if (FAT_FileHandle* han = (FAT_FileHandle*)pfs_fat0.search("mx86.elf", &a)) {
		pfs_fat0.readfl(han, Slice{ 0,han->size }, (byte*)0x120000);
	}
	
	ELF32_LoadExecFromMemory((void*)0x120000, (void**)&entry_kernel);
	printlog(_LOG_INFO, "Transfer to Kernel at 0x%[32H]", entry_kernel);

	if (!entry_kernel) {
		printlog(_LOG_WARN, "Kernel not found");
		return;
	}
	entry_kernel();// noreturn
}
_sign_entry() {
	__asm("movl $0x200000, %esp");
	clear_bss();
	body();
}


void DiscPartition::renew_slice()
{
	Slice* retp;
	HD_Info& hdinfo = *(HD_Info*)hdinfo_addr;
	retp = device < MINOR_hd1a ?
		&hdinfo.primary[device % NR_PRIM_PER_DRIVE] :
		&hdinfo.logical[(device - MINOR_hd1a) % NR_SUB_PER_DRIVE];//{} 2 disks
	self.slice = *retp;
}

extern "C" { void* __dso_handle = 0; }
extern "C" { void __cxa_atexit(void) {} }
extern "C" { void __gxx_personality_v0(void) {} }
extern "C" { void __stack_chk_fail(void) {} }
