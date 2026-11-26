#ifndef _MCCA_H750_FANKE_UNICFG_HPP_
#define _MCCA_H750_FANKE_UNICFG_HPP_

// LIMIT
// - OTG only support Device mode 「CC1/CC2 均为 5.1 kΩ 下拉，硬件声明为 Device/受电端」

#include "../../../../include/mecocoa.hpp"
#include <cpp/Device/GPIO>
#include <cpp/Device/RCC/RCC>
#include <cpp/Device/FMC>
#include <cpp/Device/SD.hpp>
#include <c/driver/RealtimeClock.h>
#include <cpp/MCU/_DEVLIST/DEVL-STM32.h>
#include "../../../../devdriv/audio/aucoder.hpp"


#define BOARD_CONFIG_GPIO_DEVICES 1

namespace BoardConfig {
	namespace Dma {
		constexpr bool Dma1Enabled = false;
		constexpr bool Dma2Enabled = false;
	}
	namespace AudioCodec {
		constexpr bool Enabled = false;
		constexpr const char* ControlBus = nullptr;
		constexpr unsigned InitialVolumePercent = 40;
		inline constexpr const AudioCodecCandidate* Candidates = nullptr;
		constexpr stduint CandidateCount = 0;
	}
	namespace Sai {
		constexpr bool SAI1Enabled = false;
		constexpr bool SAI2Enabled = false;
		constexpr unsigned SAI1TxDmaController = 0;
		constexpr unsigned SAI1TxDmaStream = 0;
		constexpr unsigned SAI1RxDmaController = 0;
		constexpr unsigned SAI1RxDmaStream = 0;
		constexpr unsigned SAI2TxDmaController = 0;
		constexpr unsigned SAI2TxDmaStream = 0;
		constexpr unsigned SAI2RxDmaController = 0;
		constexpr unsigned SAI2RxDmaStream = 0;
		struct PinConfig {
			const char* Name;
			unsigned Port, Pin, AlternateFunction;
		};
		struct PinSet {
			const PinConfig* Pins;
			stduint Count;
		};
		constexpr PinSet SAI1Pins = { nullptr, 0 };
		constexpr PinSet SAI2Pins = { nullptr, 0 };
		constexpr uni::ClockSource SAI1ClockSource = uni::ClockSource::PLL1;
		constexpr uni::ClockSource SAI2ClockSource = uni::ClockSource::PLL1;
		constexpr unsigned OutputSampleRate = 44100;
		constexpr unsigned OutputMckDivider = 12;
		constexpr byte OutputBlock = 1;
		constexpr stduint DmaBufferBase = 0x30000000;
		constexpr stduint DmaBufferBytes = 8192;
		constexpr unsigned DmaBufferCount = 2;
		constexpr stduint DmaReservedBytes = 0;
	}

	namespace Gpio {
		struct LedConfig {
			const char* Identifier;
			uni::Color Color;
			unsigned Port, Pin;
			bool ActiveHigh;
		};
		struct KeyConfig {
			const char* Identifier;
			unsigned Port, Pin;
			bool ActiveHigh, PullUp;
		};

		struct StatusLed {
			enum : unsigned { Port = 'C', Pin = 13 };
			static constexpr uni::GPIOMode Mode = uni::GPIOMode::OUT;
			static constexpr bool ActiveHigh = false;
		};
		using ErrorLed = StatusLed;

		inline const LedConfig Leds[] = {
			{ "status", uni::Color::Green, StatusLed::Port, StatusLed::Pin, StatusLed::ActiveHigh },
		};
		constexpr stduint LedCount = numsof(Leds);
		inline constexpr const KeyConfig* Keys = nullptr;
		constexpr stduint KeyCount = 0;
	}

	namespace Xart1 {
		enum : unsigned {
			TxPort = 'A', TxPin = 9,
			RxPort = 'A', RxPin = 10,
		};
	}

	namespace Xart {
		struct Pins {
			unsigned TxPort, TxPin, RxPort, RxPin;
		};

		inline bool GetPins(unsigned id, Pins& pins) {
			switch (id) {
			case 1:
				pins = { Xart1::TxPort, Xart1::TxPin, Xart1::RxPort, Xart1::RxPin };
				return true;
			default:
				return false;
			}
		}
	}

