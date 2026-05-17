// UTF-8 g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: Signal Management - Default Actions
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../include/mecocoa.hpp"

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

extern "C" void check_and_deliver_signals(void* context) {
	ThreadBlock* crt = Taskman::current_thread[Taskman::getID()];
	if (!crt || !crt->parent_process) return;

	// Note: Signal bitmask fields (pending_signals, blocked_signals) 
	// and sig_actions table will be added to Control Blocks in Step 4.
	// This function currently provides the logic foundation for SIG_DFL.

	// Placeholder for pending signal detection (Logic to be expanded in Step 4)
	int signo = 0; // Find first unblocked pending signal
	
	// If no signal, return immediately
	if (signo == 0) return;

	ProcessBlock* pb = crt->parent_process;
	
	// We assume pb->sig_actions[signo].sa_handler is SIG_DFL for this step
	SigActionDefault def = default_actions[signo];
	
	switch (def) {
		case SIG_ACT_IGN:
			// Simply discard and return
			break;
		case SIG_ACT_TERM:
		case SIG_ACT_CORE:
			// Terminate process with standardized exit code
			Taskman::Exit(pb, (128 + signo));
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
}

// ---- Bitmask Helpers ----

extern "C" int sigemptyset(sigset_t* set) {
	if (!set) return -1;
	_sigset_raw(set) = 0;
	return 0;
}

extern "C" int sigfillset(sigset_t* set) {
	if (!set) return -1;
	_sigset_raw(set) = ~0ULL;
	return 0;
}

extern "C" int sigaddset(sigset_t* set, int signo) {
	if (!set || signo <= 0 || signo >= _NSIG) return -1;
	_sigset_raw(set) |= (1ULL << (signo - 1));
	return 0;
}

extern "C" int sigdelset(sigset_t* set, int signo) {
	if (!set || signo <= 0 || signo >= _NSIG) return -1;
	_sigset_raw(set) &= ~(1ULL << (signo - 1));
	return 0;
}

extern "C" int sigismember(const sigset_t* set, int signo) {
	if (!set || signo <= 0 || signo >= _NSIG) return 0;
	return (_sigset_raw(set) & (1ULL << (signo - 1))) != 0;
}

// ---- Syscall Implementations ----

extern "C" stduint sys_sigaction(int sig, const struct sigaction* act, struct sigaction* oact) {
	if (sig <= 0 || sig >= _NSIG || sig == SIGKILL || sig == SIGSTOP) return (stduint)-1;
	
	ThreadBlock* crt = Taskman::current_thread[Taskman::getID()];
	ProcessBlock* pb = crt->parent_process;
	
	if (oact) {
		*oact = pb->sig_actions[sig];
	}
	if (act) {
		pb->sig_actions[sig] = *act;
	}
	return 0;
}

extern "C" stduint sys_kill(stduint pid, int sig, stduint tid) {
	if (sig < 0 || sig >= _NSIG) return (stduint)-1;
	
	ProcessBlock* pb = Taskman::Locate(pid);
	if (!pb) return (stduint)-1;
	
	if (sig == 0) return 0; // Check existence

	if (tid == 0) {
		// Send to process (Shared)
		sigaddset(&pb->shared_pending_signals, sig);
		//{} TODO: Wake up any thread of the process that is not blocking the signal
	} else {
		// Send to specific thread
		ThreadBlock* th = Taskman::LocateThread(tid);
		if (!th || th->parent_process != pb) return (stduint)-1;
		sigaddset(&th->pending_signals, sig);
		//{} TODO: Wake up the thread if pended
	}
	return 0;
}

extern "C" void sys_sigreturn() {
	// Placeholder for context restoration from user stack
	//{} To be implemented when user-space trampoline is finalized
}
