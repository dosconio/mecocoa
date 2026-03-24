// ASCII GAS/RISCV64 TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#include <cpp/Device/UART>
#include <c/driver/timer.h>

OstreamTrait* con0_out = &UART0;

int task_create(void (*start_routin)(), int ring = 0/*0u 1s 3m*/);
void schedule();
void user_task0(void);
void user_task1(void);


_ESYM_C
void _entry()
{
	//[!] cannot use FLOATING operation (why)
	UART0.setMode(115200);
	ploginfo("Hello Mcca-RV%u~ =OwO=", __BITS__);
	if (!Memory::initialize('ANIF', NULL)) {
		printlog(_LOG_FATAL, "Memory Initialize Failed.");
		loop HALT();
	}

	// Interrupt
	IC.Reset();

	UART0.enInterrupt();
	UART0.setInterruptPriority(1, nil);

	IC.enAble(true);

	last_schepoint = clint.Read() + TIMER_INTERVAL;
	clint.Load(getMHARTID(), last_schepoint);
	clint.enInterrupt();

	// Task
	setMSCRATCH(0);
	setMIE(getMIE() | _MIE_MSIE);// software interrupts
	task_create(user_task0);
	task_create(user_task1);
	schedule();

	while (1);
}

_ESYM_C void switch_to(NormalTaskContext *next);

#define MAX_TASKS 10
#define STACK_SIZE 1024
/*
 * In the standard RISC-V calling convention, the stack pointer sp
 * is always 16-byte aligned.
 */
uint8_t __attribute__((aligned(16))) task_stack[MAX_TASKS][STACK_SIZE];
NormalTaskContext ctx_tasks[MAX_TASKS];

static int _top = 0;
static int _current = -1;

void schedule()
{
	if (_top <= 0) {
		return;
	}
	_current = (_current + 1) % _top;
	NormalTaskContext *next = &(ctx_tasks[_current]);
	switch_to(next);
}

/*
 * DESCRIPTION
 * 	Create a task.
 * 	- start_routin: task routine entry
 * RETURN VALUE
 * 	0: success
 * 	-1: if error occured
 */
int task_create(void (*start_routin)(void), int ring)
{
	if (_top < MAX_TASKS) {
		ctx_tasks[_top].sp = (stduint) &task_stack[_top][STACK_SIZE];
		ctx_tasks[_top].mepc = (stduint)start_routin;
		ctx_tasks[_top].mstatus = (ring << 11) | _MSTATUS_MPIE;// 0 or 1 or 3
		// ctx->satp = 0;
		_top++;
		return 0;
	} else {
		return -1;
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