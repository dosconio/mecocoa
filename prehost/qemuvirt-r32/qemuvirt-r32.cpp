// ASCII GAS/RISCV64 TAB4 LF
// Attribute: 
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include <c/stdinc.h>
#include <c/proctrl/RISCV/riscv.h>
#include "../../include/qemuvirt-riscv.hpp"
//
#include <cpp/Device/UART>

using namespace uni;

int task_create(void (*start_routin)());
void schedule();
void user_task0(void);
void user_task1(void);

extern "C"
void _entry()
{
	_preprocess();
	//[!] cannot use FLOATING operation (why)
	UART0.setMode(115200);
	UART0.OutFormat("Hello Mcca-RV%u~ =OwO=\n", __BITS__);

	setMSCRATCH(0);
	task_create(user_task0);
	task_create(user_task1);
	schedule();

	while (1);
}

_ESYM_C void switch_to(TaskContext *next);

#define MAX_TASKS 10
#define STACK_SIZE 1024
/*
 * In the standard RISC-V calling convention, the stack pointer sp
 * is always 16-byte aligned.
 */
uint8_t __attribute__((aligned(16))) task_stack[MAX_TASKS][STACK_SIZE];
TaskContext ctx_tasks[MAX_TASKS];

static int _top = 0;
static int _current = -1;

void schedule()
{
	if (_top <= 0) {
		return;
	}
	_current = (_current + 1) % _top;
	TaskContext *next = &(ctx_tasks[_current]);
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
int task_create(void (*start_routin)(void))
{
	if (_top < MAX_TASKS) {
		ctx_tasks[_top].sp = (stduint) &task_stack[_top][STACK_SIZE];
		ctx_tasks[_top].ra = (stduint) start_routin;
		_top++;
		return 0;
	} else {
		return -1;
	}
}

void task_yield()
{
	schedule();
}

void task_delay(volatile int count)
{
	count *= 50000;
	while (count--);
}

#define DELAY 4000

void user_task0(void)
{
	UART0.OutFormat("Task 0: Created!\n");
	while (1) {
		UART0.OutFormat("Task 0: Running...\n");
		task_delay(DELAY);
		task_yield();
	}
}

void user_task1(void)
{
	UART0.OutFormat("Task 1: Created!\n");
	while (1) {
		UART0.OutFormat("Task 1: Running...\n");
		task_delay(DELAY);
		task_yield();
	}
}

extern "C" { void* __dso_handle = 0; }
extern "C" { void __cxa_atexit(void) {} }
extern "C" { void __gxx_personality_v0(void) {} }
extern "C" { void __stack_chk_fail(void) {} }

extern "C" void __cxa_pure_virtual() {
	while (1);
}
extern "C" void *memset(void *str, int c, size_t n) { return MemSet(str, c, n); }
