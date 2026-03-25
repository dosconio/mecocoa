// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <cpp/Device/UART>
#include <c/driver/timer.h>
#include <c/delay.h>

volatile timeval_t system_time = {};
volatile stduint tick = 0;

// ---- ---- Timer ---- ---- //

#if (_MCCA & 0xFF00) == 0x8600


static int TimerCmp(pureptr_t a, pureptr_t b) {
	return treat<MsgTimer>(((Dnode*)a)->offs).timeout -
		treat<MsgTimer>(((Dnode*)b)->offs).timeout;
}
Dchain TimerManager = { DnodeHeapFreeSimple };

void SysTimer::Initialize() {
	TimerManager.Compare_f = TimerCmp;
}

void SysTimer::Append(stduint timeout, stduint iden, _tocall_ft hand) {
	auto n = TimerManager.Append(new MsgTimer{ tick + timeout, iden, hand });
	if (!n) plogerro("SysTimer::Append failed");
	// ploginfo("SysTimer::Append %u, now %u timers", timeout, TimerManager.Count());
}

#endif

#if _MCCA == 0x8664 && defined(_UEFI)

void delay001ms(void) {
	_TODO
}
#endif

// ---- ---- Rupt ---- ---- //

extern bool ento_gui;
extern uni::LayerManager global_layman;
extern VideoControlInterface* real_pvci;

#if (_MCCA & 0xFF00) == 0x8600
inline void RenderFrameFlush() {
	if (!enable_dubuffer) return;
	global_layman.CheckTimers(tick);
	if (ento_gui && global_layman.pvci && global_layman.is_dirty) {
		static uint64 last_flush_time = 0;
		if (tick - last_flush_time >= 5) { // 100 FPS target (10ms)
			global_layman.is_dirty = false;
			SysMessage msg;
			msg.type = SysMessage::RUPT_FLUSH;
			// Safely clamp coordinates assuming entirely garbage values
			stdsint x = global_layman.dirty_area.x;
			stdsint y = global_layman.dirty_area.y;
			stdsint w = global_layman.dirty_area.width;
			stdsint h = global_layman.dirty_area.height;
			
			if (x < 0) { w += x; x = 0; }
			if (y < 0) { h += y; y = 0; }
			
			stdsint max_w = global_layman.window.width - x;
			stdsint max_h = global_layman.window.height - y;

			if (w > max_w) w = max_w;
			if (h > max_h) h = max_h;

			if (w < 0) w = 0;
			if (h < 0) h = 0;

			msg.args.rect.x = x;
			msg.args.rect.y = y;
			msg.args.rect.w = w;
			msg.args.rect.h = h;
			global_layman.dirty_area = {};
			global_layman.is_dirty = false;
#if _MCCA == 0x8664
			message_queue.Enqueue(msg);
#elif _MCCA == 0x8632
			if (real_pvci && w > 0 && h > 0)
				real_pvci->DrawPoints(msg.args.rect.toRectangle(), global_layman.sheet_buffer);
#endif

			last_flush_time = tick;
		}
	}
}
#endif

#if _MCCA == 0x8632

void blink2();
void Handint_PIT()
{
	// auto push flag by intterrupt module
	// 1000Hz
	mecocoa_global->system_time.mic += 1000;// 1k us = 1ms
	if (mecocoa_global->system_time.mic >= 1000000) {
		mecocoa_global->system_time.mic -= 1000000;
		// mecocoa_global->system_time.sec++;
	}
	static unsigned time = 0;
	time++;
	if (time % 10 == 0) tick++;
	if (time >= 1000) {
		time = 0;
		// mecocoa_global->system_time.sec++;//{TEMP} help RTC	
		if (!ento_gui) {
			Letvar(p, char*, 0xB8001);
			*p ^= 0x70;// make it blink
		}
		else {
			blink2();
		}
	}
	static unsigned time_slice = 0;
	time_slice++;
	#if _GUI_DOUBLE_BUFFER
	RenderFrameFlush();
	#endif
	if (time_slice >= 4) { // switch task
		time_slice = 0;
		if (task_switch_enable) {
			Taskman::Schedule();
		}
	}
}

void blink();
void Handint_RTC()
{
	// 1Hz
	// auto push flag by interrupt module
	// OPEN NMI AFTER READ REG-C, OR ONLY INT ONCE
	outpb(IRQ_RTC, 0x0C);
	innpb(0x71);
	mecocoa_global->system_time.sec++;

	if (!ento_gui) {
		Letvar(p, char*, 0xB8003);
		*p ^= 0x70;// make it blink
	}
	else blink();
}

#endif
#if _MCCA == 0x8664 && defined(_UEFI)
__attribute__((/*interrupt, */target("general-regs-only")))// the stack is ready
void Handint_LAPICT(/*InterruptFrame* frame*/) {
	tick = tick + 1;// mecocoa_global->system_time.mic++
	sendEOI();
	while (TimerManager.Root()) {
		auto crt = treat<MsgTimer>(TimerManager.Root()->offs);
		if (tick >= crt.timeout) {
			TimerManager.Remove(TimerManager.Root());
			if (crt.hand)
				crt.hand((pureptr_t)crt.timeout, crt.iden);// realtime process
			else {
				message_queue.Enqueue(SysMessage{ SysMessage::RUPT_TIMER, crt });
			}
		}
		else break;
	}
	#if _GUI_DOUBLE_BUFFER
	RenderFrameFlush();
	#endif

	Taskman::Schedule();
}
#endif
#if (_MCCA & 0xFF00) == 0x1000

uint64 last_schepoint;
void timer_handler()
{
	tick++;
	// ploginfo("tick: %d\n", tick);
	last_schepoint += TIMER_INTERVAL;
	clint.Load(getMHARTID(), last_schepoint);
	Taskman::Schedule();
}

#endif
