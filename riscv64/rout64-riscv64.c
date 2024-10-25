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
#include "kernel-riscv64.h"
#include "process-riscv64.h"

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
	// log_info("sysexit(%d)", code);
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(timeval_t* val, int _tz)
{
	uint64 cycle = get_cycle();
	val->sec = cycle / CPU_FREQ;
	val->mic = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	return 0;
}

;//{TODO} uC LAB1: need to define SYS_task_info here




extern char trap_page[];

void syscall()
{
	struct trapframe* trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	// log_trace("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	//        args[1], args[2], args[3], args[4], args[5]);
	//{TODO} uC LAB1: need to define SYS_task_info here
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], (char *)args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((timeval_t*)args[0], args[1]);
		break;
	//{TODO} uC LAB1: need to define SYS_task_info here
	default:
		ret = -1;
		log_panic("Unknown syscall", 0);
		while (1);
	}
	trapframe->a0 = ret;
}


