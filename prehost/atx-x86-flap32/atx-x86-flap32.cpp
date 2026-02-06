// UTF-8 g++ TAB4 LF 
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _HER_TIME_H

#include <c/consio.h>
#include <cpp/string>
#include <cpp/interrupt>
#include <c/format/ELF.h>
#include <c/driver/PIT.h>
#include <c/driver/mouse.h>
#include <c/driver/timer.h>
#include <cpp/Device/Cache>
#include <c/proctrl/x86/x86.h>
#include <c/format/filesys/FAT.h>
#include <c/driver/RealtimeClock.h>
#include "../../include/atx-x86-flap32.hpp"

void krnl_init() {
	_call_serious = (_tocall_ft)kernel_fail;//{TODO} DbgStop
	// ---- Paging
	_physical_allocate = Memory::physical_allocate;
	kernel_paging.Reset();// should take 0x1000
	kernel_paging.MapWeak(0x00000000, 0x00000000, 0x00400000, true, _Comment(R0) true);
	kernel_paging.MapWeak(0x80000000, 0x00000000, 0x00400000, true, _Comment(R0) false);
	setCR3(_IMM(kernel_paging.root_level_page));
	PagingEnable();
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

	// Console.OutFormat("Mem Avail: %s\n\r", ker_buf.reference());

	Console.OutFormat("CPU Brand: %s\n\r", text_brand());

	tm datime; CMOS_Readtime(&datime);
	Console.OutFormat("Date Time: %d/%d/%d %d:%d:%d\n\r",
		datime.tm_year, datime.tm_mon, datime.tm_mday,
		datime.tm_hour, datime.tm_min, datime.tm_sec
	);

}// like neofetch

extern uint32 _start_eax, _start_ebx;
_sign_entry() {
	krnl_init();// using memory blindly
	if (!Memory::initialize(_start_eax, (byte*)_start_ebx)) HALT();
	cons_init();// located here, for  INT-10H may influence PIC
	

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


	mecfetch();
	__asm("ud2");
	
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
