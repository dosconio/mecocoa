// ASCII C/C++ TAB4 CRLF
// LastCheck: 20241020
// AllAuthor: @dosconio
// ModuTitle: Process
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "process-riscv64.h"
#include "appload-riscv64.h"
#include "trap.h"
#include "logging.h"
#include <c/ustring.h>

struct proc pool[NPROC];
char kstack[NPROC][PAGE_SIZE];
_ALIGN(4096) char ustack[NPROC][PAGE_SIZE];
_ALIGN(4096) char trapframe[NPROC][PAGE_SIZE];

extern char boot_stack_top[];
struct proc* current_proc;
struct proc idle;

int threadid()
{
	struct proc* p = curr_proc();
	return p ? p->pid : 0;
}

struct proc* curr_proc()
{
	return current_proc;
}

// initialize the proc table at boot time.
void proc_init(void)
{
	struct proc* p;
	for (p = pool; p < &pool[NPROC]; p++) {
		p->state = UNUSED;
		p->kstack = (uint64)kstack[p - pool];
		p->ustack = (uint64)ustack[p - pool];
		p->trapframe = (struct trapframe*)trapframe[p - pool];
		/*
		* LAB1: you may need to initialize your new fields of proc here
		*/
	}
	idle.kstack = (uint64)boot_stack_top;
	idle.pid = 0;
	current_proc = &idle;
}

int allocpid()
{
	static int PID = 1;
	return PID++;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel.
// If there are no free procs, or a memory allocation fails, return 0.
struct proc* allocproc(void)
{
	struct proc* p;
	for (p = pool; p < &pool[NPROC]; p++) {
		if (p->state == UNUSED) {
			goto found;
		}
	}
	return 0;

found:
	p->pid = allocpid();
	p->state = USED;
	MemSet(&p->context, 0, sizeof(p->context));
	MemSet(p->trapframe, 0, PAGE_SIZE);
	MemSet((void*)p->kstack, 0, PAGE_SIZE);
	p->context.ra = (uint64)ReturnTrapFromU;
	p->context.sp = p->kstack + PAGE_SIZE;
	return p;
}

// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void scheduler(void)
{
	struct proc* p;
	for (;;) {
		for (p = pool; p < &pool[NPROC]; p++) {
			if (p->state == RUNNABLE) {
				/*
				* LAB1: you may need to init proc start time here
				*/
				p->state = RUNNING;
				current_proc = p;
				swtch(&idle.context, &p->context);
			}
		}
	}
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void sched(void)
{
	struct proc* p = curr_proc();
	if (p->state == RUNNING)
		log_panic("sched running", 0);
	swtch(&p->context, &idle.context);
}

// Give up the CPU for one scheduling round.
void yield(void)
{
	current_proc->state = RUNNABLE;
	sched();
}

// Exit the current process.
void exit(int code)
{
	struct proc* p = curr_proc();
	log_info("proc %d exit with %d", p->pid, code);
	p->state = UNUSED;
	finished();
	sched();
}

