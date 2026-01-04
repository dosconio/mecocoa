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
#define single_sector ((byte*)0x100000)
#define hdinfo_addr   0x110000
static FAT_FileHandle filhan;
Harddisk_PATA* pdisk;
void partition(uni::Harddisk_PATA& drv, unsigned device, bool primary_but_logical = true);
struct HD_Info {
    Slice primary[NR_PRIM_PER_DRIVE];
    Slice logical[NR_SUB_PER_DRIVE];
};

_sign_entry() {
	__asm("movl $0x1E00, %esp");// mov esp, 0x1E00; set stack
	clear_bss();
	temp_init();
	void (*entry_kernel)();
	BareConsole Console(80, 50, 0xB8000); con0_out = &Console;
	Console.setShowY(0, 25);
	printlog(_LOG_INFO, "Loading Kernel...");
	Harddisk_PATA hdisk(0x01);
	pdisk = &hdisk;

	DiscPartition part_fat0(hdisk, ROOT_DEV_FAT0);
	FilesysFAT pfs_fat0(32, part_fat0, (byte*)0x101000, ROOT_DEV_FAT0);
	pfs_fat0.buffer_fatable = (byte*)0x800;
	
	stduint a[2] = { _IMM(&filhan)/*, _IMM(&filinf) */ };
	MemSet((void*)hdinfo_addr, 0, sizeof(HD_Info));
	partition(hdisk, 5);

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


//{TODO} into unisym
static void get_partition_table(Harddisk_PATA& drv, unsigned partable_sectposi, PartitionTableX86* pt)
{
	drv.Read(partable_sectposi, single_sector);
	MemCopyN(pt, single_sector + PARTITION_TABLE_OFFSET, sizeof(*pt) * NR_PART_PER_DRIVE);
}
void partition(uni::Harddisk_PATA &drv, unsigned device, bool primary_but_logical) {
	unsigned drive = DRV_OF_DEV(device);
	HD_Info& hdi = *(HD_Info*)hdinfo_addr;
	// ploginfo("%s: drive%u", __FUNCIDEN__, drive);
	Letvar(part_tbl, PartitionTableX86*, single_sector);
	if (primary_but_logical) {
		get_partition_table(drv, 0, part_tbl);
		int nr_prim_parts = 0;
		for0 (i, NR_PART_PER_DRIVE) {
			if (part_tbl[i].type == NO_PART) 
				continue;
			nr_prim_parts++;
			hdi.primary[i + 1].address = part_tbl[i].lba_start;
			hdi.primary[i + 1].length = part_tbl[i].lba_count;
			if (part_tbl[i].type == EX_PART)
				partition(drv, device + i + 1, false);
		}
		// assert(nr_prim_parts != 0);
	}
	else {
		int j = device % NR_PRIM_PER_DRIVE; /* 1~4 */
		int ext_start_sect = hdi.primary[j].address;
		int s = ext_start_sect;
		int nr_1st_sub = (j - 1) * NR_SUB_PER_PART; /* 0/16/32/48 */
		for0 (i, NR_SUB_PER_PART) {
			int dev_nr = nr_1st_sub + i;/* 0~15/16~31/32~47/48~63 */
			get_partition_table(drv, s, part_tbl);
			hdi.logical[dev_nr].address = s + part_tbl[0].lba_start;
			hdi.logical[dev_nr].length = part_tbl[0].lba_count;
			s = ext_start_sect + part_tbl[1].lba_start;
			if (part_tbl[1].type == NO_PART) {
				break;
			}
		}
	}
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
