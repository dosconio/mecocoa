// UTF-8 g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: Signal Management - Default Actions
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../include/mecocoa.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress-of-packed-member"

// Default actions for signals as defined in the implementation plan
enum SigActionDefault {
	SIG_ACT_TERM,	// Terminate process (Taskman::Exit)
	SIG_ACT_IGN,	// Ignore signal
	SIG_ACT_CORE,	// Terminate and dump core (currently treated as Term)
	SIG_ACT_STOP,	// Stop process
	SIG_ACT_CONT	// Continue process
};

// Default action table for all 64 signals
static const SigActionDefault default_actions[_NSIG] = {
	SIG_ACT_TERM,	// 0: Unused
	SIG_ACT_TERM,	// 1: SIGHUP
	SIG_ACT_TERM,	// 2: SIGINT
	SIG_ACT_CORE,	// 3: SIGQUIT
	SIG_ACT_CORE,	// 4: SIGILL
	SIG_ACT_CORE,	// 5: SIGTRAP
	SIG_ACT_CORE,	// 6: SIGABRT
	SIG_ACT_CORE,	// 7: SIGBUS
	SIG_ACT_TERM,	// 8: SIGFPE
	SIG_ACT_TERM,	// 9: SIGKILL
	SIG_ACT_TERM,	// 10: SIGUSR1
	SIG_ACT_CORE,	// 11: SIGSEGV
	SIG_ACT_TERM,	// 12: SIGUSR2
	SIG_ACT_TERM,	// 13: SIGPIPE
	SIG_ACT_TERM,	// 14: SIGALRM
	SIG_ACT_TERM,	// 15: SIGTERM
	SIG_ACT_TERM,	// 16: SIGSTKFLT
	SIG_ACT_IGN,	// 17: SIGCHLD
	SIG_ACT_CONT,	// 18: SIGCONT
	SIG_ACT_STOP,	// 19: SIGSTOP
	SIG_ACT_STOP,	// 20: SIGTSTP
	SIG_ACT_STOP,	// 21: SIGTTIN
	SIG_ACT_STOP,	// 22: SIGTTOU
	SIG_ACT_IGN,	// 23: SIGURG
	SIG_ACT_TERM,	// 24: SIGXCPU
	SIG_ACT_TERM,	// 25: SIGXFSZ
	SIG_ACT_TERM,	// 26: SIGVTALRM
	SIG_ACT_TERM,	// 27: SIGPROF
	SIG_ACT_IGN,	// 28: SIGWINCH
	SIG_ACT_TERM,	// 29: SIGIO
	SIG_ACT_TERM,	// 30: SIGPWR
	SIG_ACT_TERM,	// 31: SIGSYS
	// 32-64 (Real-time): Default to Term
	SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM,
	SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM,
	SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM,
	SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM, SIG_ACT_TERM,
	SIG_ACT_TERM
};

struct RegisterContext {
	stduint* p_ip;
	stduint* p_sp;
	stduint* p_flags;
	stduint* p_ax;
	stduint* p_cx;
	stduint* p_dx;
	stduint* p_bx;
	stduint* p_bp;
	stduint* p_si;
	stduint* p_di;
#if _MCCA == 0x8664
	stduint* p_r8;
	stduint* p_r9;
	stduint* p_r10;
	stduint* p_r11;
	stduint* p_r12;
	stduint* p_r13;
	stduint* p_r14;
	stduint* p_r15;
#endif
};

