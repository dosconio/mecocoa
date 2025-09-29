// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _DEBUG

#include <c/cpuid.h>
#include <c/consio.h>
#include <cpp/string>
#include <cpp/interrupt>
#include <c/format/ELF.h>
#include <c/driver/PIT.h>
#include <c/format/FAT12.h>
#include <c/proctrl/x86/x86.h>
#include <c/storage/harddisk.h>
#include <c/driver/RealtimeClock.h>
#include "../../include/atx-x86-flap32.hpp"

bool opt_info = true;
bool opt_test = true;

_ESYM_C{
char _buf[65]; String ker_buf;
stduint tmp;
}

static ProcessBlock krnl_tss;
void (*entry_temp)();


int* kernel_fail(loglevel_t serious) {
	if (serious == _LOG_FATAL) {
		outsfmt("\n\rKernel panic!\n\r");
		__asm("cli; hlt");
	}
	return nullptr;
}

statin rostr text_brand() {
	CpuBrand(ker_buf.reflect());
	ker_buf.reflect()[_CPU_BRAND_SIZE] = 0;
	return ker_buf.reference();
}

statin void _start_assert() {
	if (byteof(mecocoa_global_t) > 0x100) {
		printlog(_LOG_FATAL, "mecocoa_global_t: %d bytes over size\n\r", byteof(mecocoa_global_t)); loop;
	}
	if (byteof(gate_t) != 8 || byteof(GateInterrupt) != 8) {
		printlog(_LOG_FATAL, "gate_t: %d bytes over size\n\r", byteof(gate_t)); loop;
	}
	if (byteof(TSS_t) != 104) {
		printlog(_LOG_FATAL, "TSS: %d bytes over size\n\r", byteof(TSS_t)); loop;
	}
}

void krnl_init() {
	new (&ker_buf) String(_buf, byteof(_buf));
	(*mecocoa_global).gdt_ptr = (mec_gdt*)0x80000600;
	mecocoa_global->system_time.sec = 0;
	mecocoa_global->system_time.mic = 0;
	//
	_physical_allocate = Memory::physical_allocate;
	_logstyle = _LOG_STYLE_NONE;
	_pref_info = "[Mecocoa] ";
	_call_serious = (_tocall_ft)kernel_fail;//{TODO} DbgStop
	_start_assert();
}

#define IRQ_SYSCALL 0x81// leave 0x80 for unix-like syscall

_ESYM_C void RETONLY();
extern "C" void General_IRQHandler();

bool ento_gui = false;

