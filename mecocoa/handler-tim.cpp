// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Handler and Soft Timer
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <c/driver/timer.h>

// ---- ---- ---- ---- SOFT-TIM ---- ---- ---- ---- //

volatile timeval_t system_time = {};
volatile stduint tick = 0;

#if !defined (_MPU_STM32MP13) //{TEMP}

namespace {
	enum class TimerAction : byte {
		Callback,
		ThreadWake,
		DriverMessage,
	};

	struct MsgTimer {
		stduint timeout = 0;
		stduint iden = 0;
		_tocall_ft hand = nullptr;
		TimerAction action = TimerAction::Callback;
		stduint target = 0;
		stduint message_type = 0;
		stduint retry_after = 0;
		stduint serial = 0;
		byte dispatch_attempts = 0;
	};

	static constexpr byte TimerDispatchAttemptLimit = 5;
	static constexpr stduint TimerDispatchRetryDelay = 2;
}

static int TimerCmp(pureptr_t a, pureptr_t b) {
	return treat<MsgTimer>(((Dnode*)a)->offs).timeout -
		treat<MsgTimer>(((Dnode*)b)->offs).timeout;
}
static int TimerRetryCmp(pureptr_t a, pureptr_t b) {
	return treat<MsgTimer>(((Dnode*)a)->offs).retry_after -
		treat<MsgTimer>(((Dnode*)b)->offs).retry_after;
}
// Timer management
Dchain TimerManager = { DnodeHeapFreeSimple };
Dchain TimerRetryManager = { DnodeHeapFreeSimple };
Spinlock timer_lock;

namespace {
	bool timer_dispatch_pending = false;
	stduint timer_serial = 0;

	bool CopyTimerToChain(Dchain& destination, const MsgTimer& timer) {
		auto* copy = new MsgTimer(timer);
		if (!copy) return false;
		if (destination.Append(copy)) return true;
		delete copy;
		return false;
	}

	Dnode* FindTimerBySerial(Dchain& timers, stduint serial) {
		for (auto* node = timers.Root(); node; node = node->next) {
			if (treat<MsgTimer>(node->offs).serial == serial) return node;
		}
		return nullptr;
	}

	void CancelThreadWakeFromChain(Dchain& timers, stduint tid) {
		for (auto* node = timers.Root(); node; ) {
			auto* next = node->next;
			auto& timer = treat<MsgTimer>(node->offs);
			if (timer.action == TimerAction::ThreadWake && timer.target == tid) {
				timers.Remove(node);
			}
			node = next;
		}
	}

	bool AppendTimer(stduint timeout, MsgTimer timer) {
		SpinlockLocal guard(&timer_lock);
		timer.timeout = tick + timeout;
		timer.serial = ++timer_serial;
		if (!timer.serial) timer.serial = ++timer_serial;
		const bool appended = CopyTimerToChain(TimerManager, timer);
		if (!appended) plogerro("SysTimer::Append failed");
		return appended;
	}
}

void SysTimer::Initialize() {
	TimerManager.Compare_f = TimerCmp;
	TimerRetryManager.Compare_f = TimerRetryCmp;
}



// [Spinlocked]
void SysTimer::Append(stduint timeout, stduint iden, _tocall_ft hand) {
	MsgTimer timer = {};
	timer.iden = iden;
	timer.hand = hand;
	timer.action = TimerAction::Callback;
	(void)AppendTimer(timeout, timer);
}

bool Systimex::AppendThreadWake(stduint timeout, stduint tid) {
	MsgTimer timer = {};
	timer.action = TimerAction::ThreadWake;
	timer.target = tid;
	return AppendTimer(timeout, timer);
}

bool Systimex::AppendDriverMessage(stduint timeout, stduint target_tid,
	stduint message_type, stduint value) {
	MsgTimer timer = {};
	timer.iden = value;
	timer.action = TimerAction::DriverMessage;
	timer.target = target_tid;
	timer.message_type = message_type;
	return AppendTimer(timeout, timer);
}

void Systimex::CancelThreadWake(stduint tid) {
	SpinlockLocal guard(&timer_lock);
	CancelThreadWakeFromChain(TimerManager, tid);
	CancelThreadWakeFromChain(TimerRetryManager, tid);
}