static void check_and_deliver_signals_generic(RegisterContext& ctx) {
	ThreadBlock* crt = Taskman::CurrentTB();
	if (!crt || !crt->parent_process) return;

	ProcessBlock* pb = crt->parent_process;

	// Find the first unblocked pending signal (both thread-specific and process-shared)
	int signo = 0;
	struct _POSIX_sigaction act = {};
	for (int i = 1; i < _NSIG; i++) {
		if (!sigismember(&crt->blocked_signals, i)) {
			if (sigismember(&crt->pending_signals, i)) {
				signo = i;
				sigdelset(&crt->pending_signals, i);
				break;
			} else {
				auto signals = pb->signals.Lock();
				if (sigismember(&signals->shared_pending_signals, i)) {
					signo = i;
					sigdelset(&signals->shared_pending_signals, i);
					break;
				}
			}
		}
	}
	
	// If no signal, return immediately
	if (signo == 0) return;

	// We retrieve the registered action
	{
		auto signals = pb->signals.Lock();
		act = signals->sig_actions[signo];
	}
	
	// Check if action is IGNORE
	if (act.sa_handler == SIG_IGN) {
		return;
	}

	// Check if action is DEFAULT
	if (act.sa_handler == SIG_DFL) {
		SigActionDefault def = default_actions[signo];
		switch (def) {
			case SIG_ACT_IGN:
				// Simply discard and return
				break;
			case SIG_ACT_TERM:
			case SIG_ACT_CORE:
				// Terminate process with standardized exit code
				if (signo == SIGKILL || signo == SIGTERM) {
					ploginfo("Process %u forced to exit due to signal %d", pb->getID(), signo);
				}
				Taskman::ExitCurrent(128 + signo);
				break;
			case SIG_ACT_STOP:
				// Suspend the thread
				crt->state = ThreadBlock::State::Pended;
				crt->block_reason = ThreadBlock::BlockReason::BR_Waiting;
				Taskman::Schedule(true);
				break;
			case SIG_ACT_CONT:
				// Resume the thread if it was pended
				if (crt->state == ThreadBlock::State::Pended) {
					crt->state = ThreadBlock::State::Ready;
					crt->block_reason = ThreadBlock::BlockReason::BR_None;
					Taskman::EnqueueReady(crt);
				}
				break;
		}
		return;
	}

	// User-defined signal handler setup
	stduint user_sp = *ctx.p_sp;

	// Alternate signal stack check
	if ((act.sa_flags & SA_ONSTACK) && crt->sas_sp != 0 && crt->sas_size > 0) {
		// Only switch to alternate stack if we are not already executing on it
		if (user_sp < crt->sas_sp || user_sp >= crt->sas_sp + crt->sas_size) {
			user_sp = crt->sas_sp + crt->sas_size;
		}
	}

	// 16-byte stack alignment
	user_sp &= ~0xF;

	// Populate SigContext for restoration
	struct SigContext {
		stduint reg_eip;
		stduint reg_eflags;
		stduint reg_esp;
		stduint reg_eax;
		stduint reg_ecx;
		stduint reg_edx;
		stduint reg_ebx;
		stduint reg_ebp;
		stduint reg_esi;
		stduint reg_edi;
		_POSIX_sigset_t old_mask;
	} scontext = {};

	scontext.reg_eip = *ctx.p_ip;
	scontext.reg_eflags = *ctx.p_flags;
	scontext.reg_esp = *ctx.p_sp;
	scontext.reg_eax = *ctx.p_ax;
	scontext.reg_ecx = *ctx.p_cx;
	scontext.reg_edx = *ctx.p_dx;
	scontext.reg_ebx = *ctx.p_bx;
	scontext.reg_ebp = *ctx.p_bp;
	scontext.reg_esi = *ctx.p_si;
	scontext.reg_edi = *ctx.p_di;

	scontext.old_mask = crt->blocked_signals;

	struct SigStackFrame {
		int signo;
		SigContext context;
	} uframe = {};

	uframe.signo = signo;
	uframe.context = scontext;

	// Push SigStackFrame and trampoline/return address onto user stack
#if _MCCA == 0x8632
	user_sp -= sizeof(SigStackFrame);
	user_sp &= ~0xF; // Align the stack frame start to 16-byte boundary
	user_sp -= 4; // Space for return address (sa_restorer), making ESP = 16n - 4 (12 mod 16)

	// Write SigStackFrame (located at user_sp + 4, which is 16-byte aligned)
	MccaMemCopyP((void*)(user_sp + 4), pb, &uframe, nullptr, sizeof(SigStackFrame));

	// Write trampoline address (sa_restorer) at user_sp
	stduint restorer = (stduint)act.sa_restorer;
	MccaMemCopyP((void*)user_sp, pb, &restorer, nullptr, 4);

	// Update stack frame to execute the handler
	*ctx.p_sp = user_sp;
	*ctx.p_ip = (stduint)act.sa_handler;
	
#elif _MCCA == 0x8664
	user_sp -= sizeof(SigStackFrame);
	user_sp &= ~0xF; // Align the stack frame start to 16-byte boundary
	user_sp -= 8; // Space for return address (sa_restorer), making RSP = 16n - 8 (8 mod 16)

	// Write SigStackFrame (located at user_sp + 8, which is 16-byte aligned)
	MccaMemCopyP((void*)(user_sp + 8), pb, &uframe, nullptr, sizeof(SigStackFrame));

	// Write trampoline address (sa_restorer) at user_sp
	stduint restorer = (stduint)act.sa_restorer;
	MccaMemCopyP((void*)user_sp, pb, &restorer, nullptr, 8);

	// Update stack frame to execute the handler
	*ctx.p_sp = user_sp;
	*ctx.p_ip = (stduint)act.sa_handler;
	*ctx.p_di = signo; // first argument (RDI) in x86_64 calling convention

#elif (_MCCA & 0xFF00) == 0x1000 // RISCV
	user_sp -= sizeof(SigStackFrame);
	user_sp &= ~0xF; // Keep 16-byte aligned

	// Write SigStackFrame (located at user_sp)
	MccaMemCopyP((void*)user_sp, pb, &uframe, nullptr, sizeof(SigStackFrame));

	// Update context: set ra to restorer, a0 to signo, sp to user_sp, IP to handler
	*ctx.p_di = (stduint)act.sa_restorer;
	*ctx.p_ax = signo;
	*ctx.p_sp = user_sp;
	*ctx.p_ip = (stduint)act.sa_handler;
#endif

	// Block the signal during handler execution (nested protection) unless SA_NODEFER is set
	if (!(act.sa_flags & SA_NODEFER)) {
		sigaddset(&crt->blocked_signals, signo);
	}
	// Add sa_mask to blocked signals
	for (int i = 1; i < _NSIG; i++) {
		if (sigismember(&act.sa_mask, i)) {
			sigaddset(&crt->blocked_signals, i);
		}
	}
}

