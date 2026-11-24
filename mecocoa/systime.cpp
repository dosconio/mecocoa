// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Handler, Systick and Soft Timer
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../include/mecocoa.hpp"
#include "../include/devsman-storage.hpp"
#include <c/driver/timer.h>

/* [SYSTIME]
* system date and time
* manage devices of timers(RTC, vmware-datime...) and datetimes
* soft timer
* systick
*/


// ---- ---- ---- ---- TIME DEVICES ---- ---- ---- ---- //

struct TimeDeviceBinding {
	DeviceNode* node;
	TimeDevice device;
	TimeDeviceBinding* next = nullptr;
	stduint users = 0;
	bool last_read_ok = false;

	TimeDeviceBinding(DeviceNode* node, const TimeDevice& device) : node(node), device(device) {}
};

namespace {
	Spinlock time_device_lock;
	TimeDeviceBinding* time_devices = nullptr;
	TimeDeviceBinding* date_device = nullptr;
	TimeDeviceBinding* tick_device = nullptr;

	bool ValidDeviceDate(const datime_t& date) {
		return date.year >= 0 && date.year <= 9999 && date.month < 12 &&
			date.mday >= 1 && date.mday <= moondays(date.year, int(date.month) + 1) &&
			date.hour < 24 && date.minute < 60 && date.second < 60;
	}

	TimeDeviceBinding* FindTimeDevice(DeviceNode* node) {
		for (auto* entry = time_devices; entry; entry = entry->next)
			if (entry->node == node) return entry;
		return nullptr;
	}

	TimeDeviceBinding* AcquireTimeDevice(DeviceNode* node) {
		SpinlockLocal guard(&time_device_lock);
		auto* entry = node ? FindTimeDevice(node) : date_device;
		if (entry) ++entry->users;
		return entry;
	}

	bool ReadTimeDevice(TimeDeviceBinding* entry, datime_t& date) {
		return entry->device.ReadDate &&
			entry->device.ReadDate(entry->device.context, date) && ValidDeviceDate(date);
	}
}

bool Systime::AttachDevice(DeviceNode* node, const TimeDevice& device) {
	if (!node || device.info.readable_date != bool(device.ReadDate) ||
		device.info.writable_date != bool(device.WriteDate) ||
		device.info.readable_counter != bool(device.ReadCounter) ||
		(device.ReadDate && !device.info.date_resolution_ms) ||
		(device.ReadCounter && (!device.info.counter_frequency ||
			!device.info.counter_bits || device.info.counter_bits > 64)) ||
		(device.info.periodic_event && !device.info.event_frequency) ||
		(!device.ReadDate && !device.WriteDate && !device.ReadCounter && !device.info.periodic_event))
		return false;
	auto* prepared = new (std::nothrow_t{}) TimeDeviceBinding(node, device);
	if (!prepared) return false;
	bool attached = false;
	{
		SpinlockLocal guard(&time_device_lock);
		if (!node->fields.time_device) {
			auto** slot = &time_devices;
			while (*slot && (*slot)->device.info.priority >= device.info.priority) slot = &(*slot)->next;
			prepared->next = *slot;
			*slot = prepared;
			node->fields.time_device = prepared;
			attached = true;
		}
	}
	if (!attached) delete prepared;
	return attached;
}

bool Systime::DetachDevice(DeviceNode* node) {
	TimeDeviceBinding* removed = nullptr;
	{
		SpinlockLocal guard(&time_device_lock);
		auto** slot = &time_devices;
		while (*slot && (*slot)->node != node) slot = &(*slot)->next;
		if (!*slot) return true;
		if ((*slot)->users || *slot == tick_device) return false;
		removed = *slot;
		*slot = removed->next;
		if (date_device == removed) date_device = nullptr;
		node->fields.time_device = nullptr;
	}
	delete removed;
	return true;
}

bool Systime::GetTimeDeviceInfo(DeviceNode* node, TimeDeviceInfo& result) {
	SpinlockLocal guard(&time_device_lock);
	auto* entry = FindTimeDevice(node);
	if (!entry) return false;
	result = entry->device.info;
	result.selected_date = entry == date_device;
	result.selected_tick = entry == tick_device;
	result.last_read_ok = entry->last_read_ok;
	return true;
}

bool Systime::SelectDateTimeDevice(DeviceNode* node) {
	if (!node) {
		SpinlockLocal guard(&time_device_lock);
		date_device = nullptr;
		return true;
	}
	auto* entry = AcquireTimeDevice(node);
	if (!entry) return false;
	datime_t date{};
	const bool ok = ReadTimeDevice(entry, date);
	{
		SpinlockLocal guard(&time_device_lock);
		entry->last_read_ok = ok;
		if (ok) date_device = entry;
		--entry->users;
	}
	return ok;
}

DeviceNode* Systime::GetDateTimeDevice() {
	SpinlockLocal guard(&time_device_lock);
	return date_device ? date_device->node : nullptr;
}

