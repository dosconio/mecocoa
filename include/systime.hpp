// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Handler, Systick and Soft Timer
// Copyright: Dosconio Mecocoa, BSD 3-Clause License


#ifndef SYSTIME_HPP_
#define SYSTIME_HPP_

#include <cpp/atomic>
#include <cpp/Device/_Timer.hpp>
#include <cpp/ISO_IEC_STD/memory>

struct ThreadTimerState {
	uni::Atomic<bool> closed{ false };
};

class Systime {
public:
	static bool Initialize() { SysTimer::Initialize(); return true; }
	
public: // system date and time

public: // manage devices of timers(RTC, vmware-datime...) and datetimes

public: // soft timer

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
	static stduint CancelDeferredCallback(stduint iden, _tocall_ft hand = nullptr);
	static void
		CancelThreadWake(stduint tid);
	// Called by kernel exit and reclamation paths; safe while scheduler_lock is held.
	static void CancelThreadTimers(const uni::SharedPtr<ThreadTimerState>& owner);

public: // systick

};

#endif