// in future, some may be abstracted into mecocoa/mccaker.cpp
void MAIN();
_sign_entry() {
	__asm("movl $0x7FF0, %esp");// mov esp, 0x1E00; set stack
	MAIN();
}
void MAIN() {
	Memory::clear_bss();
	krnl_init();
	Memory::text_memavail(ker_buf);
	Memory::align_basic_4k();
	page_init();
	GDT_Init();
	if (opt_info) printlog(_LOG_INFO, "GDT Globl: 0x%[32H]", mecocoa_global->gdt_ptr);
	// if (opt_test) syscall(syscall_t::TEST, (stduint)'T', (stduint)'E', (stduint)'S');
	new (&krnl_tss) ProcessBlock;
	krnl_tss.TSS.CR3 = getCR3();
	krnl_tss.TSS.LDTDptr = 0;
	krnl_tss.TSS.STRC_15_T = 0;
	krnl_tss.TSS.IO_MAP = sizeof(TSS_t) - 1;
	krnl_tss.focus_tty_id = 0;
	krnl_tss.state = ProcessBlock::State::Running;
	mecocoa_global->gdt_ptr->tss.setRange((dword)&krnl_tss.TSS, sizeof(TSS_t) - 1);
	TaskAdd(&krnl_tss);
	__asm("mov $8*5, %eax; ltr %ax");

	// IVT and Device
	InterruptControl GIC(_IMM(0x80000800));// linear but not physical
	GIC.Reset(SegCode);
	GIC[IRQ_PIT].setRange(mglb(Handint_PIT_Entry), SegCode); PIT_Init();
	GIC[IRQ_RTC].setRange(mglb(Handint_RTC_Entry), SegCode); RTC_Init();
	GIC[IRQ_Keyboard].setRange(mglb(Handint_KBD_Entry), SegCode);
	GIC[IRQ_ATA_DISK0].setRange(mglb(Handint_HDD_Entry), SegCode); DEV_Init();
	GIC[IRQ_SYSCALL].setRange(mglb(call_intr), SegCode); GIC[IRQ_SYSCALL].DPL = 3;
	
	MccaTTYCon::cons_init();
	if (opt_test) __asm("ud2");

	printlog(_LOG_WARN, "   It isn't friendly to develop a kernel in pure C++ (GNU g++).");

	Console.OutFormat("Mem Avail: %s\n\r", ker_buf.reference());
	Console.OutFormat("CPU Brand: %s\n\r", text_brand());

	GIC.enAble();

	// Service
	TaskRegister((void*)&MccaTTYCon::serv_cons_loop, 1);
	TaskRegister((void*)&serv_dev_hd_loop, 1);
	TaskRegister((void*)&serv_file_loop, 1);

	//{TODO} Load Shell (FAT + ELF)
	stduint&& bufsize = 512 * 64;
	void* load_buffer = Memory::physical_allocate(bufsize);
	Harddisk_PATA hdisk(0);
	// subappb
	printlog(_LOG_INFO, "Loading Subappb");
	for0(i, 64) hdisk.Read(i + 384, (void*)((char*)load_buffer + 512 * (i)));
	TaskLoad(NULL _TEMP, load_buffer, 3)->focus_tty_id = 2;
	// subappa
	printlog(_LOG_INFO, "Loading Subappa");
	for0(i, 64) hdisk.Read(i + 256, (void*)((char*)load_buffer + 512 * (i)));
	TaskLoad(NULL _TEMP, load_buffer, 3)->focus_tty_id = 1;
	// subappc
	printlog(_LOG_INFO, "Loading Subappc");
	for0(i, 64) hdisk.Read(i + 512, (void*)((char*)load_buffer + 512 * (i)));
	TaskLoad(NULL _TEMP, load_buffer, 3)->focus_tty_id = 0;

	if (false) CallFar(0, 8 * 9);// manually schedule
	if (false) { CallFar(0, 8 * 9); jmpFar(0, 8 * 9); }// re-entry test

	syscall(syscall_t::OUTC, 'O');
	Console.OutFormat("hayouuu~!\n\r");

	// ttycons[0]->OutFormat("HelloTTY%d\n\r", 0);
	// ttycons[1]->OutFormat("HelloTTY%d\n\r", 1);
	// ttycons[2]->OutFormat("HelloTTY%d\n\r", 2);
	// ttycons[3]->OutFormat("HelloTTY%d\n\r", 3);
	// MccaTTYCon::current_switch(0);

	InterruptEnable();
	auto lastsec = mecocoa_global->system_time.sec;
	loop{
		__asm("hlt");
	if (mecocoa_global->system_time.sec != lastsec) {
		lastsec = mecocoa_global->system_time.sec;
		// outc('T');
	}
	}
		
	auto crt = mecocoa_global->system_time.sec;
	stduint esp; __asm("mov %%esp, %0" : "=r"(esp));
	loop{
		stduint newesp; __asm("mov %%esp, %0" : "=r"(newesp));
		if (newesp != esp) {
			Console.OutFormat("ESP: %u\n\r", newesp);
			esp = newesp;
		}
	}
	// Done
	loop HALT();
}
// assert_stack() = assert(%%esp == 0x8000);

extern "C" { void* __dso_handle = 0; }
extern "C" { void __cxa_atexit(void) {} }
extern "C" { void __gxx_personality_v0(void) {} }
extern "C" { void __stack_chk_fail(void) {} }
void operator delete(void*) {}
void operator delete(void* ptr, unsigned size) noexcept { _TODO }