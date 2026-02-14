// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <c/dnode.h>
#include <cpp/interrupt>
#include <cpp/Device/UART>
#include <c/driver/timer.h>
#include "../include/taskman.hpp"

// ---- ---- Timer ---- ---- //

volatile timeval_t system_time = {};

volatile stduint tick = 0;

static void TimerFree(pureptr_t inp) {
	Letvar(nod, Dnode*, inp);
	memf(nod->offs);
}
static int TimerCmp(pureptr_t a, pureptr_t b) {
	return treat<MsgTimer>(((Dnode*)a)->offs).timeout -
		treat<MsgTimer>(((Dnode*)b)->offs).timeout;
}
Dchain TimerManager = { TimerFree };

void SysTimer::Initialize() {
	TimerManager.Compare_f = TimerCmp;
}

void SysTimer::Append(stduint timeout, stduint iden, _tocall_ft hand) {
	TimerManager.Append(new MsgTimer{ timeout, iden, hand });
	// ploginfo("SysTimer::Append %u, now %u timers", timeout, TimerManager.Count());
}


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
	if (time_slice >= 20) { // switch task
		time_slice = 0;
		if (task_switch_enable) {
			switch_task();
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
__attribute__((interrupt, target("general-regs-only")))
void Handint_LAPICT(InterruptFrame* frame) {
	tick = tick + 1;// mecocoa_global->system_time.mic++
	while (TimerManager.Root()) {
		auto& crt = treat<MsgTimer>(TimerManager.Root()->offs);
		if (tick >= crt.timeout) {
			if (crt.hand)
				crt.hand((pureptr_t)crt.timeout, crt.iden);// realtime process
			else {
				message_queue.Enqueue(SysMessage{ SysMessage::RUPT_TIMER, crt });
			}
			TimerManager.Remove(TimerManager.Root());
		}
		else break;
	}
	sendEOI();
}
#endif