void Systimex::CollectExpired() {
	bool notify = false;
	stduint notify_serial = 0;
	for (;;) {
		MsgTimer callback = {};
		bool run_callback = false;
		{
			SpinlockLocal guard(&timer_lock);
			if (!timer_dispatch_pending) {
				auto* retry = TimerRetryManager.Root();
				if (retry && tick >= treat<MsgTimer>(retry->offs).retry_after) {
					MsgTimer timer = treat<MsgTimer>(retry->offs);
					timer.retry_after = 0;
					if (CopyTimerToChain(TimerManager, timer)) {
						TimerRetryManager.Remove(retry);
					}
				}
			}

			Dnode* dispatch = nullptr;
			for (auto* node = TimerManager.Root(); node; node = node->next) {
				auto& timer = treat<MsgTimer>(node->offs);
				if (tick < timer.timeout) break;
				if (timer.action == TimerAction::Callback) {
					callback = timer;
					TimerManager.Remove(node);
					run_callback = true;
					break;
				}
				if (!timer_dispatch_pending && !dispatch && tick >= timer.retry_after) {
					dispatch = node;
				}
			}
			if (!run_callback && dispatch) {
				notify_serial = treat<MsgTimer>(dispatch->offs).serial;
				timer_dispatch_pending = true;
				notify = true;
			}
		}
		if (run_callback && callback.hand) {
			callback.hand((pureptr_t)callback.timeout, callback.iden);
		}
		if (!run_callback) break;
	}
	if (!notify) return;

	DeviceEvent event = {};
	event.kind = _IMM(DeviceEventKind::TimerExpired);
	event.count = 1;
	if (device_event_proc(Task_Devsman, event)) return;

	MsgTimer dropped = {};
	bool drop = false;
	bool retry_queue_failed = false;
	{
		SpinlockLocal guard(&timer_lock);
		timer_dispatch_pending = false;
		auto* node = FindTimerBySerial(TimerManager, notify_serial);
		if (node) {
			MsgTimer retry = treat<MsgTimer>(node->offs);
			retry.dispatch_attempts++;
			if (retry.dispatch_attempts >= TimerDispatchAttemptLimit) {
				dropped = retry;
				TimerManager.Remove(node);
				drop = true;
			}
			else {
				retry.retry_after = tick + TimerDispatchRetryDelay;
				if (CopyTimerToChain(TimerRetryManager, retry)) {
					TimerManager.Remove(node);
				}
				else {
					treat<MsgTimer>(node->offs).dispatch_attempts = retry.dispatch_attempts;
					treat<MsgTimer>(node->offs).retry_after = retry.retry_after;
					retry_queue_failed = true;
				}
			}
		}
	}
	if (drop) {
		plogerro("SysTimer: drop undeliverable timer action=%u target=%u type=%u timeout=%u attempts=%u",
			(stduint)dropped.action, dropped.target, dropped.message_type,
			dropped.timeout, dropped.dispatch_attempts);
	}
	else if (retry_queue_failed) {
		plogerro("SysTimer: retry queue append failed serial=%u", notify_serial);
	}
}

void Systimex::DispatchExpired() {
	for (;;) {
		MsgTimer expired = {};
		{
			SpinlockLocal guard(&timer_lock);
			Dnode* dispatch = nullptr;
			for (auto* node = TimerManager.Root(); node; node = node->next) {
				auto& timer = treat<MsgTimer>(node->offs);
				if (tick < timer.timeout) break;
				if (timer.action != TimerAction::Callback) {
					dispatch = node;
					break;
				}
			}
			if (!dispatch) {
				timer_dispatch_pending = false;
				return;
			}
			expired = treat<MsgTimer>(dispatch->offs);
			TimerManager.Remove(dispatch);
		}

		switch (expired.action) {
		// Realtime
		case TimerAction::Callback:
			if (expired.hand) expired.hand((pureptr_t)expired.timeout, expired.iden);
			break;
		//
		case TimerAction::ThreadWake:
			(void)Taskman::UnblockThread(expired.target,
				ThreadBlock::BlockReason::BR_Resting);
			break;
		case TimerAction::DriverMessage:
			(void)syssend_async(expired.target, &expired.iden, sizeof(expired.iden),
				expired.message_type);
			break;
		default:
			break;
		}
	}
}

#endif
