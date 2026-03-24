// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <cpp/interrupt>
#include <cpp/Device/UART>
#include <c/driver/mouse.h>
#include <c/driver/timer.h>

#if _MCCA == 0x8664 && defined(_UEFI)
InterruptControl IC = { nil };
#elif (_MCCA & 0xFF00) == 0x8600
InterruptControl IC = { mglb(0x800) };
#elif (_MCCA & 0xFF00) == 0x1000
_ESYM_C Handler_t trap_vector;
InterruptControl IC = { _IMM(&trap_vector) };
#endif

#ifdef _ARC_x86 // x86:

extern KeyboardBridge kbdbridge;
extern uni::Queue<SysMessage> message_queue_conv;
static bool fa_mouse = false;
static byte mouse_buf[4] = { 0 };
QueueLimited* queue_mouse;
void Handint_KBD() {
	kbdbridge.OutChar(innpb(PORT_KEYBOARD_DAT));
}
static void process_mouse(byte ch) {
	mouse_buf[mouse_buf[3]++] = ch;
	mouse_buf[3] %= 3;
	MouseMessage& mm = *(MouseMessage*)mouse_buf;
	if (!mouse_buf[3]) {
		SysMessage smsg{
			.type = SysMessage::RUPT_MOUSE,
		};
		mm.Y = -mm.Y;
		smsg.args.mou_event = mm,
		message_queue_conv.Enqueue(smsg);
	}
	else if (mouse_buf[3] == 1) {
		if (!mm.HIGH) mouse_buf[3] = 0;
	}
}
void Handint_MOU() {
	byte state = innpb(PORT_KEYBOARD_CMD);
	if (state & 0x20); else return;//{} check AUX, give KBD
	byte ch = innpb(PORT_KEYBOARD_DAT);
	// if (ch != (byte)0xFA)// 0xFA is ready signal
	if (!fa_mouse && ch == (byte)0xFA) {
		fa_mouse = true;
		return;
	}
	process_mouse(ch);// asserv(queue_mouse)->OutChar(ch);
	while ((innpb(PORT_KEYBOARD_CMD) & 0x21) == 0x21)// 0x01 for OBF, 0x20 for AUX
	{
		process_mouse(innpb(PORT_KEYBOARD_DAT));
	}
}

// void Handint_HDD()...

#elif defined(_UEFI)

__attribute__((target("general-regs-only"), optimize("O0")))
void Handint_XHCI() {
	message_queue.Enqueue(SysMessage{ SysMessage::RUPT_xHCI });
	sendEOI();
}

#endif



// ---- ---- ---- ---- EXCEPTION ---- ---- ---- ---- //

#if (_MCCA & 0xFF00) == 0x8600
static rostr ExceptionDescription[] = {
	"#DE Divide Error",
	"#DB Step",
	"#NMI",
	"#BP Breakpoint",
	"#OF Overflow",
	"#BR BOUND Range Exceeded",
	"#UD Invalid Opcode",
	"#NM Device Not Available",
	"#DF Double Fault",
	"#   Cooperative Processor Segment Overrun",
	"#TS Invalid TSS",
	"#NP Segment Not Present",
	"#SS Stack-Segment Fault",
	"#GP General Protection",
	"#PF Page Fault",
	"#   (Intel reserved)",
	// 0x10
	"#MF x87 FPU Floating-Point Error",
	"#AC Alignment Check",
	"#MC Machine-Check",
	"#XM SIMD Floating-Point Exception"
};
#endif


#if (_MCCA & 0xFF00) == 0x8600

_ESYM_C
__attribute__((target("general-regs-only"), optimize("O0")))
void exception_handler(sdword iden, dword para) {
	stduint r15;
	#if _MCCA == 0x8664
	_ASM("mov %%r15, %0" : "=r"(r15));
	#else
	_ASM("mov %%edi, %0" : "=r"(r15));
	#endif
	const bool have_para = iden >= 0;
	if (iden < 0) iden = ~iden;

	dword tmp;
	if (iden >= 0x20)
		printlog(_LOG_FATAL, "#ELSE %x %x", iden, para);
	switch (iden) {
	case ERQ_Invalid_Opcode:// 6
	{
		// first #UD is for TEST
		static bool first_done = false;
		if (!first_done) {
			first_done = true;
			rostr test_page = (rostr)"\xFF\x70[Mecocoa]\xFF\x27 Exception #UD Test OK!\xFF\x07";
			outsfmt("%s\n\r", test_page);
		}
		else {
			printlog(_LOG_FATAL, " %s", ExceptionDescription[iden]);// no-para
			__asm("cli; hlt");
		}
		break;
	}

	case ERQ_Coprocessor_Not_Available:// 7
		// needed by jmp-TSS method
		if (!(getCR0() & 0b1110)) {
			printlog(_LOG_FATAL, " %s", ExceptionDescription[iden]);// no-para
		}
		EnableSSE();
		break;

	case ERQ_Page_Fault:// 14
		printlog(_LOG_FATAL, have_para ? "%s with 0x%[x], vaddr=0x%[x], CR3=0x%[x]" : "%s",
			ExceptionDescription[iden], para, getCR2(), r15); // printlog will call halt machine
		break;

	default:
		printlog(_LOG_FATAL, have_para ? "%s with 0x%[32H]" : "%s",
			ExceptionDescription[iden], para); // printlog will call halt machine
		__asm("cli; hlt");
		break;
	}
}

// No dynamic core

#elif (_MCCA & 0xFF00) == 0x1000// RV


void external_interrupt_handler();
void schedule();

_ESYM_C
stduint trap_handler(stduint epc, stduint cause, NormalTaskContext* cxt)
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
			cxt->mepc += 4;
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



#elif defined(_MPU_STM32MP13)// Thumb


_ESYM_C
void Default_Handler(void) {
	erro("");
}

// Internal References
_ESYM_C void Vectors(void) __attribute__((naked, section("RESET")));
_ESYM_C void Reset_Handler(void) __attribute__((naked, target("arm")));

// ExcRupt Handler
_ESYM_C void Undef_Handler(void) __attribute__((weak, alias("Default_Handler")));
_ESYM_C void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
_ESYM_C void PAbt_Handler(void) __attribute__((weak, alias("Default_Handler")));
_ESYM_C void DAbt_Handler(void) __attribute__((weak, alias("Default_Handler")));
_ESYM_C void Rsvd_Handler(void) __attribute__((weak, alias("Default_Handler")));
_ESYM_C void IRQ_Handler(void) __attribute__((weak, alias("Default_Handler")));
_ESYM_C void FIQ_Handler(void) __attribute__((weak, alias("Default_Handler")));

_ESYM_C
void Vectors(void) {
	__asm__ volatile(
		".align 7                                         \n"
		"LDR    PC, =Reset_Handler                        \n"
		"LDR    PC, =Undef_Handler                        \n"
		"LDR    PC, =SVC_Handler                          \n"
		"LDR    PC, =PAbt_Handler                         \n"
		"LDR    PC, =DAbt_Handler                         \n"
		"LDR    PC, =Rsvd_Handler                         \n"
		"LDR    PC, =IRQ_Handler                          \n"
		"LDR    PC, =FIQ_Handler                          \n"
		);
}

#endif

