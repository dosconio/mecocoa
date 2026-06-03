// ASCII g++ TAB4 LF
// Docutitle: Floppy Loader for FLAP32 or LONG64
// Codifiers: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

//! unchked

#include <c/format/ELF.h>
#include <c/storage/floppy.h>
#include <c/format/filesys/FAT.h>

void Memory::clear_bss() { MemSet(&BSS_ENTO, &BSS_ENDO - &BSS_ENTO, 0); }

void temp_init() {
	_logstyle = _LOG_STYLE_NONE;
	_pref_info = "[Mecocoa]";
}

OstreamTrait* con0_out;// TTY0

// use before loading elf-kernel
#define single_sector  ((byte*)0x100000)
#define fatable_sector ((byte*)0x100200)
#define kernel_addr    ((byte*)0x2200000)

// temp
#define paging_addr    ((byte*)0x200000)

static FAT_FileHandle filhan;

uint64 GDT64[]{
	0x0000000000000000ull,//(SegNull) Ring0
	0x000F9A000000FFFFull,//(SegCo16) Ring0
	0x0020980000000000ull,//(SegCo64) Ring0
	0x00CF92000000FFFFull,//(SegData) Ring0
	0x00CF9A000000FFFFull,//(SegCo32) Ring0
};// no address and limit for x64

_ESYM_C void B32_LoadKer32();
_ESYM_C void B32_LoadMod64();
void (*entry_kernel)() = nullptr;

// ISA DMA channel 2 transfer configuration helper
static void dma_xfer(int channel, stduint phys_addr, stduint length, bool read) {
	byte mode = read ? 0x46 : 0x4A; // 0x46 for read, 0x4A for write
	outpb(0x0A, 4 + channel);       // mask channel
	outpb(0x0C, 0);                 // clear flip-flop
	outpb(0x0B, mode);              // set mode
	outpb(0x04, phys_addr & 0xFF);         // address low
	outpb(0x04, (phys_addr >> 8) & 0xFF);  // address high
	outpb(0x81, (phys_addr >> 16) & 0xFF); // page register
	outpb(0x0C, 0);                 // clear flip-flop again
	stduint count = length - 1;
	outpb(0x05, count & 0xFF);
	outpb(0x05, (count >> 8) & 0xFF);
	outpb(0x0A, channel);           // unmask channel
}

namespace uni {

	void FloppyDisk::Reset() {
		// Reset controller via DOR (Digital Output Register)
		outpb(PORT_FDC_DOR, 0x00);
		for (volatile int i = 0; i < 100000; i++) _TEMP;
		outpb(PORT_FDC_DOR, 0x0C); // Enable DMA/INT, clear Reset
		motor_state = false;

		for (volatile int i = 0; i < 100000; i++) _TEMP;

		// Sense interrupt status for all 4 drives to clear controller status
		for (int i = 0; i < 4; i++) {
			byte st0, cyl;
			SenseInt(st0, cyl);
		}

		// Configure data rate
		outpb(PORT_FDC_CCR, DATA_RATE);

		// Specify drive timing
		WriteCmd(FDC_CMD_SPECIFY);
		WriteCmd(0xDF); // SRT = 3ms, HUT = 240ms
		WriteCmd(0x02); // HLT = 16ms, ND = 0 (DMA mode)

		// Recalibrate drive
		Recalibrate();
	}

	bool FloppyDisk::Read(stduint BlockIden, void* Dest) {
		if (BlockIden >= getUnits()) return false;

		byte cyl, head, sec;
		LBA2CHS(BlockIden, cyl, head, sec);

		Motor(true);
		if (fn_feedback) fn_feedback();

		// Configure data transfer rate dynamically based on drive type
		outpb(PORT_FDC_CCR, DATA_RATE);

		// Setup ISA DMA Channel 2 (destination buffer: single_sector)
		dma_xfer(2, (stduint)single_sector, 512, true);

		WriteCmd(FDC_CMD_READ_DATA);
		WriteCmd((head << 2) | id); 
		WriteCmd(cyl);
		WriteCmd(head);
		WriteCmd(sec);
		WriteCmd(0x02); // 512 Bytes/Sector
		WriteCmd(SECTORS_PER_TRACK);
		WriteCmd(GAP3_LENGTH);
		WriteCmd(0xFF); // DTL

		// Busy loop wait for transfer completion (polling mode)
		for (volatile int i = 0; i < 100000; i++) _TEMP;

		// Read 7 Status Bytes after operation completion
		byte st0 = ReadData();
		for (int i = 0; i < 6; i++) ReadData(); 

		Motor(false);

		// Check for errors in ST0
		if ((st0 & 0xC0) != 0x00) return false;

		// Copy data to Dest
		MemCopyN(Dest, single_sector, 512);
		return true;
	}

