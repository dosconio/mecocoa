// ASCII C++ TAB4 LF
// LastCheck: 20240523
// AllAuthor: @dosconio
// ModuTitle: Trap for Mecocoa riscv-64
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "trap.h"
#include "log.h"
#include "appload-riscv64.h"
#include "rout64-riscv64.h"

#define PG_SIZE 4096 // bytes per page
#define PG_SHIFT 12 // bits of offset within a page
#define PG_ROUNDUP(sz) (((sz) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PG_ROUNDDOWN(a) (((a)) & ~(PGSIZE - 1))

extern char trampoline[], uservec[], boot_stack_top[];
extern void *userret(uint64);

// set up to take exceptions and traps while in the kernel.
void trap_init(void)
{
	setSTVEC((uint64)uservec & ~0x3);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void usertrap(struct trapframe *trapframe)
{
	if ((getSSTATUS() & _SSTATUS_SPP) != 0)
		log_panic("usertrap: not from user mode");

	uint64 cause = getSCAUSE();
	if (cause == UserEnvCall) {
		trapframe->epc += 4;
		syscall();
		return usertrapret(trapframe, (uint64)boot_stack_top);
	}
	switch (cause) {
	case StoreMisaligned:
	case StorePageFault:
	case LoadMisaligned:
	case LoadPageFault:
	case InstructionMisaligned:
	case InstructionPageFault:
		log_error("%d in application, bad addr = %p, bad instruction = %p, core "
		       "dumped.",
		       cause, getSTVAL(), trapframe->epc);
		break;
	case IllegalInstruction:
		log_error("IllegalInstruction in application, epc = %p, core dumped.",
		       trapframe->epc);
		break;
	default:
		log_error("unknown trap: %p, stval = %p sepc = %p", getSCAUSE(),
		       getSTVAL(), getSEPC());
		break;
	}
	log_info("switch to next app");
	run_next_app();
	outs("ALL DONE\n");
	shutdown();
}

//
// return to user space
//
void usertrapret(struct trapframe *trapframe, uint64 kstack)
{
	trapframe->kernel_satp = getSATP(); // kernel page table
	trapframe->kernel_sp = kstack + PG_SIZE; // process's kernel stack
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