extern "C" void check_and_deliver_signals(void* context) {
	RegisterContext ctx = {};
#if _MCCA == 0x8632
	HardwareInterruptFrame* frame = (HardwareInterruptFrame*)context;
	ctx.p_ip = &frame->hw_eip;
	ctx.p_sp = &frame->hw_esp;
	ctx.p_flags = &frame->hw_eflags;
	ctx.p_ax = &frame->pusha_eax;
	ctx.p_cx = &frame->pusha_ecx;
	ctx.p_dx = &frame->pusha_edx;
	ctx.p_bx = &frame->pusha_ebx;
	ctx.p_bp = &frame->pusha_ebp;
	ctx.p_si = &frame->pusha_esi;
	ctx.p_di = &frame->pusha_edi;
#elif _MCCA == 0x8664
	HardwareInterruptFrame* frame = (HardwareInterruptFrame*)context;
	ctx.p_ip = &frame->hw_rip;
	ctx.p_sp = &frame->hw_rsp;
	ctx.p_flags = &frame->hw_rflags;
	ctx.p_ax = &frame->pusha_rax;
	ctx.p_cx = &frame->pusha_rcx;
	ctx.p_dx = &frame->pusha_rdx;
	ctx.p_bx = &frame->pusha_rbx;
	ctx.p_bp = &frame->pusha_rbp;
	ctx.p_si = &frame->pusha_rsi;
	ctx.p_di = &frame->pusha_rdi;
	ctx.p_r8 = &frame->pusha_r8;
	ctx.p_r9 = &frame->pusha_r9;
	ctx.p_r10 = &frame->pusha_r10;
	ctx.p_r11 = &frame->pusha_r11;
	ctx.p_r12 = &frame->pusha_r12;
	ctx.p_r13 = &frame->pusha_r13;
	ctx.p_r14 = &frame->pusha_r14;
	ctx.p_r15 = &frame->pusha_r15;
#elif (_MCCA & 0xFF00) == 0x1000 // RISCV
	NormalTaskContext* cxt = (NormalTaskContext*)context;
	ctx.p_ip = &cxt->IP;
	ctx.p_sp = &cxt->sp;
	ctx.p_flags = &cxt->mstatus;
	ctx.p_ax = &cxt->a0;
	ctx.p_cx = &cxt->a1;
	ctx.p_dx = &cxt->a2;
	ctx.p_bx = &cxt->a3;
	ctx.p_bp = &cxt->s0;
	ctx.p_si = &cxt->s1;
	ctx.p_di = &cxt->ra;
#endif
	check_and_deliver_signals_generic(ctx);
}