	bool FloppyDisk::Write(stduint BlockIden, const void* Sors) {
		if (BlockIden >= getUnits()) return false;

		// Copy data from Sors to DMA buffer
		MemCopyN(single_sector, Sors, 512);

		byte cyl, head, sec;
		LBA2CHS(BlockIden, cyl, head, sec);

		Motor(true);
		if (fn_feedback) fn_feedback();

		// Configure data transfer rate dynamically
		outpb(PORT_FDC_CCR, DATA_RATE);

		// Setup ISA DMA Channel 2 (source buffer: single_sector)
		dma_xfer(2, (stduint)single_sector, 512, false);

		WriteCmd(FDC_CMD_WRITE_DATA);
		WriteCmd((head << 2) | id);
		WriteCmd(cyl);
		WriteCmd(head);
		WriteCmd(sec);
		WriteCmd(0x02); 
		WriteCmd(SECTORS_PER_TRACK);
		WriteCmd(GAP3_LENGTH);
		WriteCmd(0xFF); 

		// Busy loop wait for transfer completion (polling mode)
		for (volatile int i = 0; i < 100000; i++) _TEMP;

		// Read 7 Status Bytes
		byte st0 = ReadData();
		for (int i = 0; i < 6; i++) ReadData(); 

		Motor(false);
		
		if ((st0 & 0xC0) != 0x00) return false;
		return true;
	}

}

void body() {
	bool support_ia32e = false;
	temp_init();
	BareConsole Console(80, 50, _VIDEO_ADDR_BUFFER); con0_out = &Console;
	Console.setShowY(0, 25);

	// Initialize floppy drive A (id = 0)
	uni::FloppyDisk floppy(0, uni::FloppyDriveType::Drive_1_44MB_3_5);
	floppy.Reset();

	// Load FAT12 filesys directly from the floppy storage
	uni::FilesysFAT pfs_fat0(12, floppy, single_sector, fatable_sector);
	uni::FilesysSearchArgs args = { &filhan, nullptr, nullptr, nullptr };
	if (!pfs_fat0.loadfs()) {
		plogerro("FAT0 loadfs failed"); _ASM("hlt");
	}

	// FLOPPY + FAT + ELF
	// if   mx64.elf exists, load, switch to IA-32e and transfer to mx64.elf
	// elif mx86.elf exists, load, transfer to mx86.elf
	support_ia32e = IfSupport_IA32E();
	ploginfo("[IA32E] %s", support_ia32e ? "Supported" : "No");
	if (support_ia32e && pfs_fat0.search("mx64.elf", &args)) {
		printlog(_LOG_INFO, "Found: long64");
		pfs_fat0.readfl(&filhan, Slice{ 0,filhan.size }, kernel_addr);
	}
	else if (pfs_fat0.search("mx86.elf", &args)) {
		printlog(_LOG_INFO, "Found: flap32");
		support_ia32e = false;
		pfs_fat0.readfl(&filhan, Slice{ 0,filhan.size }, kernel_addr);
	}
	else {
		printlog(_LOG_ERROR, "Kernel not found");
		_ASM("HLT");
	}

	if (support_ia32e) {
		if (pfs_fat0.search("ladder", &args)) {
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
	body();
}

// to be thin, redefine to avoid including unisym's version:
stduint uni::FilesysFAT::writfl(void* fil_handler, Slice file_slice, const byte* sors) { return nil; }
bool uni::FilesysFAT::remove(rostr pathname) { return false; }
bool uni::FilesysFAT::create(rostr fullpath, stduint flags, void* exinfo, rostr linkdest) { return false; }

void operator delete(void* ptr, stduint size) noexcept {}
_ESYM_C void* calloc(size_t nmemb, size_t size) { return 0; }
_ESYM_C void free(void* p) {}
