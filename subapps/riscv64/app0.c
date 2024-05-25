//#define _OPT_RISCV64
#include <c/stdinc.h>
#include <c/proctrl/RISCV/riscv64.h>
#include "../../userkit/inc/cocoapp.h"
#include "../../userkit/inc/stdio.h"

int main()
{
	int make_fail = 0;
	puts("[subapp0] Yahoo");
	switch (make_fail)
	{
	case 1:
		*(int *)0 = 0;
		break;
	case 2:
		asm volatile("sret");
		break;
	case 3:
		getSSTATUS();
		break;
	default:
		break;
	}
	exit(0x12);
	return 0;
}