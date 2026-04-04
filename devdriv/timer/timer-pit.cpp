// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: PIT
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include <c/driver/PIT.h>

_ESYM_C void R_PIT_INIT();

#if _MCCA == 0x8632
#if 1
__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_PIT{
	.init = R_PIT_INIT,
	.name = "PIT",
};
#endif

void R_PIT_INIT() {
	IC[IRQ_PIT].setRange(mglb(Handint_PIT_Entry), SegCo32);
	PIT_Init();
}

extern volatile stduint tick;
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

#endif
