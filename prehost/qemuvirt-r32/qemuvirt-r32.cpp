// ASCII GAS/RISCV64 TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#include <cpp/Device/UART>
#include <c/driver/timer.h>
#include <c/format/filesys/FAT.h>

OstreamTrait* con0_out = &UART0;

constexpr inline static stduint operator ""_Baud(unsigned long long i) { return i; }

#define TIMER_INTERVAL (CLINT_TIMEBASE_FREQ/100) // 100Hz


extern uint64 last_schepoint;
_ESYM_C
void _entry()
{
	UART0.setMode(115200_Baud);
	ploginfo("Hello Mcca-RV%u~ =OwO= %lf", __BITS__, 2026.03);
	if (!Memory::initialize(0, NULL)) {
		printlog(_LOG_FATAL, "Memory Initialize Failed.");
		loop HALT();
	}
	//{} Cache_t::enAble();
	Taskman::Initialize();
	Filesys::Initialize();

	IC.Reset();
	// UART0
	UART0.enInterrupt();
	UART0.setInterruptPriority(1, nil);
	VTTY_Append((&Console));

	if constexpr(0) {
		plogwarn("levstack offset size:%u addr:%u", offsetof(ThreadBlock, stack_size), offsetof(ThreadBlock, stack_levladdr));
	}
	Taskman::Create((void*)&serv_task_loop, RING_M);
	Taskman::Create((void*)&serv_cons_loop, RING_M);
	Taskman::Create((void*)&serv_graf_loop, RING_M);
	Taskman::Create((void*)&serv_file_loop, RING_M);
	//
	Taskman::Create((void*)&serv_dev_mem_loop, RING_M);

	if (Taskman::chain.Count() <= 1) {
		ploginfo("Nothing to do.");
		HALT();//{} shutdown
	}
	IC.enAble(true);
	// CLINT Clock
	last_schepoint = clint.Read() + TIMER_INTERVAL;
	clint.Load(getMHARTID(), last_schepoint);
	clint.enInterrupt();
	while (1) {
		// UART0.OutFormat("Task K: Running...\n");
		clint.MSIP(getMHARTID(), MSIP_Type::SofRupt);// Bad Method: Taskman::Schedule(true)
		HALT();
	}
}
