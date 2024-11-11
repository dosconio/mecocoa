// ASCII C++ TAB4 LF
// LastCheck: 20240523
// AllAuthor: @dosconio
// ModuTitle: Trap for Mecocoa riscv-64
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#ifndef _OPT_RISCV64
#define _OPT_RISCV64
#endif

#include "trap.h"
#include "logging.h"
#include "appload-riscv64.h"
#include "process-riscv64.h"
#include "kernel-riscv64.h"
#include "../mecocoa/routine/rout64.h"

#define PG_SIZE 4096 // bytes per page
#define PG_SHIFT 12 // bits of offset within a page
#define PG_ROUNDUP(sz) (((sz) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PG_ROUNDDOWN(a) (((a)) & ~(PGSIZE - 1))

extern char trampoline[], uservec[];
extern void *userret(uint64);

#define set_usertrap() setSTVEC((uint64)uservec & ~0x3)
#define set_krnltrap() setSTVEC((uint64)kerneltrap & ~0x3)

void kerneltrap()
{
	if ((getSSTATUS() & _SSTATUS_SPP) == 0)
		log_panic("kerneltrap: not from supervisor mode", 0);
	log_panic("trap from kernel\n", 0);
}

// set up to take exceptions and traps while in the kernel.
void trap_init(void)
{
	set_krnltrap();
}

void unknown_trap()
{
	log_error("unknown trap: %p, stval = %p\n", getSCAUSE(), getSTVAL());
	exit(-1);
}


// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
void usertrap()
{
	set_krnltrap();
	struct trapframe* trapframe = curr_proc()->trapframe;
	if ((getSSTATUS() & _SSTATUS_SPP) != 0)
		log_panic("usertrap: not from user mode", 0);

	uint64 cause = getSCAUSE();
	if (cause & (1ULL << 63)) {
		cause &= ~(1ULL << 63);//{TOPT} To optimize/beautify
		switch (cause) {
		case SupervisorTimer:
			if (nil) log_trace("time interrupt!\n", 0);
			set_next_timer();
			yield();
			break;
		default:
			unknown_trap();
			break;
		}
	}
	else switch (cause) {
	case InstructionMisaligned:
	case LoadMisaligned:
	case StoreMisaligned:
	case InstructionPageFault:
	case LoadPageFault:
	case StorePageFault:
		log_error("%d in application, bad addr = %p, bad instruction = %p, core dumped.", cause, getSTVAL(), trapframe->epc);
		exit(-2);
		break;
	case UserEnvCall:
		trapframe->epc += 4;
		syscall();
		break;
	case IllegalInstruction:
		log_error("IllegalInstruction in application, epc = %p, core dumped.",
			trapframe->epc);
		exit(-3);
		break;
	default:
		unknown_trap();
		break;
	}
	ReturnTrapFromU();
}

//
// return to user space
//
// old: struct trapframe *trapframe, stduint kstack
void ReturnTrapFromU()
{
	set_usertrap();
	struct trapframe* trapframe = curr_proc()->trapframe;
	trapframe->kernel_satp = getSATP(); // kernel page table
	trapframe->kernel_sp = curr_proc()->kstack + PG_SIZE; // process's kernel stack
	trapframe->kernel_trap = (uint64)usertrap;
	trapframe->kernel_hartid = getTP(); // hartid for cpuid()

	setSEPC(trapframe->epc);
	// set up the registers that trampoline.S's sret will use
	// to get to user space.

	// set S Previous Privilege mode to User.
	uint64 x = getSSTATUS();
	x &= ~ _SSTATUS_SPP; // clear SPP to 0 for user mode
	x |= _SSTATUS_SPIE; // enable interrupts in user mode
	setSSTATUS(x);

	// tell trampoline.S the user page table to switch to.
	// uint64 satp = MAKE_SATP(p->pagetable);
	userret((uint64)trapframe);
}
