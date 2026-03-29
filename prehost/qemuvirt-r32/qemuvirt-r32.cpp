// ASCII GAS/RISCV64 TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#include <cpp/Device/UART>
#include <c/driver/timer.h>

OstreamTrait* con0_out = &UART0;

void user_task0(void);
void user_task1(void);

constexpr inline static stduint operator "" Baud(unsigned long long i) { return i; }

#define TIMER_INTERVAL (CLINT_TIMEBASE_FREQ/100) // 100Hz

_ESYM_C
void _entry()
{
	//[!] cannot use FLOATING operation (why)
	UART0.setMode(115200Baud);
	ploginfo("Hello Mcca-RV%u~ =OwO=", __BITS__);
	if (!Memory::initialize('ANIF', NULL)) {
		printlog(_LOG_FATAL, "Memory Initialize Failed.");
		loop HALT();
	}
	const unsigned mempool_lenN = 0x20000;
	mempool.Append(Slice{ _IMM(mem.allocate(mempool_lenN)), mempool_lenN });
	//{} Cache_t::enAble();
	Taskman::Initialize();

	IC.Reset();
	// UART0
	UART0.enInterrupt();
	UART0.setInterruptPriority(1, nil);
	// CLINT Clock
	last_schepoint = clint.Read() + TIMER_INTERVAL;
	clint.Load(getMHARTID(), last_schepoint);
	clint.enInterrupt();
	
	Taskman::Create((void*)user_task0, 0);
	Taskman::Create((void*)user_task1, 0);

	IC.enAble(true);
	while (1) {
		UART0.OutFormat("Task K: Running...\n");
		clint.MSIP(getMHARTID(), MSIP_Type::SofRupt);// Bad Method: Taskman::Schedule(true)
		HALT();
	}
}

void task_yield()
{
	clint.MSIP(getMHARTID(), MSIP_Type::SofRupt);
}

void task_delay(volatile int count)
{
	count *= 50000;
	while (count--);
}

// ---- ---- USER BEG ---- ----


#define DELAY 4000

_ESYM_C
stduint gethid(unsigned int* ptr_hid);
void user_task0(void)
{
	unsigned int hid = -1;
	UART0.OutFormat("Task 0: Created!\n");
	// task_yield(); <--- seem a privileged code
	UART0.OutFormat("Task 0: Yield Back!\n");
	int ret = -1;
	ret = gethid(&hid);
	if (!ret) {
		UART0.OutFormat("system call returned!, hart id is %d\n", hid);
	} else {
		UART0.OutFormat("gethid() failed, return: %d\n", ret);
	}
	while (1) {
		UART0.OutFormat("Task 0: Running...\n");
		task_delay(DELAY);
	}
}

void user_task1(void)
{
	UART0.OutFormat("Task 1: Created!\n");
	while (1) {
		UART0.OutFormat("Task 1: Running...\n");
		task_delay(DELAY);
	}
}

// ---- ---- USER END ---- ----

static const byte _FOLLOW_VHD[] = {
#embed "qemuvirt.hs"
};
static_assert(sizeof(_FOLLOW_VHD) > 0);

extern "C" void __cxa_pure_virtual() {
	while (1);
}
extern "C" void *memset(void *str, int c, size_t n) { return MemSet(str, c, n); }
_ESYM_C unsigned int __ctzsi2(unsigned int x)
{
	if (x == 0) return 32;
	unsigned int n = 0;
	while ((x & 1) == 0) {
		n++;
		x >>= 1;
	}
	return n;
}
_ESYM_C unsigned int __ctzdi2(unsigned long x)
{
	if (x == 0) return 64;
	unsigned int n = 0;
	while ((x & 1) == 0) {
		n++;
		x >>= 1;
	}
	return n;
}