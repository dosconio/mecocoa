// ASCII g++ TAB4 LF
// Docutitle: Loader for FLAP32 or LONG64
// Codifiers: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST

#include <c/consio.h>
#include <c/format/ELF.h>
#include <c/proctrl/x86/x86.h>
#include <c/storage/harddisk.h>
#include <c/format/filesys/FAT.h>
#include "../../include/atx-x86-flap32.hpp"


statin void clear_bss() { MemSet(&BSS_ENTO, &BSS_ENDO - &BSS_ENTO, 0); }

void temp_init() {
	_logstyle = _LOG_STYLE_NONE;
	_pref_info = "[Mecocoa]";
}

OstreamTrait* con0_out;// TTY0

// use before loading elf-kernel
#define ROOT_DEV_FAT0 (MINOR_hd6a + 2)
#define single_sector  ((byte*)0x100000)
#define fatable_sector ((byte*)0x101000)
#define hdinfo_addr    ((byte*)0x102000)
#define kernel_addr    ((byte*)0x103000)

// temp
#define paging_addr    ((byte*)0x200000)

static FAT_FileHandle filhan;

uint64 GDT64[]{
	0x0000000000000000ull,//(SegNull) Ring0
	0x00CF92000000FFFFull,//(SegData) Ring0
	0x000F9A000000FFFFull,//(SegCo16) Ring0
	0x00CF9A000000FFFFull,//(SegCo32) Ring0
	0x0020980000000000ull,//(SegCo64) Ring0
};// no address and limit for x64

_ESYM_C void B32_LoadKer32();
_ESYM_C void B32_LoadMod64();
void (*entry_kernel)() = nullptr;

void body() {
	bool support_ia32e = false;
	temp_init();
	BareConsole Console(80, 50, _VIDEO_ADDR_BUFFER); con0_out = &Console;
	Console.setShowY(0, 25);
	Harddisk_PATA hdisk(0x01);
	hdisk.Reset();

	MemSet((void*)hdinfo_addr, 0, sizeof(HD_Info));
	DiscPartition::Partition(hdisk, *(HD_Info*)hdinfo_addr, single_sector, 5);
	// HD_Info& hdi = *(HD_Info*)hdinfo_addr;

	DiscPartition part_fat0(hdisk, ROOT_DEV_FAT0);
	FilesysFAT pfs_fat0(32, part_fat0, single_sector, ROOT_DEV_FAT0);
	pfs_fat0.buffer_fatable = fatable_sector;
	
	stduint a[2] = { _IMM(&filhan)/*, _IMM(&filinf) */ };

	if (!pfs_fat0.loadfs()) {
		plogerro("FAT0 loadfs failed"); _ASM("hlt");
	}
	
	// HDISK + FAT + ELF
	// if   mx64.elf exists, load, switch to IA-32e and transfer to mx64.elf
	// elif mx86.elf exists, load, transfer to mx86.elf
	support_ia32e = IfSupport_IA32E();
	ploginfo("[IA32E] %s", support_ia32e ? "Supported" : "No");
	if (support_ia32e && pfs_fat0.search("mx64.elf", &a)) {
		printlog(_LOG_INFO, "Found: long64");
		pfs_fat0.readfl(&filhan, Slice{ 0,filhan.size }, kernel_addr);
	}
	else if (pfs_fat0.search("mx86.elf", &a)) {
		printlog(_LOG_INFO, "Found: flap32");
		support_ia32e = false;
		pfs_fat0.readfl(&filhan, Slice{ 0,filhan.size }, kernel_addr);
	}
	else {
		printlog(_LOG_ERROR, "Kernel not found");
		_ASM("HLT");
	}

	if (support_ia32e) {
		if (pfs_fat0.search("ladder", &a)) {
			pfs_fat0.readfl(&filhan, Slice{ 0,filhan.size }, 0x8000_addr);
		}
		else plogerro("No ladder for x64l");
	}

	if (!support_ia32e) ELF32_LoadExecFromMemory(kernel_addr, (void**)&entry_kernel);
	else ELF64_LoadExecFromMemory(kernel_addr, (void**)&entry_kernel);
	printlog(_LOG_INFO, "Transfer to Kernel at 0x%[32H]", entry_kernel);

	if (!entry_kernel) {
		printlog(_LOG_WARN, "Kernel not found");
		return;
	}

	// (noreturn) switch to IA-32e or not
	if (support_ia32e) {
		// Make Temp Paging
		Letvar(paging, uint64*, paging_addr);
		paging[0x800 / sizeof(uint64)] = paging[0] = _IMM(paging_addr) + 0x1007;
		paging[0x1000 / sizeof(uint64)] = _IMM(paging_addr) + 0x2007;
		for0(i, 6) paging[0x2000 / sizeof(uint64) + i] = 0x200000 * i + 0x83;
		B32_LoadMod64();
		_ASM("HLT");
	}
	else B32_LoadKer32();
}

_sign_entry() {
	__asm("movl $0x70000, %esp");
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

// to be thin, redefine to avoid including unisym's version:
stduint FilesysFAT::writfl(void* fil_handler, Slice file_slice, const byte* sors) { return nil; }
bool FilesysFAT::remove(rostr pathname) { return false; }
bool FilesysFAT::create(rostr fullpath, stduint flags, void* exinfo, rostr linkdest) { return false; }

