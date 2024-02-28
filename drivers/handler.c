// ASCII C TAB4 LF
// Attribute: Bits(32)
// LastCheck: 20240221
// AllAuthor: @dosconio
// ModuTitle: Handler
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../cokasha/kernel.h"
#include "../include/conio32.h"
#include <x86/x86.h>

static char *ExceptionDescription[] = 
{
	"#DE Divide Error",
	"#DB Step (reserved)",
	"#NMI",
	"#BP Breakpoint",
	"#OF Overflow",
	"#BR BOUND Range Exceeded",
	"#UD Invalid Opcode",
	"#NM Device Not Available",
	"#DF Double Fault",
	"Cooperative Processor Segment Overrun (reserved)",
	"#TS Invalid TSS",
	"#NP Segment Not Present",
	"#SS Stack-Segment Fault",
	"#GP General Protection",
	"#PF Page Fault",
	"(Intel reserved)",
	"#MF x87 FPU Floating-Point Error (reserved)",
	"#AC Alignment Check",
	"#MC Machine-Check",
	"#XF SIMD Floating-Point Exception"
};

void Handexc(sdword iden, dword para)
{
	if (iden >= 0x20)
		outs("#ELSE");
	else if (iden < 0)// do not have para
	{
		iden =~ iden;
		outs(ExceptionDescription[iden]);
	}
	else
	{
		outs(ExceptionDescription[iden]);
		outs(" : ");
		outi32hex(para);	
	}
	outs("\x0A\x0D");
}