	namespace Touch {
		constexpr bool Enabled = true;
		enum : unsigned {
			SclPort = 'H', SclPin = 4,
			SdaPort = 'H', SdaPin = 5,
			ResetPort = 'I', ResetPin = 8,
			InterruptPort = 'I', InterruptPin = 11,
			Address7 = 0x5D,
			MaxContacts = 5,
			SwapXY = 0, MirrorX = 0, MirrorY = 0,
			IicDelayLoops = 500,
			ReadyRetryTicks = 1,
			ReadyTimeoutMs = 30,
			IoRetryLimit = 3,
			IoRetryDelayMs = 20,
			MoveLogPeriodMs = 100,
		};
	}

	namespace Ltdc {
		struct PinConfig {
			unsigned Port, Pin, AlternateFunction;
		};
		namespace Pins {
			constexpr unsigned AlternateFunction = 14;
			constexpr PinConfig R0{ 'I', 15, AlternateFunction };
			constexpr PinConfig R1{ 'J', 0, AlternateFunction };
			constexpr PinConfig R2{ 'J', 1, AlternateFunction };
			constexpr PinConfig R3{ 'J', 2, AlternateFunction };
			constexpr PinConfig R4{ 'J', 3, AlternateFunction };
			constexpr PinConfig R5{ 'J', 4, AlternateFunction };
			constexpr PinConfig R6{ 'J', 5, AlternateFunction };
			constexpr PinConfig R7{ 'J', 6, AlternateFunction };
			constexpr PinConfig G0{ 'J', 7, AlternateFunction };
			constexpr PinConfig G1{ 'J', 8, AlternateFunction };
			constexpr PinConfig G2{ 'J', 9, AlternateFunction };
			constexpr PinConfig G3{ 'J', 10, AlternateFunction };
			constexpr PinConfig G4{ 'J', 11, AlternateFunction };
			constexpr PinConfig G5{ 'K', 0, AlternateFunction };
			constexpr PinConfig G6{ 'K', 1, AlternateFunction };
			constexpr PinConfig G7{ 'K', 2, AlternateFunction };
			constexpr PinConfig B0{ 'J', 12, AlternateFunction };
			constexpr PinConfig B1{ 'J', 13, AlternateFunction };
			constexpr PinConfig B2{ 'J', 14, AlternateFunction };
			constexpr PinConfig B3{ 'J', 15, AlternateFunction };
			constexpr PinConfig B4{ 'K', 3, AlternateFunction };
			constexpr PinConfig B5{ 'K', 4, AlternateFunction };
			constexpr PinConfig B6{ 'K', 5, AlternateFunction };
			constexpr PinConfig B7{ 'K', 6, AlternateFunction };
			constexpr PinConfig Clock{ 'I', 14, AlternateFunction };
			constexpr PinConfig HorizontalSync{ 'I', 12, AlternateFunction };
			constexpr PinConfig VerticalSync{ 'I', 13, AlternateFunction };
			constexpr PinConfig DataEnable{ 'K', 7, AlternateFunction };
			constexpr PinConfig All[] = {
				R0, R1, R2, R3, R4, R5, R6, R7,
				G0, G1, G2, G3, G4, G5, G6, G7,
				B0, B1, B2, B3, B4, B5, B6, B7,
				Clock, HorizontalSync, VerticalSync, DataEnable,
			};
		}
		namespace Backlight {
			enum : unsigned { Port = 'H', Pin = 6 };
			constexpr bool ActiveHigh = true;
		}
		namespace Panel {
			enum : unsigned {
				HorizontalSyncLength = 1,
				HorizontalBackPorch = 80,
				Width = 800,
				HorizontalFrontPorch = 200,
				VerticalSyncLength = 1,
				VerticalBackPorch = 40,
				Height = 480,
				VerticalFrontPorch = 22,
				BackgroundArgb = 0xFF000000u,
			};
		}
	}

	namespace Clock {
		constexpr unsigned HSEHz = 25000000;
		constexpr bool HSEBypass = false;
		constexpr unsigned LSEHz = 32768;
		constexpr bool LSEBypass = false;