bool Systime::BindTickDevice(DeviceNode* node) {
	SpinlockLocal guard(&time_device_lock);
	auto* entry = node ? FindTimeDevice(node) : nullptr;
	if (node && (!entry || !entry->device.info.periodic_event ||
		entry->device.info.event_frequency != GetTickFrequency())) return false;
	tick_device = entry;
	return true;
}

bool Systime::ReadDeviceDateTime(datime_t& result, DeviceNode* node) {
	auto* preferred = AcquireTimeDevice(node);
	if (preferred) {
		datime_t date{};
		const bool ok = ReadTimeDevice(preferred, date);
		{
			SpinlockLocal guard(&time_device_lock);
			preferred->last_read_ok = ok;
			if (!node) date_device = ok ? preferred : nullptr;
			if (ok || node) --preferred->users;
		}
		if (ok) { result = date; return true; }
	}
	if (node) return false;
	TimeDeviceBinding* entry;
	{
		SpinlockLocal guard(&time_device_lock);
		entry = time_devices;
		if (entry) ++entry->users;
	}
	while (entry) {
		datime_t date{};
		const bool ok = entry != preferred && ReadTimeDevice(entry, date);
		TimeDeviceBinding* next;
		{
			SpinlockLocal guard(&time_device_lock);
			if (entry != preferred) entry->last_read_ok = ok;
			next = ok ? nullptr : entry->next;
			if (next) ++next->users;
			if (ok) date_device = entry;
			--entry->users;
			if (ok && preferred) --preferred->users;
		}
		if (ok) { result = date; return true; }
		entry = next;
	}
	if (preferred) {
		SpinlockLocal guard(&time_device_lock);
		--preferred->users;
	}
	return false;
}

bool Systime::WriteDeviceDateTime(const datime_t& date, DeviceNode* node) {
	if (!ValidDeviceDate(date)) return false;
	auto* entry = AcquireTimeDevice(node);
	if (!entry) return false;
	const bool ok = entry->device.WriteDate && entry->device.WriteDate(entry->device.context, date);
	{
		SpinlockLocal guard(&time_device_lock);
		--entry->users;
	}
	return ok;
}

bool Systime::ReadDeviceCounter(DeviceNode* node, uint64& result) {
	if (!node) return false;
	auto* entry = AcquireTimeDevice(node);
	if (!entry) return false;
	uint64 value = 0;
	const bool ok = entry->device.ReadCounter && entry->device.ReadCounter(entry->device.context, value);
	{
		SpinlockLocal guard(&time_device_lock);
		--entry->users;
	}
	if (ok) result = value;
	return ok;
}

// ---- ---- ---- ---- SOFT-TIM ---- ---- ---- ---- //

volatile timeval_t system_time = {};
uni::Atomic<stduint> tick = 0;

namespace {
	uni::Atomic<bool> timer_cleanup_pending{false};
}

// Called by kernel exit paths; never acquire timer_lock or wait for dispatch.
void Systime::CancelThreadTimers(const uni::SharedPtr<ThreadTimerState>& owner) {
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
	static constexpr stduint MaxTimerDelay = ~stduint(0) >> 1;

	bool HasReached(stduint now, stduint deadline) {
		return stdsint(now - deadline) >= 0;
	}

	int CompareDeadlines(stduint left, stduint right) {
		// Order overdue entries before future entries using the same tick snapshot.
		const stduint now = Systime::GetTick();
		const stdsint left_delay = stdsint(left - now);
		const stdsint right_delay = stdsint(right - now);
		if (left_delay < right_delay) return -1;
		return left_delay > right_delay ? 1 : 0;
	}
}

