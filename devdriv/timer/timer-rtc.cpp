// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: RTC
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include <c/driver/RealtimeClock.h>
#if defined(_MCU_STM32H7x)
#if __has_include("unicfg.hpp")
#include "unicfg.hpp"
#else
#error "BoardConfig is not defined"
#endif
#endif

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
		device.info.basis = TimeBasis::UTC;
		device.info.date_resolution_ms = 1000;
		device.info.priority = 100;
		device.ReadDate = ReadCmosDate;
		if (!Systime::AttachDevice(node, device)) plogerro("[RTC] cannot attach time capability");
		else if (Systime::SelectDateTimeDevice(node)) {
			SystemTimeInfo info{};
			Systime::GetSystemTimeInfo(info);
			if (!info.valid && !Systime::SyncFromDevice(node))
				plogwarn("[RTC] cannot seed system date");
		}
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

#elif (_MCCA & 0xFFFF) == 0x2032 && defined(_MCU_STM32H7x)

namespace {
	Spinlock stm32_rtc_lock;

	bool ReadStm32RtcDate(void* context, datime_t& date) {
		auto* rtc = static_cast<uni::RTC_t*>(context);
		if (!rtc) return false;
		SpinlockLocal guard(&stm32_rtc_lock);
		datime_t snapshot{};
		if (!rtc->getTime(snapshot, uni::RTCFormat::Bin) ||
			!rtc->getDate(snapshot, uni::RTCFormat::Bin)) return false;
		date = snapshot;
		return true;
	}

	bool WriteStm32RtcDate(void* context, const datime_t& date) {
		auto* rtc = static_cast<uni::RTC_t*>(context);
		if (!rtc) return false;
		SpinlockLocal guard(&stm32_rtc_lock);
		return rtc->setDate(date, uni::RTCFormat::Bin) &&
			rtc->setTime(date, uni::RTCFormat::Bin);
	}
}

__attribute__((section(".init.rmod")))
extern const RMOD_LIST RMOD_LIST_RTC{
	R_RTC_INIT,
	"RTC",
	{}
};

void R_RTC_INIT() {
	if (!BoardConfig::Rtc::Enabled) return;
	if (!ConfigDeviceClock(uni::QrsDeviceList::RTC) ||
		!uni::RTC.enClock(BoardConfig::Rtc::ClockSource) ||
		!uni::RTC.setMode(BoardConfig::Rtc::HourFormat,
			BoardConfig::Rtc::AsynchPrediv, BoardConfig::Rtc::SynchPrediv)) {
		plogerro("[RTC] initialization failed");
		return;
	}

	auto* node = Devsman::RegisterPlatformDevice("rtc", "stm32-rtc", &uni::RTC);
	if (node) node->fields.rcc_state = uni::RTC.isAble()
		? DeviceRccState::Enabled : DeviceRccState::Disabled;
	const bool registered = node && Devsman::AddMemoryResource(node, 0,
		uni::RTC.getBaseaddr(), (_IMM(uni::RTCReg::BKP31R) + 1) * sizeof(uint32));
	if (!registered) {
		if (node) Devsman::UnregisterDevice(node);
		plogerro("[RTC] device registration failed");
		return;
	}

	TimeDevice device{};
	device.info.readable_date = true;
	device.info.writable_date = true;
	device.info.basis = TimeBasis::UTC;
	device.info.date_resolution_ms = 1000;
	device.info.priority = 100;
	device.context = &uni::RTC;
	device.ReadDate = ReadStm32RtcDate;
	device.WriteDate = WriteStm32RtcDate;
	if (!Systime::AttachDevice(node, device)) {
		Devsman::UnregisterDevice(node);
		plogerro("[RTC] cannot attach time capability");
	}
	else if (Systime::SelectDateTimeDevice(node)) {
		SystemTimeInfo info{};
		Systime::GetSystemTimeInfo(info);
		if (!info.valid && !Systime::SyncFromDevice(node))
			plogwarn("[RTC] cannot seed system date");
	}
}

#endif
