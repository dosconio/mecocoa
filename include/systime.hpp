// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Handler, Systick and Soft Timer
// Copyright: Dosconio Mecocoa, BSD 3-Clause License


#ifndef SYSTIME_HPP_
#define SYSTIME_HPP_

#include <c/datime.h>
#include <cpp/atomic>
#include <cpp/Device/_Timer.hpp>
#include <cpp/ISO_IEC_STD/memory>

struct DeviceNode;

enum class TimeBasis : byte { Unknown, UTC, Local };

struct TimeDeviceInfo {
	bool readable_date = false, writable_date = false;
	bool readable_counter = false, periodic_event = false;
	bool selected_date = false, selected_tick = false;
	bool last_read_ok = false;
	TimeBasis basis = TimeBasis::Unknown;
	uint32 date_resolution_ms = 0;
	uint64 counter_frequency = 0;
	byte counter_bits = 0;
	stduint event_frequency = 0;
	unsigned priority = 0;
};

struct TimeDevice {
	TimeDeviceInfo info;
	void* context = nullptr;
	bool (*ReadDate)(void*, datime_t&) = nullptr;
	bool (*WriteDate)(void*, const datime_t&) = nullptr;
	bool (*ReadCounter)(void*, uint64&) = nullptr;
};

struct DateTimeConfig {
	sint32 utc_offset_minutes = 0;
	TimeBasis unknown_device_basis = TimeBasis::Unknown;
};

enum class SystemTimeOrigin : byte { Unset, Manual, Device };

struct SystemTimeInfo {
	bool valid = false;
	SystemTimeOrigin origin = SystemTimeOrigin::Unset;
	DateTimeConfig config;
	uint32 resolution_us = 0;
};

struct ThreadTimerState {
	uni::Atomic<bool> closed{ false };
};

class Systime {
public:
	static bool Initialize() { SysTimer::Initialize(); return true; }
	
public: // system date and time

	// Thread-context calls. Fixed offsets are minutes east of UTC, within -1440..1440.
	static bool
		ConfigureDateTime(const DateTimeConfig& config);
	// UTC seconds since 1970-01-01, with 0 <= mic < 1000000. Failure preserves output.
	static bool
		GetSystemTime(timeval_t& result);
	static bool
		SetSystemTime(const timeval_t& value);
	// Calendar dates use the configured local offset, full years 0..9999, and zero-based months.
	static bool
		GetDateTime(datime_t& result);
	static bool
		SetDateTime(const datime_t& value);
	// Reads the selected source, then falls back by priority. A concurrent setting cancels the import.
	static bool
		SyncFromDevice(DeviceNode* node = nullptr);
	// Writes a snapshot; the system clock keeps running if the backend is absent or fails.
	static bool
		SyncToDevice(DeviceNode* node = nullptr);
	// Available even before the date has been set; inspect result.valid.
	static void
		GetSystemTimeInfo(SystemTimeInfo& result);

public: // manage devices of timers(RTC, vmware-datime...) and datetimes

	// The caller serializes topology changes through Devsman. Context stays alive until detach succeeds.
	static bool
		AttachDevice(DeviceNode* node, const TimeDevice& device);
	// Stop the backend first. Retry if a callback is active; unbind a tick source before detaching.
	static bool
		DetachDevice(DeviceNode* node);
	static bool
		GetTimeDeviceInfo(DeviceNode* node, TimeDeviceInfo& result);
	// A null node clears the selected date source.
	static bool
		SelectDateTimeDevice(DeviceNode* node);
	static auto
		GetDateTimeDevice() -> DeviceNode*;
	// Records the already configured interrupt source; the caller stops the old source before rebinding.
	static bool
		BindTickDevice(DeviceNode* node);
	/* Thread-context calls. Dates retain the device basis; failure leaves output unchanged.
	   A null read node tries the selected source, then other sources in priority order. */
	static bool
		ReadDeviceDateTime(datime_t& result, DeviceNode* node = nullptr);
	static bool
		WriteDeviceDateTime(const datime_t& date, DeviceNode* node = nullptr);
	static bool
		ReadDeviceCounter(DeviceNode* node, uint64& result);

public: // soft timer

	// Delays are logical ticks and must be less than half the native tick range.

	static void
		CollectExpired();
	static void
		DispatchExpired();
	static bool
		AppendThreadWake(stduint timeout, stduint tid);
	static bool
		AppendDriverMessage(stduint timeout, stduint target_tid,
			stduint message_type, stduint value = 0);
	static bool
		AppendDeferredCallback(stduint timeout, stduint iden,
			_tocall_ft hand);
	static auto CancelDeferredCallback(stduint iden, _tocall_ft hand = nullptr) -> stduint;
	static void
		CancelThreadWake(stduint tid);
	// Called by kernel exit and reclamation paths; safe while scheduler_lock is held.
	static void CancelThreadTimers(const uni::SharedPtr<ThreadTimerState>& owner);

public: // systick

	// The selected system timer calls this once per logical tick.
	static void OnTick();
	// Native-width counter, wrapping modulo the width of stduint.
	static stduint GetTick();
	static stduint GetTickFrequency();
	// Convert a duration to milliseconds, truncating fractional milliseconds.
	static uint64 TicksToMilliseconds(stduint ticks);

};

#endif
