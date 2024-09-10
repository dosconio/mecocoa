// ASCII C++ TAB4 LF
// Attribute: bits(64)
// LastCheck: 20240523
// AllAuthor: @dosconio
// ModuTitle: Routine for Mecocoa riscv-64
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#ifndef _OPT_RISCV64
#define _OPT_RISCV64
#endif

#include "../mecocoa/routine/rout64.h"
#include "appload-riscv64.h"
#include "trap.h"
#include "logging.h"
#include "rustsbi.h"
#include <c/consio.h>

uint64 sys_write(int fd, char *str, unsigned len)
{
	// debugf("sys_write fd = %d str = %x, len = %d", fd, str, len);
	if (fd == _STD_OUT)
		outtxt(str, len);
	else len = 0;
	return len;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	log_info("sysexit(%d)", code);
	run_next_app();
	outs("Oyasminasaiii~\n");
	shutdown();
	__builtin_unreachable();
}

extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = (struct trapframe *)trap_page;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	// tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	//        args[1], args[2], args[3], args[4], args[5]);
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], (char *)args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	default:
		ret = -1;
		log_panic("Unknown syscall", 0);
		while (1);
	}
	trapframe->a0 = ret;
}


