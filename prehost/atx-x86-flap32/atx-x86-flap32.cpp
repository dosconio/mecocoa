// UTF-8 g++ TAB4 LF 
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#include <c/format/ELF.h>
#include "c/driver/UART.h"
#include <c/driver/mouse.h>
#include <c/driver/timer.h>
#include <cpp/Device/Cache>
#include <c/format/filesys/FAT.h>


extern uint32 _start_eax, _start_ebx;
extern OstreamTrait* con0_out;
_sign_entry() {
	_call_serious = kernel_fail;
	x86_COM com1; con0_out = &com1;// early UART
	if (!Memory::initialize(_start_eax, (byte*)_start_ebx)) HALT();
	Consman::Initialize();// located here, for  INT-10H may influence PIC
	Cache_t::enAble();
	Filesys::Initialize();
	SysTimer::Initialize();
	Taskman::Initialize();
	Devsman::Initialize();
	Syscall::Initialize();

	mecfetch();
	__asm("ud2");

	// Service
	Taskman::Create((void*)&serv_task_loop, RING_M);
	Taskman::Create((void*)&serv_cons_loop, RING_M);
	Taskman::Create((void*)&serv_graf_loop, RING_M);
	Taskman::Create((void*)&serv_file_loop, RING_M);
	//
	Taskman::Create((void*)&serv_dev_mem_loop, RING_M);
	Taskman::Create((void*)&serv_dev_hd_loop, RING_M);

	IC.enInterrupt();
	// syscall(syscall_t::OUTC, 'O', 0);
	// Console.OutFormat("hayouuu~!\n\r\a");

	// mempool0.dump_available();

	loop HALT();
}
