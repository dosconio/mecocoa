// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Handler, Systick and Soft Timer
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include "../include/devsman-storage.hpp"
#include <c/driver/timer.h>

// ---- ---- ---- ---- SOFT-TIM ---- ---- ---- ---- //

volatile timeval_t system_time = {};
uni::Atomic<stduint> tick = 0;

namespace {
	uni::Atomic<bool> timer_cleanup_pending{false};
}

// Called by kernel exit paths; never acquire timer_lock or wait for dispatch.
void Systimex::CancelThreadTimers(const uni::SharedPtr<ThreadTimerState>& owner) {
	if (!owner) return;
	owner->closed.store(true, uni::MemoryOrder_Release);
	timer_cleanup_pending.store(true, uni::MemoryOrder_Release);
}

#if !defined (_MPU_STM32MP13) //{TEMP}

namespace {
	enum class TimerAction : byte {
		Callback,
		DeferredCallback,
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
		uni::SharedPtr<ThreadTimerState> owner;
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
// Dchain releases the node itself after this callback destroys its payload.
static void FreeMsgTimer(pureptr_t value) {
	auto* node = static_cast<Dnode*>(value);
	delete static_cast<MsgTimer*>(node->offs);
}

// Timer management
Dchain TimerManager = { FreeMsgTimer };
Dchain TimerRetryManager = { FreeMsgTimer };
Spinlock timer_lock;

namespace {
	bool timer_dispatch_pending = false;
	stduint timer_serial = 0;

	// Only thread-targeted timers depend on the target's lifetime state.
	bool IsClosedThreadTimer(const MsgTimer& timer) {
		if (timer.action != TimerAction::ThreadWake && timer.action != TimerAction::DriverMessage) return false;
		return !timer.owner || timer.owner->closed.load(uni::MemoryOrder_Acquire);
	}

	// Called by the Devsman timer service while holding timer_lock.
	void RemoveClosedThreadTimers(Dchain& timers) {
		for (auto* node = timers.Root(); node; ) {
			auto* next = node->next;
			if (IsClosedThreadTimer(treat<MsgTimer>(node->offs))) timers.Remove(node);
			node = next;
		}
	}

