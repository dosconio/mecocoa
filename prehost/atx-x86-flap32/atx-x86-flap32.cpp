// UTF-8 g++ TAB4 LF 
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#include <c/format/ELF.h>
#include <c/driver/mouse.h>
#include <c/driver/timer.h>
#include <cpp/Device/Cache>
#include <c/format/filesys/FAT.h>


extern uint32 _start_eax, _start_ebx;
extern RMOD_LIST __init_rmod_ento[], __init_rmod_endo[];
_sign_entry() {
	_call_serious = kernel_fail;
	if (!Memory::initialize(_start_eax, (byte*)_start_ebx)) HALT();
	cons_init();// located here, for  INT-10H may influence PIC
	Cache_t::enAble();
	Taskman::Initialize();
	// IVT and Device
	IC.Reset(SegCo32, 0x80000000);
	IC.Init();
	for (auto func = __init_rmod_ento; func < __init_rmod_endo; func++) (func->init)();

	IC[IRQ_Keyboard].setRange(mglb(Handint_KBD_Entry), SegCo32); Keyboard_Init();
	IC[IRQ_PS2_Mouse].setRange(mglb(Handint_MOU_Entry), SegCo32); Mouse_Init();
	IC[IRQ_ATA_DISK0].setRange(mglb(Handint_HDD_Entry), SegCo32); DEV_Init();
	IC[IRQ_SYSCALL].setRange(mglb(Handint_INTCALL_Entry), SegCo32); IC[IRQ_SYSCALL].DPL = 3;

	mecfetch();
	__asm("ud2");

	// Service
	Taskman::Create((void*)&serv_task_loop, 0);
	Taskman::Create((void*)&serv_cons_loop, 0);
	Taskman::Create((void*)&serv_conv_loop, 0);
	Taskman::Create((void*)&serv_dev_hd_loop, 0);
	Taskman::Create((void*)&serv_file_loop, 0);

	IC.enAble();
	syscall(syscall_t::OUTC, 'O', 0);
	Console.OutFormat("hayouuu~!\n\r");

	// Done
	loop HALT();
}
