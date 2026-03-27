// UTF-8 g++ TAB4 LF 
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#include <c/consio.h>
#include <cpp/string>
#include <cpp/interrupt>
#include <c/format/ELF.h>
#include <c/driver/PIT.h>
#include <c/driver/mouse.h>
#include <c/driver/timer.h>
#include <cpp/Device/Cache>
#include <c/format/filesys/FAT.h>
#include <c/driver/RealtimeClock.h>

_ESYM_C
{
	alignas(16) byte kernel_stack[4 * 1024] = {};
}

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
	_call_serious = kernel_fail;
	if (!Memory::initialize(_start_eax, (byte*)_start_ebx)) HALT();
	const unsigned mempool_lenN = 0x20000;
	mempool.Append(Slice{ _IMM(mem.allocate(mempool_lenN)), mempool_lenN });
	mempool.Append(Slice{ _IMM(mem.allocate(mempool_lenN)), mempool_lenN });
	mempool.Append(Slice{ _IMM(mem.allocate(mempool_lenN)), mempool_lenN });
	cons_init();// located here, for  INT-10H may influence PIC
	Cache_t::enAble();
	Taskman::Initialize();
	// IVT and Device
	IC.Reset(SegCo32, 0x80000000);
	IC.Init();
	IC[IRQ_PIT].setRange(mglb(Handint_PIT_Entry), SegCo32); PIT_Init();
	IC[IRQ_RTC].setRange(mglb(Handint_RTC_Entry), SegCo32); RTC_Init();
	IC[IRQ_Keyboard].setRange(mglb(Handint_KBD_Entry), SegCo32); Keyboard_Init();
	IC[IRQ_PS2_Mouse].setRange(mglb(Handint_MOU_Entry), SegCo32); Mouse_Init();
	IC[IRQ_ATA_DISK0].setRange(mglb(Handint_HDD_Entry), SegCo32); DEV_Init();
	IC[IRQ_SYSCALL].setRange(mglb(Handint_INTCALL_Entry), SegCo32); IC[IRQ_SYSCALL].DPL = 3;


	mecfetch();
	__asm("ud2");

	ploginfo("[Memoman] total memory %[x]", Memory::total_memsize);

	// Service
	Taskman::Create((void*)&serv_cons_loop, 1);
	Taskman::Create((void*)&serv_conv_loop, 0);
	Taskman::Create((void*)&serv_dev_hd_loop, 1);
	Taskman::Create((void*)&serv_file_loop, 0);
	Taskman::Create((void*)&serv_task_loop, 0);// GDT operation

	IC.enAble();
	syscall(syscall_t::OUTC, 'O');
	Console.OutFormat("hayouuu~!\n\r");

	Memory::pagebmap->dump_avail_memory();

	// Done
	loop HALT();
}
