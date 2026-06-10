// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Handler and Soft Timer
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <c/delay.h>
#include <cpp/Device/UART>
#include <c/driver/mouse.h>
#include <c/driver/timer.h>
#if _MCCA == 0x8632
extern "C" volatile uint32 ap_ring3_iret_guard_hits;
extern "C" volatile uint32 ap_ring3_iret_last_lapicid;
extern "C" volatile uint32 ap_ring3_iret_last_coreid;
#endif

#if _MCCA == 0x8664 && defined(_UEFI)
InterruptControl IC = { nil };
#elif (_MCCA & 0xFF00) == 0x8600
InterruptControl IC = { mglb(0x800) };
#elif (_MCCA & 0xFF00) == 0x1000
_ESYM_C Handler_t trap_vector[];
InterruptControl IC = { _IMM(trap_vector) };
#endif

// Unified interrupt handler array
Handler_t interrupt_handlers[256] = { nullptr };

#include <c/ISO_IEC_STD/signal.h>
extern "C" void check_and_deliver_signals(void* context);

#if (_MCCA & 0xFF00) == 0x8600
extern "C" void exception_handler(HardwareInterruptFrame* frame);

// Unified interrupt dispatcher called from assembly stubs
extern "C" void interrupt_dispatcher(HardwareInterruptFrame* frame) {
	stduint irq_id = frame->interrupt_id;
	if (irq_id < 0x20) {
		exception_handler(frame);
	}
	else {
		if (irq_id < 256 && interrupt_handlers[irq_id]) {
			interrupt_handlers[irq_id]();
		}
	}

	// Check for pending signals if returning to user mode (Ring > 0)
	if ((frame->hw_cs & 3) != 0) { // returning to Ring > 0
		check_and_deliver_signals(frame);
	}
}
#elif (_MCCA & 0xFF00) == 0x1000

// Unified interrupt dispatcher called from assembly stubs
extern "C" void interrupt_dispatcher(stduint irq_id, NormalTaskContext* cxt) {
	if (irq_id < 256 && interrupt_handlers[irq_id]) {
		interrupt_handlers[irq_id]();
	}
	
	// Check for pending signals if returning to user mode (Ring > 0)
	if (cxt) {
		if ((cxt->mstatus & (3 << 11)) == 0) { // MPP == 0 (User mode)
			check_and_deliver_signals(cxt);
		}
	}
}
#endif

// Register a handler for a specific interrupt ID
extern "C" void register_interrupt_handler(stduint irq_id, Handler_t handler) {
	if (irq_id < 256) {
		interrupt_handlers[irq_id] = handler;
	}
}



#if defined(_UEFI)// x64 only

__attribute__((target("general-regs-only"), optimize("O0")))
void Handint_XHCI() {
	message_queue.Enqueue(SysMessage{ SysMessage::RUPT_xHCI, {} });
	sendEOI();
}

#endif


// ---- ---- ---- ---- SOFT-TIM ---- ---- ---- ---- //

volatile timeval_t system_time = {};
volatile stduint tick = 0;



static int TimerCmp(pureptr_t a, pureptr_t b) {
	return treat<MsgTimer>(((Dnode*)a)->offs).timeout -
		treat<MsgTimer>(((Dnode*)b)->offs).timeout;
}
// Timer management
Dchain TimerManager = { DnodeHeapFreeSimple };
Spinlock timer_lock;

void SysTimer::Initialize() {
	TimerManager.Compare_f = TimerCmp;
}



// [Spinlocked]
void SysTimer::Append(stduint timeout, stduint iden, _tocall_ft hand) {
	SpinlockLocal guard(&timer_lock);
	auto n = TimerManager.Append(new MsgTimer{ tick + timeout, iden, hand });
	if (!n) plogerro("SysTimer::Append failed");
	// ploginfo("SysTimer::Append %u, now %u timers", timeout, TimerManager.Count());
}


#if _MCCA == 0x8664 && defined(_UEFI)