	bool CopyTimerToChain(Dchain& destination, const MsgTimer& timer) {
		auto* copy = ::new (std::nothrow_t{}) MsgTimer(timer);
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

	stduint CancelCallbackFromChain(Dchain& timers, stduint iden, _tocall_ft hand,
		TimerAction action = TimerAction::DeferredCallback) {
		stduint cancelled = 0;
		for (auto* node = timers.Root(); node; ) {
			auto* next = node->next;
			auto& timer = treat<MsgTimer>(node->offs);
			if (timer.action == action && timer.iden == iden &&
				(!hand || timer.hand == hand)) {
				timers.Remove(node);
				++cancelled;
			}
			node = next;
		}
		return cancelled;
	}

	bool AppendTimer(stduint timeout, MsgTimer timer) {
		SpinlockLocal guard(&timer_lock);
		if (IsClosedThreadTimer(timer)) return false;
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
	timer.owner = Taskman::AcquireThreadTimerState(tid);
	if (!timer.owner) return false;
	timer.action = TimerAction::ThreadWake;
	timer.target = tid;
	return AppendTimer(timeout, timer);
}

bool Systimex::AppendDriverMessage(stduint timeout, stduint target_tid, stduint message_type, stduint value) {
	MsgTimer timer = {};
	timer.owner = Taskman::AcquireThreadTimerState(target_tid);
	if (!timer.owner) return false;
	timer.iden = value;
	timer.action = TimerAction::DriverMessage;
	timer.target = target_tid;
	timer.message_type = message_type;
	return AppendTimer(timeout, timer);
}

bool Systimex::AppendDeferredCallback(stduint timeout, stduint iden,
	_tocall_ft hand) {
	if (!hand) return false;
	MsgTimer timer = {};
	timer.iden = iden;
	timer.hand = hand;
	timer.action = TimerAction::DeferredCallback;
	return AppendTimer(timeout, timer);
}

stduint Systimex::CancelDeferredCallback(stduint iden, _tocall_ft hand) {
	SpinlockLocal guard(&timer_lock);
	return CancelCallbackFromChain(TimerManager, iden, hand) +
		CancelCallbackFromChain(TimerRetryManager, iden, hand);
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
			// Cleanup must reach Devsman even when no timer has expired yet.
			if (!run_callback && !timer_dispatch_pending && (dispatch || timer_cleanup_pending.load(uni::MemoryOrder_Acquire))) {
				notify_serial = dispatch ? treat<MsgTimer>(dispatch->offs).serial : 0;
				timer_dispatch_pending = true;
				notify = true;
			}
		}
		if (run_callback && callback.hand) {
			callback.hand((pureptr_t)callback.timeout, callback.iden);
		}
		if (!run_callback) break;
	}
	#if (_MCCA & 0xFF00) == 0x8600
	Iommu::NotifyFaultService();
	#endif
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
		// Keep cleanup pending on notification failure so a later tick retries it.
		auto* node = notify_serial ? FindTimerBySerial(TimerManager, notify_serial) : nullptr;
		if (node && IsClosedThreadTimer(treat<MsgTimer>(node->offs))) {
			TimerManager.Remove(node);
			node = nullptr;
		}
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
	if (drop && !IsClosedThreadTimer(dropped)) {
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
			if (timer_cleanup_pending.exchange(false, uni::MemoryOrder_Acq_Rel)) {
				RemoveClosedThreadTimers(TimerManager);
				RemoveClosedThreadTimers(TimerRetryManager);
			}
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

		// The target may have closed after this timer was copied out of the queue.
		if (IsClosedThreadTimer(expired)) continue;

		switch (expired.action) {
		// Realtime
		case TimerAction::Callback:
			if (expired.hand) expired.hand((pureptr_t)expired.timeout, expired.iden);
			break;
		// Deferred to the Devsman thread.
		case TimerAction::DeferredCallback:
			if (expired.hand) expired.hand((pureptr_t)expired.timeout, expired.iden);
			break;
		//
		case TimerAction::ThreadWake:
			(void)Taskman::UnblockThread(expired.target,
				ThreadBlock::BlockReason::BR_Resting);
			break;
		case TimerAction::DriverMessage: {
			if (!syssend_async(expired.target, &expired.iden, sizeof(expired.iden),
				expired.message_type)) break;
			if (IsClosedThreadTimer(expired)) break;
			expired.dispatch_attempts++;
			if (expired.dispatch_attempts >= TimerDispatchAttemptLimit) {
				plogerro("SysTimer: drop undeliverable driver message target=%u type=%u attempts=%u",
					expired.target, expired.message_type, expired.dispatch_attempts);
				break;
			}
			(void)AppendTimer(TimerDispatchRetryDelay, expired);
			break;
		}
		default:
			break;
		}
	}
}

#if (_MCCA & 0xFF00) == 0x8600
namespace Devs {
	/* Keep the storage adapter here to use the fallible timer backend without adding
	   storage-specific entry points to Systimex or changing ordinary ThreadWake dispatch. */
	bool StorageTimer::ArmWake(stduint delay, stduint tid, stduint generation) {
		if (!delay || tid == ~stduint(0) || !generation) return false;
		Cancel();
		target = tid;
		callback = nullptr;
		wake_generation = generation;
		deadline = stduint(tick) + delay;
		MsgTimer timer{};
		timer.action = TimerAction::Callback;
		timer.iden = generation;
		timer.hand = Wake;
		armed = AppendTimer(delay, timer);
		return armed;
	}

	void StorageTimer::Cancel() {
		if (!armed) return;
		if (callback) Systimex::CancelDeferredCallback(target, callback);
		else {
			SpinlockLocal guard(&timer_lock);
			(void)CancelCallbackFromChain(TimerManager, wake_generation, Wake, TimerAction::Callback);
			(void)CancelCallbackFromChain(TimerRetryManager, wake_generation, Wake, TimerAction::Callback);
		}
		armed = false;
	}
}
#endif

#endif