		constexpr uni::SysclkSource::RCCSysclockSource PLL1InputSource = uni::SysclkSource::HSE;
		constexpr uni::RCCClockConfig_t SystemConfig = {
			.UsePLL1 = true,
			.HSEHz = HSEHz,
			.HSEBypass = HSEBypass,
			.Supply = uni::PWRSupply::LDO,
			.VoltageScale = uni::PWRVoltageScale::SCALE0,
			.FlashLatency = 4,
			.AHBPrescaler = 1,
			.APB3Prescaler = 1,
			.APB1Prescaler = 1,
			.APB2Prescaler = 1,
			.APB4Prescaler = 1,
			.PLL1InputRange = 2,
			.PLL1VcoSelect = 0,
			// PLL1P provides 480 MHz SYSCLK; PLL1Q provides 240 MHz to SDMMC1.
			.PLL1Parameters = { 5, 192, 2, 4, 2, 0 },
			.SysTickFrequency = 1000,
		};

		// HSE=25 MHz, PLL3 VCO=330 MHz, PLL3R=33 MHz for the RGB panel pixel clock.
		constexpr uni::PLLPara_t LtdcPLL3 = { 25, 330, 2, 2, 10, 0 };
		constexpr uni::PLLSource::PLLSource LtdcPLL3Source = uni::PLLSource::HSE;
		constexpr unsigned LtdcPLL3InputRange = 0;
		constexpr unsigned LtdcPLL3VcoSelect = 0;
	}

	namespace Rtc {
		constexpr bool Enabled = true;
		constexpr uni::RTCClockSource ClockSource = uni::RTCClockSource::LSE;
		constexpr uni::RTCHourFormat HourFormat = uni::RTCHourFormat::Hour24;
		constexpr stduint AsynchPrediv = 127;
		constexpr stduint SynchPrediv = 255;
	}

	namespace Sdmmc1 {
		constexpr uni::SDMMC1_CLKSRC ClockSource = uni::SDMMC1_CLKSRC::PLL1Q;
		constexpr bool ClockOnRisingEdge = true;
		struct PinConfig {
			const char* Signal;
			unsigned Port, Pin, AlternateFunction;
		};
		constexpr PinConfig Pins[] = {
			{ "D0", 'C', 8, 12 },
			{ "D1", 'C', 9, 12 },
			{ "D2", 'C', 10, 12 },
			{ "D3", 'C', 11, 12 },
			{ "CLK", 'C', 12, 12 },
			{ "CMD", 'D', 2, 12 },
		};
	}

	namespace Sdram {
		constexpr stduint Base = FMC_SDRAM_BANK1_BASE;
		constexpr stduint Size = 32u * 1024u * 1024u;
		constexpr stduint ReservedSize = 1u * 1024u * 1024u;
		struct PinConfig {
			unsigned Port, Pin, AlternateFunction;
		};
		constexpr unsigned AlternateFunction = 12;

		constexpr PinConfig D0{ 'D', 14, AlternateFunction };
		constexpr PinConfig D1{ 'D', 15, AlternateFunction };
		constexpr PinConfig D2{ 'D', 0, AlternateFunction };
		constexpr PinConfig D3{ 'D', 1, AlternateFunction };
		constexpr PinConfig D4{ 'E', 7, AlternateFunction };
		constexpr PinConfig D5{ 'E', 8, AlternateFunction };
		constexpr PinConfig D6{ 'E', 9, AlternateFunction };
		constexpr PinConfig D7{ 'E', 10, AlternateFunction };
		constexpr PinConfig D8{ 'E', 11, AlternateFunction };
		constexpr PinConfig D9{ 'E', 12, AlternateFunction };
		constexpr PinConfig D10{ 'E', 13, AlternateFunction };
		constexpr PinConfig D11{ 'E', 14, AlternateFunction };
		constexpr PinConfig D12{ 'E', 15, AlternateFunction };
		constexpr PinConfig D13{ 'D', 8, AlternateFunction };
		constexpr PinConfig D14{ 'D', 9, AlternateFunction };
		constexpr PinConfig D15{ 'D', 10, AlternateFunction };
		constexpr PinConfig D16{ 'H', 8, AlternateFunction };
		constexpr PinConfig D17{ 'H', 9, AlternateFunction };
		constexpr PinConfig D18{ 'H', 10, AlternateFunction };
		constexpr PinConfig D19{ 'H', 11, AlternateFunction };
		constexpr PinConfig D20{ 'H', 12, AlternateFunction };
		constexpr PinConfig D21{ 'H', 13, AlternateFunction };
		constexpr PinConfig D22{ 'H', 14, AlternateFunction };
		constexpr PinConfig D23{ 'H', 15, AlternateFunction };
		constexpr PinConfig D24{ 'I', 0, AlternateFunction };
		constexpr PinConfig D25{ 'I', 1, AlternateFunction };
		constexpr PinConfig D26{ 'I', 2, AlternateFunction };
		constexpr PinConfig D27{ 'I', 3, AlternateFunction };
		constexpr PinConfig D28{ 'I', 6, AlternateFunction };
		constexpr PinConfig D29{ 'I', 7, AlternateFunction };
		constexpr PinConfig D30{ 'I', 9, AlternateFunction };
		constexpr PinConfig D31{ 'I', 10, AlternateFunction };