extern "C" void check_and_deliver_signals_syscall(CallgateFrame* frame) {
	RegisterContext ctx = {};
#if _MCCA == 0x8632
	ctx.p_ip = &frame->ip;
	ctx.p_sp = &frame->sp0;
	ctx.p_flags = &frame->flags;
	ctx.p_ax = &frame->ax;
	ctx.p_cx = &frame->cx;
	ctx.p_dx = &frame->dx;
	ctx.p_bx = &frame->bx;
	ctx.p_bp = &frame->bp;
	ctx.p_si = &frame->si;
	ctx.p_di = &frame->di;
#elif _MCCA == 0x8664
	ctx.p_ip = &frame->ip;
	ctx.p_sp = &frame->sp0;
	ctx.p_flags = &frame->flags;
	ctx.p_ax = &frame->ax;
	ctx.p_cx = &frame->cx;
	ctx.p_dx = &frame->dx;
	ctx.p_bx = &frame->bx;
	ctx.p_bp = &frame->bp;
	ctx.p_si = &frame->si;
	ctx.p_di = &frame->di;
	ctx.p_r8 = &frame->r8;
	ctx.p_r9 = &frame->r9;
	ctx.p_r10 = &frame->r10;
	ctx.p_r11 = &frame->r11;
	ctx.p_r12 = &frame->r12;
	ctx.p_r13 = &frame->r13;
	ctx.p_r14 = &frame->r14;
	ctx.p_r15 = &frame->r15;
#endif
	check_and_deliver_signals_generic(ctx);
}

// ---- Bitmask Helpers ----

extern "C" int sigemptyset(_POSIX_sigset_t* set) {
	if (!set) return -1;
	_sigset_raw(set) = 0;
	return 0;
}

extern "C" int sigfillset(_POSIX_sigset_t* set) {
	if (!set) return -1;
	_sigset_raw(set) = ~0ULL;
	return 0;
}

extern "C" int sigaddset(_POSIX_sigset_t* set, int signo) {
	if (!set || signo <= 0 || signo >= _NSIG) return -1;
	_sigset_raw(set) |= (1ULL << (signo - 1));
	return 0;
}

extern "C" int sigdelset(_POSIX_sigset_t* set, int signo) {
	if (!set || signo <= 0 || signo >= _NSIG) return -1;
	_sigset_raw(set) &= ~(1ULL << (signo - 1));
	return 0;
}

extern "C" int sigismember(const _POSIX_sigset_t* set, int signo) {
	if (!set || signo <= 0 || signo >= _NSIG) return 0;
	return (_sigset_raw(set) & (1ULL << (signo - 1))) != 0;
}

// ---- Syscall Implementations ----

extern "C" stduint sys_sigaction(int sig, const struct _POSIX_sigaction* act, struct _POSIX_sigaction* oact) {
	if (sig <= 0 || sig >= _NSIG || sig == SIGKILL || sig == SIGSTOP) return (stduint)-1;
	
	ThreadBlock* crt = Taskman::CurrentTB();
	ProcessBlock* pb = crt->parent_process;
	
	if (oact) {
		auto signals = pb->signals.Lock();
		*oact = signals->sig_actions[sig];
	}
	if (act) {
		auto signals = pb->signals.Lock();
		signals->sig_actions[sig] = *act;
	}
	return 0;
}

