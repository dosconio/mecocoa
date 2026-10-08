// UTF-8 g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Mecocoa
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

_ESYM_C void mecocoa()
{
	if (!Memory::initialize(0, 0)) erro();
	Consman::Initialize();// located here, for  INT-10H may influence PIC
	//{} Cache_t::enAble();
	Filesys::Initialize();
	Systime::Initialize();
	Taskman::Initialize();
	Devsman::Initialize();
	Syscall::Initialize();
	Coreman::Initialize();// Multicore:
	Virtman::Initialize();
	
	// Service
	Taskman::Create((void*)&serv_task_loop, RING_M)->main_thread->name.reset(StrHeap("serv_task_loop"));
	Taskman::Create((void*)&serv_cons_loop, RING_M)->main_thread->name.reset(StrHeap("serv_cons_loop"));
	Taskman::Create((void*)&serv_graf_loop, RING_M)->main_thread->name.reset(StrHeap("serv_graf_loop"));
	Taskman::Create((void*)&serv_file_loop, RING_M)->main_thread->name.reset(StrHeap("serv_file_loop"));
	Taskman::Create((void*)&serv_devs_loop, RING_M)->main_thread->name.reset(StrHeap("serv_devs_loop"));
	//
	//Taskman::Create((void*)&serv_netw_loop, RING_M)->main_thread->name = "serv_netw_loop";
	//Taskman::Create((void*)&serv_dev_audio_loop, RING_M)->main_thread->name = "serv_dev_audio_loop";
	
	IC.enInterrupt();
	
	// loop { }
}
