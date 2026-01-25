// UTF-8 g++ TAB4 LF 
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST

#include <c/cpuid.h>
#include <c/consio.h>
#include <cpp/string>
#include <cpp/interrupt>
#include <c/format/ELF.h>
#include <c/driver/PIT.h>
#include <c/driver/mouse.h>
#include <c/driver/timer.h>
#include <cpp/Device/Cache>
#include <c/proctrl/x86/x86.h>
#include <c/storage/harddisk.h>
#include <cpp/Device/Buzzer.hpp>
#include <c/format/filesys/FAT.h>
#include <c/driver/RealtimeClock.h>
#include "../../include/atx-x86-flap32.hpp"

bool opt_info = true;
bool opt_test = true;


char _buf[64];
String ker_buf(_buf, byteof(_buf));
extern uint32 _start_eax, _start_ebx;

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
	const char* ret = ker_buf.reference();
	while (*ret == ' ') ret++;
	return ret;
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
	(*mecocoa_global).gdt_ptr = (mec_gdt*)0x80000600;
	mecocoa_global->system_time.sec = 0;
	mecocoa_global->system_time.mic = 0;
	//
	_physical_allocate = Memory::physical_allocate;
	_call_serious = (_tocall_ft)kernel_fail;//{TODO} DbgStop
	_start_assert();
	Memory::pagebmap = 0;
	Memory::text_memavail(ker_buf); Memory::align_basic_4k();
	// ---- Paging
	kernel_paging.Reset();// should take 0x1000
	kernel_paging.MapWeak(0x00000000, 0x00000000, 0x00400000, true, _Comment(R0) true);
	kernel_paging.MapWeak(0x80000000, 0x00000000, 0x00400000, true, _Comment(R0) false);
	setCR3(_IMM(kernel_paging.root_level_page));
	PagingEnable();
	// rostr test_page = (rostr)"\xFF\x70[Mecocoa]\xFF\x27 Paging Test OK!\xFF\x07" + 0x80000000;
	// if (opt_test) Console.OutFormat("%s\n\r", test_page);
	GDT_Init();
}

extern ProcessBlock* pblocks[16]; extern stduint pnumber;

void mecfetch() {
	const rostr blue = ento_gui ? "\xFE\xF8\xC8\x58" : "\xFF\x30";
	const rostr pink = ento_gui ? "\xFE\xB8\xA8\xF8" : "\xFF\x50";
	const rostr white = ento_gui ? "\xFE\xFF\xFF\xFF" : "\xFF\x70";
	const unsigned attrl = ento_gui ? 4 : 2;
	const unsigned width = ento_gui ? 48 : 16;
	const unsigned height = ento_gui ? 3 : 1;
	// draw a 🏳️‍⚧️ flag
	Console.out(blue, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\n\r"); }
	Console.out(pink, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\n\r"); }
	Console.out(white, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\n\r"); }
	Console.out(pink, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\n\r"); }
	Console.out(blue, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\n\r"); }
	Console.out("\xFF\xFF", 2);

	Console.OutFormat("Mem Avail: %s\n\r", ker_buf.reference());

	Console.OutFormat("CPU Brand: %s\n\r", text_brand());

	tm datime; CMOS_Readtime(&datime);
	Console.OutFormat("Date Time: %d/%d/%d %d:%d:%d\n\r",
		datime.tm_year, datime.tm_mon, datime.tm_mday,
		datime.tm_hour, datime.tm_min, datime.tm_sec
	);

}// like neofetch

_sign_entry() {
	krnl_init();// blind using memory
	cons_init();// located here, for  INT-10H may influence PIC
	if (!Memory::initialize(_start_eax, (byte*)_start_ebx)) {
		Console.OutFormat("Def Param: A=0x%[x], B=0x%[x]\n\r", _start_eax, _start_ebx);
		plogerro("Unknown boot source or memory too complex.");
		_ASM("HLT");
	}
	
	Memory::text_memavail(ker_buf);
	Cache_t::enAble();
	Taskman::Initialize();

	// IVT and Device
	InterruptControl GIC(_IMM(0x80000800));
	GIC.Reset(SegCo32, 0x80000000);
	GIC.Init();
	GIC[IRQ_PIT].setRange(mglb(Handint_PIT_Entry), SegCo32); PIT_Init();
	GIC[IRQ_RTC].setRange(mglb(Handint_RTC_Entry), SegCo32); RTC_Init();
	GIC[IRQ_Keyboard].setRange(mglb(Handint_KBD_Entry), SegCo32); Keyboard_Init();
	GIC[IRQ_PS2_Mouse].setRange(mglb(Handint_MOU_Entry), SegCo32); Mouse_Init();
	GIC[IRQ_ATA_DISK0].setRange(mglb(Handint_HDD_Entry), SegCo32); DEV_Init();
	GIC[IRQ_SYSCALL].setRange(mglb(call_intr), SegCo32); GIC[IRQ_SYSCALL].DPL = 3;

	// _ASM("mov %cr0, %eax;");
	// _ASM("or $0x20, %eax;");
	// __asm("and $0xFFFFFFF1, %eax");// TS(3) EM(2) MP(1)
	// _ASM("mov %eax, %cr0;");

	cons_graf();

	mecfetch();
	if (opt_test) __asm("ud2");

	
	// Service
	TaskRegister((void*)&serv_cons_loop, 1);
	TaskRegister((void*)&serv_dev_hd_loop, 1);
	TaskRegister((void*)&serv_file_loop, 0);
	TaskRegister((void*)&serv_task_loop, 0);// GDT operation

	// GIC.enAble();
	syscall(syscall_t::OUTC, 'O');// with effect InterruptEnable();
	Console.OutFormat("hayouuu~!\a\n\r");

	Memory::pagebmap->dump_avail_memory();

	// Done
	loop HALT();
}
// assert_stack() = assert(%%esp == 0x8000);

_ESYM_C { void* __dso_handle = 0; }
_ESYM_C { void __cxa_atexit(void) {} }
_ESYM_C { void __gxx_personality_v0(void) {} }
_ESYM_C { void __stack_chk_fail(void) {} }
void operator delete(void*) {}
void operator delete(void* ptr, unsigned size) noexcept { _TODO }