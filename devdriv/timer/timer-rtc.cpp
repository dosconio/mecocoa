// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: RTC
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include <c/driver/RealtimeClock.h>

_ESYM_C void R_RTC_INIT();

#if _MCCA == 0x8632
#if 1
__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_RTC{
	.init = R_RTC_INIT,
	.name = "RTC",
};
#endif

void R_RTC_INIT() {
	IC[IRQ_RTC].setRange(mglb(Handint_RTC_Entry), SegCo32);
	RTC_Init();
}

void blink() {
	extern GloScreenARGB8888 local_vci;
	static bool b = false;
	local_vci.DrawRectangle(Rectangle(Point(0, 0), Size2(8, 16), b ? Color::Black : Color::White));
	b = !b;
}
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
