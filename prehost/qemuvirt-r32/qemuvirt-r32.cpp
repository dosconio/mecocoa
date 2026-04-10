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

static const byte _FOLLOW_VHD[] = {
	#if __BITS__ == 32
	#embed "fatvhd.ignore"
	#elif __BITS__ == 64
	#embed "../qemuvirt-r64/fatvhd.ignore"
	#endif
};
static_assert(sizeof(_FOLLOW_VHD) > 0);

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

	IC.Reset();
	// UART0
	UART0.enInterrupt();
	UART0.setInterruptPriority(1, nil);
	VTTY_Append((&Console));

	if constexpr(0) {
		plogwarn("stack_size=%u", offsetof(ProcessBlock, stack_size));
		plogwarn("stack_levladdr=%u", offsetof(ProcessBlock, stack_levladdr));
	}
	Taskman::Create((void*)&serv_task_loop, RING_M);
	Taskman::Create((void*)&serv_cons_loop, RING_M);

	ploginfo("FATVHD Size: %[x]", sizeof(_FOLLOW_VHD));
	if (1) {
		auto memdev_buffer = new byte[512];
		auto fat_buffer = new byte[512];
		MemoryBlockDevice memdev({ _IMM(_FOLLOW_VHD), sizeof(_FOLLOW_VHD) }, memdev_buffer);
		FilesysFAT fatvhd(32, memdev, fat_buffer, NULL);
		fatvhd.buffer_fatable = new byte[512];
		FAT_FileHandle* han;
		FAT_FileHandle filhan;
		stduint a[2] = { _IMM(&filhan), 0/*, _IMM(&filinf) */ };
		if (!fatvhd.loadfs()) {
			plogerro("FATVHD loadfs failed");
		}
		else {
			han = (FAT_FileHandle*)fatvhd.search("/", &a);
			fatvhd.enumer(han, NULL);
			if (han = (FAT_FileHandle*)fatvhd.search("lpa.elf", &a)) {
				FileBlockBridge loop_device(&fatvhd, han, han->size, 512);
				// plogwarn("size: %[x]", han->size);
				if (auto pb = Taskman::CreateELF(&loop_device, RING_U)) {
					Taskman::Append(pb);
				}
				else plogerro("lpa.elf: Fail to parse or load ELF");
			}
			else plogerro("lpa.elf: Not found");
		}
		delete[](fatvhd.buffer_fatable);
		delete[](memdev_buffer);
		delete[](fat_buffer);
	}

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

void DiscPartition::renew_slice() {}