		constexpr PinConfig A0{ 'F', 0, AlternateFunction };
		constexpr PinConfig A1{ 'F', 1, AlternateFunction };
		constexpr PinConfig A2{ 'F', 2, AlternateFunction };
		constexpr PinConfig A3{ 'F', 3, AlternateFunction };
		constexpr PinConfig A4{ 'F', 4, AlternateFunction };
		constexpr PinConfig A5{ 'F', 5, AlternateFunction };
		constexpr PinConfig A6{ 'F', 12, AlternateFunction };
		constexpr PinConfig A7{ 'F', 13, AlternateFunction };
		constexpr PinConfig A8{ 'F', 14, AlternateFunction };
		constexpr PinConfig A9{ 'F', 15, AlternateFunction };
		constexpr PinConfig A10{ 'G', 0, AlternateFunction };
		constexpr PinConfig A11{ 'G', 1, AlternateFunction };

		constexpr PinConfig BankAddress0{ 'G', 4, AlternateFunction };
		constexpr PinConfig BankAddress1{ 'G', 5, AlternateFunction };
		constexpr PinConfig ByteLane0{ 'E', 0, AlternateFunction };
		constexpr PinConfig ByteLane1{ 'E', 1, AlternateFunction };
		constexpr PinConfig ByteLane2{ 'I', 4, AlternateFunction };
		constexpr PinConfig ByteLane3{ 'I', 5, AlternateFunction };
		constexpr PinConfig Clock{ 'G', 8, AlternateFunction };
		constexpr PinConfig ClockEnable{ 'H', 2, AlternateFunction };
		constexpr PinConfig ChipEnable{ 'H', 3, AlternateFunction };
		constexpr PinConfig RowAddressStrobe{ 'F', 11, AlternateFunction };
		constexpr PinConfig ColumnAddressStrobe{ 'G', 15, AlternateFunction };
		constexpr PinConfig WriteEnable{ 'C', 0, AlternateFunction };

		constexpr PinConfig All[] = {
			D0, D1, D2, D3, D4, D5, D6, D7,
			D8, D9, D10, D11, D12, D13, D14, D15,
			D16, D17, D18, D19, D20, D21, D22, D23,
			D24, D25, D26, D27, D28, D29, D30, D31,
			A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11,
			BankAddress0, BankAddress1,
			ByteLane0, ByteLane1, ByteLane2, ByteLane3,
			Clock, ClockEnable, ChipEnable,
			RowAddressStrobe, ColumnAddressStrobe, WriteEnable,
		};

		constexpr uni::SDRAMInit ControllerConfig = {
			.bank = uni::SDRAMBank::Bank1,
			.column = uni::SDRAMColumn::C9,
			.row = uni::SDRAMRow::R12,
			.dataWidth = uni::SDRAMDataWidth::W32,
			.bankNum = uni::SDRAMBankNum::Four,
			.cas = uni::SDRAMCas::CL3,
			.writeProtection = false,
			.clockPeriod = uni::SDRAMClock::C2,
			.readBurst = true,
			.readPipe = uni::SDRAMReadPipe::NoDelay,
		};
		constexpr uni::SDRAMTiming Timing = {
			.loadToActive = 2,
			.exitSelfRefresh = 6,
			.selfRefreshTime = 4,
			.rowCycle = 7,
			.writeRecovery = 3,
			.rpDelay = 2,
			.rcdDelay = 2,
		};
		constexpr uni::SDRAMTarget CommandTarget = uni::SDRAMTarget::Bank1;
		enum : unsigned {
			PowerUpDelayUs = 1000,
			AutoRefreshCount = 8,
			ModeRegister = 0x0232,
			RefreshRate = 1543,
		};
	}
}

