// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: PIT
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include <c/driver/PIT.h>

_ESYM_C void R_PIT_INIT();

#if _MCCA == 0x8632
namespace {
	constexpr uint32 PIT_INPUT_FREQUENCY = 1193182;
	Spinlock pit_channel2_lock;
}

bool PIT_SetChannel2Frequency(uint32 frequency_hz) {
	if (!frequency_hz || frequency_hz > PIT_INPUT_FREQUENCY) return false;

	uint32 divisor = (PIT_INPUT_FREQUENCY + frequency_hz / 2) / frequency_hz;
	if (!divisor || divisor > 0xFFFF) return false;

	SpinlockLocal guard(&pit_channel2_lock);
	outpb(PORT_TIMER_MODE, 0b10110110);
	outpb(PORT_PIT_TIMER2, byte(divisor));
	outpb(PORT_PIT_TIMER2, byte(divisor >> 8));
	return true;
}

#if 1
__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_PIT{
	.init = R_PIT_INIT,
	.name = "PIT",
};
#endif

void R_PIT_INIT() {
	const bool interrupt_ready = bool(Devsman::EnableInterrupt(InterruptSource::Fixed(IRQ_PIT), Handint_PIT, PIT_Init));
	if (!interrupt_ready)
		plogerro("[PIT] Cannot enable the timer interrupt");
	if (auto* node = Devsman::RegisterPlatformDevice("pit@8254", "pit-8254")) {
		Devsman::AddIoPortResource(node, 0, 0x40, 4);
		Devsman::AddIrqResource(node, IRQ_PIT);
		if (interrupt_ready) {
			TimeDevice device{};
			device.info.periodic_event = true;
			device.info.event_frequency = Systime::GetTickFrequency();
			if (!Systime::AttachDevice(node, device) || !Systime::BindTickDevice(node))
				plogerro("[PIT] cannot attach system tick capability");
		}
	}
}

void RenderFrameFlush();
void Handint_PIT()
{
	IC.SendEOI(IRQ_PIT); // Acknowledge interrupt
	// auto push flag by intterrupt module
	// 1000Hz
	mecocoa_global->system_time.mic += 1000;// 1k us = 1ms
	if (mecocoa_global->system_time.mic >= 1000000) {
		mecocoa_global->system_time.mic -= 1000000;
		// mecocoa_global->system_time.sec++;
	}
	static unsigned time = 0;
	time++;
	static unsigned tick_fraction = 0;
	tick_fraction += CONFIG_SysTickFreq;
	if (tick_fraction >= 1000) {
		tick_fraction -= 1000;
		Systime::OnTick();
	}
	if (time >= 1000) {
		time = 0;
		// mecocoa_global->system_time.sec++;//{TEMP} help RTC	
		if (!Consman::ento_gui) {
			Letvar(p, char*, 0xB8001);
			*p ^= 0x70;// make it blink
		}
	}
	static unsigned time_slice = 0;
	time_slice++;
	#if _GUI_DOUBLE_BUFFER
	RenderFrameFlush();
	#endif
	if (time_slice == 2) {
		SendWakeAllApsIPI();
	}
	if (time_slice >= 4) { // switch task
		time_slice = 0;
		#if _SYS_MULTICORE
		WakeOneIdleApForReadyWork();
		#endif
		Taskman::Schedule();
	}
}

#endif
