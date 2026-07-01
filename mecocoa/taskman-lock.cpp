// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

bool InterruptSaveDisable() {
	#if (_MCCA & 0xFF00) == 0x8600
	stduint flags = getFlags();
	bool was_enabled = (byte)cast<REG_FLAG_t>(flags).IF != 0;
	if (was_enabled) {
		IC.enInterrupt(false);
	}
	return was_enabled;
	#elif (_MCCA & 0xFF00) == 0x1000
	bool was_enabled = getInterrupt() != 0;
	if (was_enabled) {
		IC.enInterrupt(false);
	}
	return was_enabled;
	#else
	return false;
	#endif
}

void InterruptRestore(bool was_enabled) {
	if (was_enabled) {
		IC.enInterrupt(true);
	}
}

bool Spinlock::Acquire() {
	bool state_rupt = InterruptSaveDisable();
	while (__atomic_exchange_n(&this->locked, 1, __ATOMIC_ACQUIRE) != 0) {
		#if (_MCCA & 0xFF00) == 0x8600
		asm volatile("pause" ::: "memory");
		#endif
	}
	this->cpu_id = (stdsint)Taskman::getID();
	return (bool)state_rupt;
}
void Spinlock::Release(bool old_if) {
	this->cpu_id = -1;
	__atomic_store_n(&this->locked, 0, __ATOMIC_RELEASE);
	InterruptRestore(old_if);
}


void Mutex::Acquire() {
	bool old_if = this->guard.Acquire();

	if (this->locked) {
		ThreadBlock* crt = Taskman::CurrentTB();
		if (!this->wait_queue.isFull()) {
			crt->Block(ThreadBlock::BlockReason::BR_Lock);
			this->wait_queue.Enqueue(crt);
			Taskman::SleepAndRelease(&this->guard);
		}
		InterruptRestore(old_if);
	}
	else {
		this->locked = 1;
		this->guard.Release(old_if);
	}
}
void Mutex::Release() {
	bool old_if = this->guard.Acquire();
	ThreadBlock* wakeup_tb = nullptr;
	if (!this->wait_queue.isEmpty()) {
		this->wait_queue.Dequeue(wakeup_tb);
	}
	else {
		this->locked = 0;
	}
	this->guard.Release(old_if);
	// Unblock outside the guard spinlock to avoid lock-order inversion:
	// Unblocking acquires scheduler_lock, and some paths hold scheduler_lock
	// before acquiring a Mutex (e.g. CleanupTTYReferences -> focus_tty.Lock).
	if (wakeup_tb) {
		wakeup_tb->Unblock(ThreadBlock::BlockReason::BR_Lock);
	}
}


void Semaphore::Acquire() {
	bool old_if = this->guard.Acquire(); // Disable interrupts and acquire lock

	if (this->value > 0) {
		this->value--;
		this->guard.Release(old_if); // Resource acquired, unlock and return
	}
	else {
		ThreadBlock* crt = Taskman::CurrentTB();
		if (!this->wait_queue.isFull()) {
			// Transition through the shared Block() path so lock waiters
			// follow the same scheduler/block_reason semantics as other sleepers.
			crt->Block(ThreadBlock::BlockReason::BR_Lock);
			this->wait_queue.Enqueue(crt);

			// Yield CPU: puts thread to sleep, releases spinlock, and switches context
			Taskman::SleepAndRelease(&this->guard);
		}
		InterruptRestore(old_if); // Re-enable interrupts if they were originally enabled
	}
}

void Semaphore::Release() {
	bool old_if = this->guard.Acquire(); // Disable interrupts and acquire lock

	if (!this->wait_queue.isEmpty()) {
		ThreadBlock* wakeup_tb = nullptr;
		this->wait_queue.Dequeue(wakeup_tb);
		if (wakeup_tb) {
			// Wake up the waiting thread by moving it back to ready queue
			wakeup_tb->Unblock(ThreadBlock::BlockReason::BR_Lock);
		}
	}
	else {
		this->value++; // Increment resource counter if no threads are pending
	}

	this->guard.Release(old_if); // Release lock and restore interrupt state
}

