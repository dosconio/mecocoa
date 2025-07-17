// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _DEBUG
#include <new>
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
tmp48le_t tmp48_le;
tmp48be_t tmp48_be;
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
	(*mecocoa_global).gdt_ptr = (mec_gdt*)0x600;
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

void a(char ch) { Console.OutChar(ch); }

// in future, some may be abstracted into mecocoa/mccaker.cpp
_sign_entry() {
	__asm("movl $0x8000, %esp");// mov esp, 0x1E00; set stack
	Memory::clear_bss();
	krnl_init();
	MccaTTYCon::cons_init();

	if (opt_info) printlog(_LOG_INFO, "Kernel Loaded!");
	printlog(_LOG_WARN, "   It isn't friendly to develop a kernel in pure C++ (GNU g++).");

	// Show CPU Info
	Console.OutFormat("CPU Brand: %s\n\r", text_brand());
	// Check Memory size and update allocator
	Console.OutFormat("Mem Avail: %s\n\r", Memory::text_memavail(ker_buf));

	// Align Memory and Keep a page for REAL 16 below 0x10000
	printlog(_LOG_INFO, "Mem Throw: 0x%[32H]", Memory::align_basic_4k());
	void* p_real16 {Memory::physical_allocate(0x1000)};

	// Kernel Paging
	page_init();
	
	// GDT and TSS
	GDT_Init();
	if (opt_info) printlog(_LOG_INFO, "GDT Globl: 0x%[32H]", mecocoa_global->gdt_ptr);
	if (opt_test) syscall(syscall_t::TEST, (stduint)'T', (stduint)'E', (stduint)'S');
	krnl_tss.TSS.CR3 = (dword)Paging::page_directory;
	krnl_tss.TSS.LDTDptr = 0;
	krnl_tss.TSS.STRC_15_T = 0;
	krnl_tss.TSS.IO_MAP = sizeof(TSS_t) - 1;
	krnl_tss.focus_tty_id = 0;
	mecocoa_global->gdt_ptr->tss.setRange((dword)&krnl_tss.TSS, sizeof(TSS_t) - 1);
	TaskAdd(&krnl_tss);
	__asm("mov $8*5, %eax; ltr %ax");
	printlog(_LOG_INFO, "SEGM: There are %d GDTs.", GDT_GetNumber());

	// IVT and Device
	InterruptControl GIC(mecocoa_global->ivt_ptr = mglb(0x800));// linear but not physical
	GIC.Reset(SegCode);
	if (opt_info) printlog(_LOG_INFO, "IDT Globl: 0x%[32H]", &GIC);
	GIC[IRQ_PIT].setRange((dword)Handint_PIT, SegCode); PIT_Init();
	GIC[IRQ_RTC].setRange((dword)Handint_RTC, SegCode); RTC_Init();
	GIC[IRQ_Keyboard].setRange((dword)Handint_KBD, SegCode);
	GIC[IRQ_SYSCALL].setRange((dword)call_intr, SegCode); GIC[IRQ_SYSCALL].DPL = 3;
	if (opt_test) __asm("ud2");

	//{TODO} Switch Graphic Mode
	if (opt_test) __asm("call SwitchReal16");
	if (opt_test) Console.OutFormat("\xFF\x70[Mecocoa]\xFF\x02 Real16 Switched Test OK!\xFF\x07\n\r");
	
	// Service
	TaskRegister((void*)&MccaTTYCon::serv_cons_loop, 1);

	//{TODO} Load Shell (FAT + ELF)
	stduint&& bufsize = 512 * 64;
	void* load_buffer = Memory::physical_allocate(bufsize);
	Harddisk_t hdisk(Harddisk_t::HarddiskType::LBA28);
	// subappb
	for0(i, 64) hdisk.Read(i + 128, (void*)((char*)load_buffer + 512 * (i)));
	ELF32_LoadExecFromMemory((void*)load_buffer, (void**)&entry_temp);
	printlog(_LOG_INFO, "Load Subappb at 0x%[32H]", entry_temp);
	TaskRegister((void*)entry_temp, 3)->focus_tty_id = 2;
	// subappa
	for0(i, 64) hdisk.Read(i + 256, (void*)((char*)load_buffer + 512 * (i)));
	ELF32_LoadExecFromMemory((void*)load_buffer, (void**)&entry_temp);
	printlog(_LOG_INFO, "Load Subappa at 0x%[32H]", entry_temp);
	TaskRegister((void*)entry_temp, 3)->focus_tty_id = 1;
	//{!} Memory is used out!

	if (false) CallFar(0, 8 * 9);// manually schedule
	if (false) { CallFar(0, 8 * 9); jmpFar(0, 8 * 9); }// re-entry test

	syscall(syscall_t::OUTC, 'O');
	Console.OutFormat("hayouuu~!\n\r");

	// MccaTTYCon::current_switch(1);
	ttycons[0]->OutFormat("HelloTTY%d\n\r", 0);
	ttycons[1]->OutFormat("HelloTTY%d\n\r", 1);
	ttycons[2]->OutFormat("HelloTTY%d\n\r", 2);
	ttycons[3]->OutFormat("HelloTTY%d\n\r", 3);
	MccaTTYCon::current_switch(0);

	//__asm("hlt");
	GIC.enAble();
	// GIC.enAble(false);
	// GIC.enAble(false);
	// CallFar(0, 8 * 9);
	loop{
		//MccaTTYCon::serv_cons_loop();
		__asm("hlt");
	}
		
	auto crt = mecocoa_global->system_time.sec;
	stduint esp; __asm("mov %%esp, %0" : "=r"(esp));
	loop{
		if (mecocoa_global->system_time.sec != crt) {
			crt = mecocoa_global->system_time.sec;
			Console.OutFormat(" CrtLine=%u \n\r", BCONS0->crtline);
		}
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
