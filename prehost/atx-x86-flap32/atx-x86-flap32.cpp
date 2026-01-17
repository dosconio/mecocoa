// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
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

_ESYM_C{
char _buf[65]; String ker_buf;
extern uint32 _start_eax, _start_ebx;
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
	Memory::clear_bss();
	new (&ker_buf) String(_buf, byteof(_buf));
	(*mecocoa_global).gdt_ptr = (mec_gdt*)0x80000600;
	mecocoa_global->system_time.sec = 0;
	mecocoa_global->system_time.mic = 0;
	//
	_physical_allocate = Memory::physical_allocate;
	_logstyle = _LOG_STYLE_NONE;
	_pref_info = "[Mecocoa]";
	_pref_warn = _pref_info;
	_call_serious = (_tocall_ft)kernel_fail;//{TODO} DbgStop
	_start_assert();
	Memory::pagebmap = 0;
	Memory::text_memavail(ker_buf); Memory::align_basic_4k();
	// Paging
	kernel_paging.Reset();// should take 0x1000
	kernel_paging.MapWeak(0x00000000, 0x00000000, 0x00400000, true, _Comment(R0) true);
	kernel_paging.MapWeak(0x80000000, 0x00000000, 0x00400000, true, _Comment(R0) false);
	setCR3(_IMM(kernel_paging.page_directory));
	PagingEnable();
	// rostr test_page = (rostr)"\xFF\x70[Mecocoa]\xFF\x27 Paging Test OK!\xFF\x07" + 0x80000000;
	// if (opt_test) Console.OutFormat("%s\n\r", test_page);
	GDT_Init();
}

extern ProcessBlock* pblocks[16]; extern stduint pnumber;

static void kernel_task_init() {
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
}

_sign_entry() {
	// Memory
	krnl_init();// blind using memory
	cons_init();// located here, for  INT-10H may influence PIC
	if (!Memory::init(_start_eax, (byte*)_start_ebx)) {
		Console.OutFormat("Def Param: A=0x%[x], B=0x%[x]\n\r", _start_eax, _start_ebx);
		plogerro("Unknown boot source or memory too complex.");
		_ASM("HLT");
	}
	Memory::text_memavail(ker_buf);
	Cache_t::enAble();
	kernel_task_init();

	// IVT and Device
	InterruptControl GIC(_IMM(0x80000800));
	GIC.Reset(SegCode);
	GIC.enAble();
	GIC[IRQ_PIT].setRange(mglb(Handint_PIT_Entry), SegCode); PIT_Init();
	GIC[IRQ_RTC].setRange(mglb(Handint_RTC_Entry), SegCode); RTC_Init();
	GIC[IRQ_Keyboard].setRange(mglb(Handint_KBD_Entry), SegCode); Keyboard_Init();
	GIC[IRQ_PS2_Mouse].setRange(mglb(Handint_MOU_Entry), SegCode); Mouse_Init();
	GIC[IRQ_ATA_DISK0].setRange(mglb(Handint_HDD_Entry), SegCode); DEV_Init();
	GIC[IRQ_SYSCALL].setRange(mglb(call_intr), SegCode); GIC[IRQ_SYSCALL].DPL = 3;
	
	if (opt_test) __asm("ud2");

	Console.OutFormat("Mem Avail: %s\n\r", ker_buf.reference());

	Console.OutFormat("CPU Brand: %s\n\r", text_brand());

	tm datime; CMOS_Readtime(&datime);
	Console.OutFormat("Date Time: %d/%d/%d %d:%d:%d\n\r",
		datime.tm_year, datime.tm_mon, datime.tm_mday,
		datime.tm_hour, datime.tm_min, datime.tm_sec
	);
	
	// Service
	TaskRegister((void*)&serv_cons_loop, 1);
	TaskRegister((void*)&serv_dev_hd_loop, 1);
	TaskRegister((void*)&serv_file_loop, 0);
	TaskRegister((void*)&serv_task_loop, 0);// GDT operation
	syscall(syscall_t::OUTC, 'O');// with effect InterruptEnable();
	Console.OutFormat("hayouuu~!\a\n\r");

	dump_avail_memory();


	//! auto lastsec = mecocoa_global->system_time.sec;
	loop __asm("hlt");

	#if 0
	if (lastsec == 8) {
		for0(i, pnumber) {
			Console.OutFormat("-- %u: (%u:%u) head %u, next %u, send_to_whom\n\r",
				i, pblocks[i]->state, pblocks[i]->block_reason,
				pblocks[i]->queue_send_queuehead, pblocks[i]->queue_send_queuenext);
		}
		break;
	}
	#endif

	stduint esp; __asm("mov %%esp, %0" : "=r"(esp));
	loop{
		__asm("hlt");
		stduint newesp; __asm("mov %%esp, %0" : "=r"(newesp));
		if (newesp != esp) {
			Console.OutFormat("ESP: %u\n\r", esp = newesp);
		}
	}
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