static int TimerCmp(pureptr_t a, pureptr_t b) {
	return CompareDeadlines(treat<MsgTimer>(((Dnode*)a)->offs).timeout,
		treat<MsgTimer>(((Dnode*)b)->offs).timeout);
}
static int TimerRetryCmp(pureptr_t a, pureptr_t b) {
	return CompareDeadlines(treat<MsgTimer>(((Dnode*)a)->offs).retry_after,
		treat<MsgTimer>(((Dnode*)b)->offs).retry_after);
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
		if (timeout > MaxTimerDelay) {
			plogerro("SysTimer: delay exceeds half the tick range");
			return false;
		}
		SpinlockLocal guard(&timer_lock);
		if (IsClosedThreadTimer(timer)) return false;
		timer.timeout = Systime::GetTick() + timeout;
		timer.retry_after = timer.timeout;
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

bool Systime::AppendThreadWake(stduint timeout, stduint tid) {
	MsgTimer timer = {};
	timer.owner = Taskman::AcquireThreadTimerState(tid);
	if (!timer.owner) return false;
	timer.action = TimerAction::ThreadWake;
	timer.target = tid;
	return AppendTimer(timeout, timer);
}

bool Systime::AppendDriverMessage(stduint timeout, stduint target_tid, stduint message_type, stduint value) {
	MsgTimer timer = {};
	timer.owner = Taskman::AcquireThreadTimerState(target_tid);
	if (!timer.owner) return false;
	timer.iden = value;
	timer.action = TimerAction::DriverMessage;
	timer.target = target_tid;
	timer.message_type = message_type;
	return AppendTimer(timeout, timer);
}

bool Systime::AppendDeferredCallback(stduint timeout, stduint iden,
	_tocall_ft hand) {
	if (!hand) return false;
	MsgTimer timer = {};
	timer.iden = iden;
	timer.hand = hand;
	timer.action = TimerAction::DeferredCallback;
	return AppendTimer(timeout, timer);
}

stduint Systime::CancelDeferredCallback(stduint iden, _tocall_ft hand) {
	SpinlockLocal guard(&timer_lock);
	return CancelCallbackFromChain(TimerManager, iden, hand) +
		CancelCallbackFromChain(TimerRetryManager, iden, hand);
}

void Systime::CancelThreadWake(stduint tid) {
	SpinlockLocal guard(&timer_lock);
	CancelThreadWakeFromChain(TimerManager, tid);
	CancelThreadWakeFromChain(TimerRetryManager, tid);
}

void Systime::CollectExpired() {
	bool notify = false;
	stduint notify_serial = 0;
	for (;;) {
		MsgTimer callback = {};
		bool run_callback = false;
		{
			SpinlockLocal guard(&timer_lock);
			const stduint now = GetTick();
			if (!timer_dispatch_pending) {
				auto* retry = TimerRetryManager.Root();
				if (retry && HasReached(now, treat<MsgTimer>(retry->offs).retry_after)) {
					MsgTimer timer = treat<MsgTimer>(retry->offs);
					timer.retry_after = now;
					if (CopyTimerToChain(TimerManager, timer)) {
						TimerRetryManager.Remove(retry);
					}
				}
			}

			Dnode* dispatch = nullptr;
			for (auto* node = TimerManager.Root(); node; node = node->next) {
				auto& timer = treat<MsgTimer>(node->offs);
				if (!HasReached(now, timer.timeout)) break;
				if (timer.action == TimerAction::Callback) {
					callback = timer;
					TimerManager.Remove(node);
					run_callback = true;
					break;
				}
				if (!timer_dispatch_pending && !dispatch &&
					(!timer.dispatch_attempts || HasReached(now, timer.retry_after))) {
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
				retry.retry_after = GetTick() + TimerDispatchRetryDelay;
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

void Systime::DispatchExpired() {
	for (;;) {
		MsgTimer expired = {};
		{
			SpinlockLocal guard(&timer_lock);
			if (timer_cleanup_pending.exchange(false, uni::MemoryOrder_Acq_Rel)) {
				RemoveClosedThreadTimers(TimerManager);
				RemoveClosedThreadTimers(TimerRetryManager);
			}
			const stduint now = GetTick();
			Dnode* dispatch = nullptr;
			for (auto* node = TimerManager.Root(); node; node = node->next) {
				auto& timer = treat<MsgTimer>(node->offs);
				if (!HasReached(now, timer.timeout)) break;
				if (timer.action != TimerAction::Callback &&
					(!timer.dispatch_attempts || HasReached(now, timer.retry_after))) {
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
	// Keep the storage adapter here to use the fallible timer backend without adding storage-specific entry points to Systime or changing ordinary ThreadWake dispatch.
	bool StorageTimer::ArmWake(stduint delay, stduint tid, stduint generation) {
		if (!delay || delay > MaxTimerDelay || tid == ~stduint(0) || !generation) return false;
		Cancel();
		target = tid;
		callback = nullptr;
		wake_generation = generation;
		deadline = Systime::GetTick() + delay;
		MsgTimer timer{};
		timer.action = TimerAction::Callback;
		timer.iden = generation;
		timer.hand = Wake;
		armed = AppendTimer(delay, timer);
		return armed;
	}

	void StorageTimer::Cancel() {
		if (!armed) return;
		if (callback) Systime::CancelDeferredCallback(target, callback);
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

// ---- ---- ---- ---- TICK ---- ---- ---- ---- //


void Systime::OnTick() {
	tick.fetch_add(1, uni::MemoryOrder_Relaxed);
#if !defined(_MPU_STM32MP13)
	CollectExpired();
#endif
}

stduint Systime::GetTick() {
	return tick.load(uni::MemoryOrder_Relaxed);
}

stduint Systime::GetTickFrequency() {
	return CONFIG_SysTickFreq;
}

uint64 Systime::TicksToMilliseconds(stduint ticks) {
	const uint64 hz = GetTickFrequency();
	return (ticks / hz) * 1000 + (ticks % hz) * 1000 / hz;
}

