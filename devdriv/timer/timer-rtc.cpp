// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: RTC
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include <c/driver/RealtimeClock.h>

_ESYM_C void R_RTC_INIT();

#if (_MCCA & 0xFF00) == 0x8600
namespace {
	Spinlock cmos_time_lock;
	bool ReadCmosDate(void*, datime_t& date) {
		SpinlockLocal guard(&cmos_time_lock);
		return CMOS_ReadDateTime(&date) != 0;
	}
}

#if 1
__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_RTC{
	.init = R_RTC_INIT,
	.name = "RTC",
};
#endif

void R_RTC_INIT() {
	#if _MCCA == 0x8632
	const bool interrupt_ready = bool(Devsman::EnableInterrupt(
		InterruptSource::Fixed(IRQ_RTC), Handint_RTC, RTC_Init));
	#endif
	if (auto* node = Devsman::RegisterPlatformDevice("rtc@cmos", "cmos-rtc")) {
		Devsman::AddIoPortResource(node, 0, PORT_CMOS_ADDR, 2);
		#if _MCCA == 0x8632
		if (interrupt_ready) Devsman::AddIrqResource(node, IRQ_RTC);
		#endif
		TimeDevice device{};
		device.info.readable_date = true;
		device.info.date_resolution_ms = 1000;
		device.info.priority = 100;
		device.ReadDate = ReadCmosDate;
		if (!Systime::AttachDevice(node, device)) plogerro("[RTC] cannot attach time capability");
		else (void)Systime::SelectDateTimeDevice(node);
	}
}

#if _MCCA == 0x8632
void Handint_RTC()
{
	// 1Hz
	// auto push flag by interrupt module
	// OPEN NMI AFTER READ REG-C, OR ONLY INT ONCE
	{
		SpinlockLocal guard(&cmos_time_lock);
		const byte saved_index = innpb(PORT_CMOS_ADDR);
		outpb(PORT_CMOS_ADDR, CMOS_STATUS_C | CMOS_NMI);
		innpb(PORT_CMOS_DATA);
		outpb(PORT_CMOS_ADDR, saved_index);
	}
	mecocoa_global->system_time.sec++;

	if (!Consman::ento_gui) {
		Letvar(p, char*, 0xB8003);
		*p ^= 0x70;// make it blink
	}

	IC.SendEOI(IRQ_RTC); // Acknowledge interrupt
}
#endif

#endif