static void wakeup_thread_for_signal(ThreadBlock* th, int sig) {
	if (th->state == ThreadBlock::State::Pended) {
		if (th->block_reason & (ThreadBlock::BlockReason::BR_Resting | 
								ThreadBlock::BlockReason::BR_RecvMsg | 
								ThreadBlock::BlockReason::BR_SendMsg | 
								ThreadBlock::BlockReason::BR_Waiting)) {
			// 1. Sleep Timer Cleanup
			if (th->block_reason & ThreadBlock::BlockReason::BR_Resting) {
				extern Spinlock timer_lock;
				extern Dchain TimerManager;
				SpinlockLocal guard(&timer_lock);
				for (auto nod = TimerManager.Root(); nod; ) {
					auto next_nod = nod->next;
					auto msg_timer = (MsgTimer*)nod->offs;
					if (msg_timer->iden == (stduint)th) {
						TimerManager.Remove(nod);
						break;
					}
					nod = next_nod;
				}
			}

			// 2. IPC Message Cleanup
			if (th->block_reason & (ThreadBlock::BlockReason::BR_RecvMsg | ThreadBlock::BlockReason::BR_SendMsg)) {
				msg_cleanup_thread(th);
			}

			// 3. Unblock the thread and enqueue it
			th->Unblock(th->block_reason);
		}
	}
}

extern "C" stduint sys_kill(stduint pid, int sig, stduint tid) {
	if (sig < 0 || sig >= _NSIG) return (stduint)-1;
	
	ProcessBlock* pb = Taskman::Locate(pid);
	if (!pb) return (stduint)-1;
	
	if (sig == 0) return 0; // Check existence

	if (tid == 0) {
		// Send to process (Shared)
		{
			auto signals = pb->signals.Lock();
			sigaddset(&signals->shared_pending_signals, sig);
		}
		
		// Find a thread to deliver the signal (one that doesn't block the signal)
		ThreadBlock* target_th = nullptr;
		ThreadBlock* curr_th = pb->thread_list_head;
		while (curr_th) {
			if (!sigismember(&curr_th->blocked_signals, sig)) {
				// Prioritize Ready/Running or interruptible Pended threads
				if (curr_th->state == ThreadBlock::State::Ready || curr_th->state == ThreadBlock::State::Running) {
					target_th = curr_th;
					break;
				} else if (curr_th->state == ThreadBlock::State::Pended && 
						   (curr_th->block_reason & (ThreadBlock::BlockReason::BR_Resting | 
													 ThreadBlock::BlockReason::BR_RecvMsg | 
													 ThreadBlock::BlockReason::BR_SendMsg | 
													 ThreadBlock::BlockReason::BR_Waiting))) {
					target_th = curr_th;
				}
			}
			curr_th = curr_th->process_thread_next;
		}

		// If a target thread is found, move the signal to its pending list and wake it up
		if (target_th) {
			sigaddset(&target_th->pending_signals, sig);
			auto signals = pb->signals.Lock();
			sigdelset(&signals->shared_pending_signals, sig);
			wakeup_thread_for_signal(target_th, sig);
		}
	} else {
		// Send to specific thread
		ThreadBlock* th = Taskman::LocateThread(tid);
		if (!th || th->parent_process != pb) return (stduint)-1;
		sigaddset(&th->pending_signals, sig);
		
		// Wake up target thread if pended and signal not blocked
		if (!sigismember(&th->blocked_signals, sig)) {
			wakeup_thread_for_signal(th, sig);
		}
	}
	return 0;
}

