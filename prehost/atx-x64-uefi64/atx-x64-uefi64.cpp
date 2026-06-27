// ASCII g++ TAB4 CRLF
// Attribute: Arch(AMD64)
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"
#include "c/driver/UART.h"
#include <c/format/filesys/FAT.h>
#include <cpp/Witch/Control/Control-Label.hpp>

alignas(16) byte kernel_stack[1024 * 1024];

UefiData uefi_data;


extern OstreamTrait* con0_out;


// ---- Kernel

#define Systime SysTimer
extern "C" //__attribute__((ms_abi))
void mecocoa(const UefiData& uefi_data_ref)
{
	#ifdef _UEFI
	stduint rsp;
	uefi_data = uefi_data_ref;
	_ASM("mov %%rsp, %0" : "=r"(rsp));
	#endif

	_call_serious = kernel_fail;
	x86_COM com1;
	con0_out = &com1;
	if (!Memory::initialize('UEFI', (byte*)(&uefi_data.memory_map))) HALT();
	Consman::Initialize();
	//{} Cache_t::enAble();
	Filesys::Initialize();
	Systime::Initialize();
	Taskman::Initialize();
	Devsman::Initialize();
	Virtman::Initialize();
	Syscall::Initialize();
	// Coreman::Initialize();

	//{} mem > 4G

	#ifdef _UEFI
	ploginfo("Ciallo %lf, rsp=%[x]", 2025.09, rsp);
	#endif

	// Deivce Driver
	mecfetch();
	tryUD();

	// Service
	Taskman::Create((void*)&serv_task_loop, RING_M);
	Taskman::Create((void*)&serv_cons_loop, RING_M);
	Taskman::Create((void*)&serv_graf_loop, RING_M);
	Taskman::Create((void*)&serv_file_loop, RING_M);
	//
	Taskman::Create((void*)&serv_dev_mem_loop, RING_M);

	// try priority_queue Dchain
	SysTimer::Append(250, 1);
	SysTimer::Append(100, 0);

	serv_sysmsg();
}
