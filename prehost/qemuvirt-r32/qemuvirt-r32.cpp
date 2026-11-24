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

#define TIMER_INTERVAL (CLINT_TIMEBASE_FREQ / CONFIG_SysTickFreq)


extern uint64 last_schepoint;

_ESYM_C void R_MEMDISK_INIT();
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
	Systime::Initialize();
	Filesys::Initialize();
	R_MEMDISK_INIT();
	Taskman::Initialize();
	Devsman::Initialize();

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
	Taskman::Create((void*)&serv_devs_loop, RING_M);

	if (Taskman::chain.Count() <= 1) {
		ploginfo("Nothing to do.");
		HALT();//{} shutdown
	}
	IC.enInterrupt(true);
	// CLINT Clock
	last_schepoint = clint.Read() + TIMER_INTERVAL;
	clint.Load(getMHARTID(), last_schepoint);
	clint.enInterrupt();

	//{TODO} move to timer-systick.cpp
	if (auto* node = Devsman::RegisterPlatformDevice("clint@2000000", "riscv-clint")) {
		Devsman::AddMemoryResource(node, 0, ADDR_CLINT_BASE, 0x10000);
		TimeDevice device{};
		device.info.periodic_event = true;
		device.info.event_frequency = Systime::GetTickFrequency();
		device.info.readable_counter = true;
		device.info.counter_frequency = CLINT_TIMEBASE_FREQ;
		device.info.counter_bits = 64;
		device.ReadCounter = [](void*, uint64& value) -> bool {
			// RISC-V Privileged Architecture: mtime needs a consistent high/low snapshot on RV32.
			auto* counter = reinterpret_cast<volatile uint32*>(ADDR_CLINT_MTIME);
			uint32 high, low;
			do { high = counter[1]; low = counter[0]; } while (high != counter[1]);
			value = (uint64(high) << 32) | low;
			return true;
		};
		if (!Systime::AttachDevice(node, device) || !Systime::BindTickDevice(node))
			plogerro("[CLINT] cannot attach time capability");
	}

	while (1) {
		// UART0.OutFormat("Task K: Running...\n");
		clint.MSIP(getMHARTID(), MSIP_Type::SofRupt);// Bad Method: Taskman::Schedule(true)
		HALT();
	}
}