extern "C" stdsint sysc_SIGR(void* context) {
	ThreadBlock* crt = Taskman::CurrentTB();
	if (!crt || !crt->parent_process) return -1;

	ProcessBlock* pb = crt->parent_process;

	// Structure definitions matching delivery
	struct SigContext {
		stduint reg_eip;
		stduint reg_eflags;
		stduint reg_esp;
		stduint reg_eax;
		stduint reg_ecx;
		stduint reg_edx;
		stduint reg_ebx;
		stduint reg_ebp;
		stduint reg_esi;
		stduint reg_edi;
		_POSIX_sigset_t old_mask;
	};

	struct SigStackFrame {
		int signo;
		SigContext context;
	};

	stduint user_sp = 0;
	#if _MCCA == 0x8632
	CallgateFrame* frame = (CallgateFrame*)context;
	user_sp = frame->sp0;
	#elif _MCCA == 0x8664
	CallgateFrame* frame = (CallgateFrame*)context;
	user_sp = frame->sp0;
	#elif (_MCCA & 0xFF00) == 0x1000 // RISCV
	NormalTaskContext* cxt = (NormalTaskContext*)context;
	user_sp = cxt->sp;
	#endif

	SigStackFrame uframe = {};
	// Safely copy signal frame from user stack
	if (MccaMemCopyP(&uframe, nullptr, (const void*)user_sp, pb, sizeof(SigStackFrame)) != sizeof(SigStackFrame)) {
		return -1;
	}

	#if (_MCCA & 0xFF00) == 0x8600 // x86 only: Segment sanitization
	//  Sanitize Segments to prevent Ring 0 escalation
	// Segment selector values defined strictly from memoman.hpp GDT layout
	stduint reg_cs = (stduint)SegCoR3 | (stduint)RING_U; // Force User Code Selector
	stduint reg_ss = (stduint)SegDaR3 | (stduint)RING_U; // Force User Data Selector
	#endif

	//  Sanitize EFLAGS
	// Allow user flags modifications (CF, PF, AF, ZF, SF, TF, IF, DF, OF)
	// Clear all system privilege flags (IOPL, NT, VM, RF) to prevent Ring 0 privilege escalation
	// Ensure IF (Interrupt Enable Flag, 0x200) remains enabled for the thread
	stduint reg_eflags = (uframe.context.reg_eflags & 0x8D5) | 0x200;

	//  Restore signal mask for nested signal support
	crt->blocked_signals = uframe.context.old_mask;

	//  Update the actual kernel stack frame registers
	#if _MCCA == 0x8632
	frame->ip = uframe.context.reg_eip;
	frame->flags = reg_eflags;
	frame->sp0 = uframe.context.reg_esp;
	frame->cs = reg_cs;
	frame->ss0 = reg_ss;
	frame->ax = uframe.context.reg_eax;
	frame->cx = uframe.context.reg_ecx;
	frame->dx = uframe.context.reg_edx;
	frame->bx = uframe.context.reg_ebx;
	frame->bp = uframe.context.reg_ebp;
	frame->si = uframe.context.reg_esi;
	frame->di = uframe.context.reg_edi;
	#elif _MCCA == 0x8664
	frame->ip = uframe.context.reg_eip;
	frame->flags = reg_eflags;
	frame->sp0 = uframe.context.reg_esp;
	frame->ax = uframe.context.reg_eax;
	// Do not restore cx on x86_64, as frame->cx shares the same union space with frame->ip (RIP) in CallgateFrame.
	// frame->cx = uframe.context.reg_ecx;
	frame->dx = uframe.context.reg_edx;
	frame->bx = uframe.context.reg_ebx;
	frame->bp = uframe.context.reg_ebp;
	frame->si = uframe.context.reg_esi;
	frame->di = uframe.context.reg_edi;
	// Segments are handled automatically by sysret/swapgs on 64-bit return
	#elif (_MCCA & 0xFF00) == 0x1000 // RISCV
	cxt->IP = uframe.context.reg_eip;
	cxt->mstatus = reg_eflags; // mstatus maps to flags here
	cxt->sp = uframe.context.reg_esp;
	cxt->a0 = uframe.context.reg_eax;
	cxt->a1 = uframe.context.reg_ecx;
	cxt->a2 = uframe.context.reg_edx;
	cxt->a3 = uframe.context.reg_ebx;
	cxt->s0 = uframe.context.reg_ebp;
	cxt->s1 = uframe.context.reg_esi;
	cxt->ra = uframe.context.reg_edi;
	#endif

	// On x86, the return value of Handint_SYSCALL is written to frame->ax.
	// Since we are restoring the original ax from the context, we must return that
	// restored eax/rax value so that the dispatcher doesn't overwrite it!
	return uframe.context.reg_eax;
}

#pragma GCC diagnostic pop
