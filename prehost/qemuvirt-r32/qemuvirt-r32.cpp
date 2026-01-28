// ASCII GAS/RISCV64 TAB4 LF
// Attribute: 
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include <c/stdinc.h>
#include <c/proctrl/RISCV/riscv.h>
#include "../../include/qemuvirt-riscv.hpp"
//
#include <cpp/interrupt>
#include <cpp/Device/UART>
#include <c/driver/timer.h>

using namespace uni;

int task_create(void (*start_routin)());
void schedule();
void user_task0(void);
void user_task1(void);

_ESYM_C void trap_vector();
void external_interrupt_handler();
extern void timer_handler(void);
void syscall(TaskContext* cxt);

_ESYM_C
stduint trap_handler(stduint epc, stduint cause, TaskContext* cxt)
{
	stduint return_pc = epc;
	stduint cause_code = cause & MCAUSE_MASK_ECODE;
	if (cause & MCAUSE_MASK_INTERRUPT) {
		/* Asynchronous trap - interrupt */
		switch (cause_code) {
		case 3:
			ploginfo("software interruption!\n");
			clint.MSIP(getMHARTID(), MSIP_Type::AckRupt);
			schedule();
			break;
		case 7:
			ploginfo("timer interruption!");
			timer_handler();
			break;
		case 11:
			ploginfo("external interruption!");
			external_interrupt_handler();
			break;
		default:
			plogwarn("Unknown async exception! Code = 0x%[x]", cause_code);
			break;
		}
	}
	else { // Synchronous trap - exception
		switch (cause_code) {
		case 8:
			ploginfo("System call from U-mode!\n");
			syscall(cxt);
			return_pc += 4;
			break;
		default:
			plogerro("Sync exceptions! Code = 0x%[x]", cause_code);
			//return_pc += sizeof(stduint);// if skip the instruction which cause the exception
			// Test: *(int *)0x00000000 = 100; // #7 Store/AMO access fault
			// Test: a = *(int *)0x00000000;   // #5 Load access fault
			while (1);
		}
	}
	return return_pc;
}

void external_interrupt_handler()
{
	Request_t irq = InterruptControl::getLastRequest();
	if (irq == IRQ_UART0) {
		int ch;
		UART0 >> ch;
		UART0 << ch;
	}
	else if (irq) {
		plogerro("not considered interrupt %d\n", irq);
	}
	if (irq) {
		InterruptControl::setLastRequest(irq);
	}
}

uint64 _tick;
uint64 last_schepoint;
void timer_handler()
{
	_tick++;
	UART0.OutFormat("tick: %d\n", _tick);
	last_schepoint += TIMER_INTERVAL;
	clint.Load(getMHARTID(), last_schepoint);
	schedule();
}

// syscall

#define SYS_GETCOREID	1

int sys_gethid(unsigned int *ptr_hid)
{
	UART0.OutFormat("--> sys_gethid, arg0 = %[x]\n", ptr_hid);
	if (ptr_hid == NULL) {
		return -1;
	} else {
		*ptr_hid = getMHARTID();
		return 0;
	}
}

void syscall(TaskContext* cxt)
{
	uint32_t syscall_num = cxt->a7;

	switch (syscall_num) {
	case SYS_GETCOREID:
		cxt->a0 = sys_gethid((unsigned int *)(cxt->a0));
		break;
	default:
		UART0.OutFormat("Unknown syscall no: %d\n", syscall_num);
		cxt->a0 = -1;
	}

	return;
}


extern "C"
void _entry()
{
	//[!] cannot use FLOATING operation (why)
	UART0.setMode(115200);
	ploginfo("Hello Mcca-RV%u~ =OwO=", __BITS__);

	// Interrupt
	InterruptControl PLIC _IMM(trap_vector);
	PLIC.Reset();

	UART0.enInterrupt();
	UART0.setInterruptPriority(1, nil);

	PLIC.enAble(true);

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
_ESYM_C
void outtxt(const char* str, stduint len) {
	for0(i, len) UART0.OutChar(str[i]);
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
		ctx_tasks[_top].pc = (stduint) start_routin;
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

extern "C" { void* __dso_handle = 0; }
extern "C" { void __cxa_atexit(void) {} }
extern "C" { void __gxx_personality_v0(void) {} }
extern "C" { void __stack_chk_fail(void) {} }

extern "C" void __cxa_pure_virtual() {
	while (1);
}
extern "C" void *memset(void *str, int c, size_t n) { return MemSet(str, c, n); }