inline static bool ConfigSaiPins(const BoardConfig::Sai::PinSet& pin_set) {
	if (!pin_set.Pins || !pin_set.Count) return false;
	for (stduint i = 0; i < pin_set.Count; ++i) {
		const auto& pin = pin_set.Pins[i];
		uni::GPIO[pin.Port].enClock(true);
		const auto& gpio = uni::GPIO[pin.Port][pin.Pin];
		gpio.setMode(uni::GPIOMode::OUT_AF_PushPull, uni::GPIOSpeed::Veryhigh);
		gpio.setPull(true);
		if (!gpio._set_alternate((byte)pin.AlternateFunction)) return false;
	}
	return true;
}

inline static bool ConfigDeviceGPIN(uni::QrsDeviceList device) {
	using namespace uni;
	switch (device) {
	case QrsDeviceList::LTDC:
		for (const auto& pin : BoardConfig::Ltdc::Pins::All) {
			const auto& gpio = uni::GPIO[pin.Port][pin.Pin];
			gpio.setMode(uni::GPIOMode::OUT_AF_PushPull, uni::GPIOSpeed::Veryhigh);
			if (!gpio._set_alternate((byte)pin.AlternateFunction)) return false;
		}
		{
			const auto& backlight = uni::GPIO[BoardConfig::Ltdc::Backlight::Port][BoardConfig::Ltdc::Backlight::Pin];
			backlight.setMode(uni::GPIOMode::OUT_PushPull, uni::GPIOSpeed::Veryhigh);
			backlight = BoardConfig::Ltdc::Backlight::ActiveHigh;
		}
		return true;
	case QrsDeviceList::SAI1:
		return ConfigSaiPins(BoardConfig::Sai::SAI1Pins);
	case QrsDeviceList::SAI2:
		return ConfigSaiPins(BoardConfig::Sai::SAI2Pins);
	case QrsDeviceList::FmcSDRAM:
		for (const auto& pin : BoardConfig::Sdram::All) {
			const auto& gpio = uni::GPIO[pin.Port][pin.Pin];
			gpio.setMode(uni::GPIOMode::OUT_AF_PushPull, uni::GPIOSpeed::Veryhigh);
			if (!gpio._set_alternate((byte)pin.AlternateFunction)) return false;
		}
		return true;
	case QrsDeviceList::Sdmmc1:
		for (const auto& pin : BoardConfig::Sdmmc1::Pins) {
			const auto& gpio = uni::GPIO[pin.Port][pin.Pin];
			gpio.setMode(uni::GPIOMode::OUT_AF_PushPull, uni::GPIOSpeed::Veryhigh);
			if (!gpio._set_alternate((byte)pin.AlternateFunction)) return false;
		}
		return true;
	default:
		return false;
	}
}

inline static bool ConfigDeviceClock(uni::QrsDeviceList device) {
	using namespace uni;
	switch (device) {
	case QrsDeviceList::LTDC:
		return uni::RCC.PLL3.setMode(
			BoardConfig::Clock::LtdcPLL3Source,
			BoardConfig::Clock::LtdcPLL3InputRange,
			BoardConfig::Clock::LtdcPLL3VcoSelect,
			BoardConfig::Clock::LtdcPLL3);
	case QrsDeviceList::SAI1:
		return RCC.setPeriphClock(PeriphClock::SAI1,
			BoardConfig::Sai::SAI1ClockSource);
	case QrsDeviceList::SAI2:
		return RCC.setPeriphClock(PeriphClock::SAI23,
			BoardConfig::Sai::SAI2ClockSource);
	case QrsDeviceList::RTC:
		if (BoardConfig::Rtc::ClockSource != RTCClockSource::LSE) return true;
		LSE_VALUE = BoardConfig::Clock::LSEHz;
		return RCC.LSE.setMode(true, BoardConfig::Clock::LSEBypass);
	default:
		return false;
	}
}

#endif