void delay001ms(void) {
	_TODO
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

#if _MCCA == 0x8632
static inline bool FrameFromUser(const HardwareInterruptFrame* frame) {
	return (frame->hw_cs & 3) != 0;
}

static inline stduint FrameSavedEsp(const HardwareInterruptFrame* frame) {
	return FrameFromUser(frame) ? frame->hw_esp : frame->pg_esp_old;
}

static inline stduint FrameSavedSs(const HardwareInterruptFrame* frame) {
	return FrameFromUser(frame) ? frame->hw_ss : frame->pg_ss;
}

static void LogSelectorFaultContext(rostr name, HardwareInterruptFrame* frame, stduint para) {
	const stduint cpu = Taskman::getID();
	const stduint tid = Taskman::CurrentTID();
	const stduint sel_index = para >> 3;
	const stduint sel_ti = (para >> 2) & 0x1;
	const stduint sel_idt = (para >> 1) & 0x1;
	const stduint sel_ext = para & 0x1;
	printlog(_LOG_FATAL,
		"%s with 0x%[x] on CPU%u, TID%u at EIP=0x%[x] ESP=0x%[x] CS=0x%[x] SS=0x%[x] CR3=0x%[x] "
		"(idx=0x%[x] %s %s ext=%u) [R3SCR] hits=%u lapic=%u core=%u",
		name, para, cpu, tid, frame->hw_eip, FrameSavedEsp(frame), frame->hw_cs, FrameSavedSs(frame), frame->pg_cr3,
		sel_index, sel_idt ? "IDT" : (sel_ti ? "LDT" : "GDT"), sel_idt ? "vector" : "selector", sel_ext,
		ap_ring3_iret_guard_hits, ap_ring3_iret_last_lapicid, ap_ring3_iret_last_coreid);
}
#endif


#if (_MCCA & 0xFF00) == 0x8600
_ESYM_C
__attribute__((target("general-regs-only"), optimize("O0")))
bool exception_handler_user(HardwareInterruptFrame* frame, stduint iden, stduint para) {
	if (iden != ERQ_Page_Fault) {
		#if _MCCA == 0x8664
		plogwarn("User exception %d (para %[x]) at RIP %[x], RSP %[x], CR2 %[x]", (int)iden, para, frame->hw_rip, frame->hw_rsp, getCR2());
		#else
		plogwarn("User exception %d (para %[x]) on CPU%u, TID%u at EIP %[x], ESP %[x], CR3 %[x]",
			(int)iden, para, Taskman::getID(), Taskman::CurrentTID(), frame->hw_eip, FrameSavedEsp(frame), frame->pg_cr3);
		#endif
	}
	int sig = 0;
	switch (iden) {
	case ERQ_Divide_By_Zero:
		sig = SIGFPE;
		break;
	case ERQ_Step:
	case ERQ_Breakpoint:
		sig = SIGTRAP;
		break;
	case ERQ_Overflow:
	case ERQ_Bound:
	case ERQ_x0D: // General Protection (#GP) defined as ERQ_x0D in IBM.h
		sig = SIGSEGV;
		break;
	case ERQ_Page_Fault:
	{
		stduint fault_addr = getCR2();
		ThreadBlock* crt = Taskman::CurrentTB();
		ProcessBlock* pb = crt ? crt->parent_process : nullptr;
		bool handled = false;
		if (pb) {
			SpinlockLocal guard(&pb->vma_lock);
			for (stduint i = 0; i < pb->vmas.Count(); i++) {
				const auto& vma = pb->vmas[i];
				if (fault_addr >= vma.vm_start && fault_addr < vma.vm_end) {
					void* phy_page = mempool.allocate(0x1000, 12);
					if (phy_page != (void*)~_IMM0) {
						MemSet((void*)mglb(phy_page), 0, 0x1000); // Zero-fill physical page
						stduint aligned_vaddr = fault_addr & ~_IMM(0xFFF);
						
						if (vma.vm_type == VMA_FILE && vma.vfile) {
							stduint offset = aligned_vaddr - vma.vm_start + vma.file_offset;
							if (vma.vfile->f_inode && offset < vma.vfile->f_inode->i_size) {
								stduint read_len = minof(_IMM(0x1000), vma.vfile->f_inode->i_size - offset);
								vma.vfile->f_pos = offset;
								Filesys::Read(vma.vfile, (void*)mglb(phy_page), read_len);
							}
						}
						
						pb->paging.Map(aligned_vaddr, (stduint)phy_page, 0x1000, PAGESIZE_4KB, PGPROP_present | PGPROP_writable | PGPROP_user_access);
						RefreshVirtualAddress(aligned_vaddr);
						handled = true;
					}
					break;
				}
			}
		}
		if (handled) {
			return true; // Retry faulting instruction
		}
		if (pb) {
			#if _MCCA == 0x8632
			plogwarn("Page fault context: pid=%u eip=0x%[x] esp=0x%[x] cs=0x%[x] cr3=0x%[x] heap=[0x%[x],0x%[x]) vmas=%u",
				pb->pid, frame->hw_eip, FrameSavedEsp(frame), frame->hw_cs, frame->pg_cr3,
				pb->heapbtm, pb->heaptop, pb->vmas.Count());
			#else
			plogwarn("Page fault context");
			#endif
			for (stduint i = 0; i < pb->vmas.Count() && i < 6; i++) {
				const auto& vma = pb->vmas[i];
				plogwarn("  vma[%u] [0x%[x],0x%[x]) flags=0x%[x] type=%u",
					i, vma.vm_start, vma.vm_end, vma.vm_flags, vma.vm_type);
			}
		}
		plogwarn("Segmentation fault: process %u accessed unmapped address 0x%[x]", pb ? pb->pid : 0, fault_addr);
		sig = SIGSEGV;
	}
	break;
	case ERQ_Invalid_Opcode:
		sig = SIGILL;
		break;
	default:
		sig = SIGILL; // Fallback signal for unknown exceptions
		break;
	}

	if (sig != 0) {
		ThreadBlock* crt = Taskman::CurrentTB();
		if (crt) {
			// Mark signal as pending directly in Ring 0 exception handler
			sigaddset(&crt->pending_signals, sig);
			return true; // Returns to dispatcher, which calls check_and_deliver_signals
		}
	}
	return false;
}

_ESYM_C
__attribute__((target("general-regs-only"), optimize("O0")))
void exception_handler(HardwareInterruptFrame* frame) {
	stduint iden = frame->interrupt_id;
	stduint para = frame->error_code;
	stduint r15 = frame->pg_cr3;
	const bool have_para = (iden == 8 || (iden >= 10 && iden <= 14) || iden == 17 || iden == 21);

	// Check if exception comes from user space (Ring 3)
	bool is_user = (frame->hw_cs & 3) != 0;

	if (is_user && exception_handler_user(frame, iden, para)) return;

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
			rostr test_page = (rostr)"[Mecocoa] Exception #UD Test OK!"; // "\xFF\x70[Mecocoa]\xFF\x27 Exception #UD Test OK!\xFF\x07";
			printlog(_LOG_GOOD, " %s", test_page);
			#if _MCCA == 0x8632
			frame->hw_eip += 2;
			#elif _MCCA == 0x8664
			frame->hw_rip += 2;
			#endif
		}
		else {
			#if _MCCA == 0x8632
			printlog(_LOG_FATAL, " %s at EIP %[x], ESP %[x], CS %[x], CR2 %[x], TID %u",
				ExceptionDescription[iden], frame->hw_eip, FrameSavedEsp(frame), frame->hw_cs, getCR2(), Taskman::CurrentTID());
			#elif _MCCA == 0x8664
			printlog(_LOG_FATAL, " %s at RIP %[x], RSP %[x], CS %[x], CR2 %[x], TID %u",
				ExceptionDescription[iden], frame->hw_rip, frame->hw_rsp, frame->hw_cs, getCR2(), Taskman::CurrentTID());
			#else
			printlog(_LOG_FATAL, " %s", ExceptionDescription[iden]);// no-para
			#endif
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
		#if _MCCA == 0x8632
		printlog(_LOG_FATAL,
			"%s with 0x%[x], vaddr=0x%[x], TID%u, EIP=0x%[x], ESP=0x%[x], CS=0x%[x], CR3=0x%[x]\n\r\t %s%s%s%s "
			"[R3SCR] hits=%u lapic=%u core=%u",
			ExceptionDescription[iden], para, getCR2(), Taskman::CurrentTID(),
			frame->hw_eip, FrameSavedEsp(frame), frame->hw_cs, r15,
			para & 1 ? "Protected " : "Miss",
			para & 0b10 ? "Write " : "Read ",
			para & 0b100 ? "User " : "Kernel ",
			para & 0b1000 ? "RSVD " : " ",
			ap_ring3_iret_guard_hits, ap_ring3_iret_last_lapicid, ap_ring3_iret_last_coreid); // printlog will call halt machine
		#else
		printlog(_LOG_FATAL,
			"%s with 0x%[x], vaddr=0x%[x], TID%u, CR3=0x%[x]\n\r\t %s%s%s%s",
			ExceptionDescription[iden], para, getCR2(), Taskman::CurrentTID(), r15,
			para & 1 ? "Protected " : "Miss",
			para & 0b10 ? "Write " : "Read ",
			para & 0b100 ? "User " : "Kernel ",
			para & 0b1000 ? "RSVD " : " "); // printlog will call halt machine
		#endif
		break;

	case ERQ_Invalid_TSS:// 10
	case ERQ_x0B:// 11 Segment Not Present
	case ERQ_x0C:// 12 Stack Fault
	case ERQ_x0D:// 13 General Protection
		#if _MCCA == 0x8632
		LogSelectorFaultContext(ExceptionDescription[iden], frame, para);
		#else
		printlog(_LOG_FATAL, have_para ? "%s with 0x%[32H]" : "%s",
			ExceptionDescription[iden], para);
		#endif
		__asm("cli; hlt");
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

#define TIMER_INTERVAL (CLINT_TIMEBASE_FREQ/100) // 100Hz

uint64 last_schepoint;

void external_interrupt_handler();
void syscall_body(NormalTaskContext* cxt);

_ESYM_C
stduint trap_handler(stduint epc, stduint cause, NormalTaskContext* cxt)
{
	stduint return_pc = epc;
	stduint cause_code = cause & MCAUSE_MASK_ECODE;
	if (cause & MCAUSE_MASK_INTERRUPT) {
		/* Asynchronous trap - interrupt */
		switch (cause_code) {
		case 3:
			// ploginfo("software interruption!");
			clint.MSIP(getMHARTID(), MSIP_Type::AckRupt);
			Taskman::Schedule(true);
			break;
		case 7:
			// ploginfo("timer interruption!"); // timer_handler();
			tick++;
			last_schepoint += TIMER_INTERVAL;
			clint.Load(getMHARTID(), last_schepoint);
			Taskman::Schedule();
			break;
		case 11:
			// ploginfo("external interruption!");
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
			// ploginfo("System call from U-mode!");
			syscall_body(cxt);
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
		// UART0 << ch;
		
		auto p_vtty = vttys[Consman::current_screen_TTY];
		if (!p_vtty) {
			plogerro("assert p_vtty");
		}
		if (ch == '\r') ch = '\n';
		else if (ch == 127) {
			VTTY_INNQ(p_vtty)->OutChar('\b');
			VTTY_INNQ(p_vtty)->OutChar(' ');
			ch = '\b';
		}
		VTTY_INNQ(p_vtty)->OutChar(ch);
		Consman::WakeBlockedWaitersDeferred();
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

