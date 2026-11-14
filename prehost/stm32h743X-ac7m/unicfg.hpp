#ifndef _MCCA_H7_UNICFG_HPP_
#define _MCCA_H7_UNICFG_HPP_

#include "../../include/mecocoa.hpp"
#include <cpp/Device/GPIO>
#include <cpp/Device/RCC/RCC>
#include <cpp/Device/FMC>

// An example of STM32H743 openedv board configuration.
#if 1
namespace BoardConfig {
	namespace Gpio {
		namespace LedBlue {
			enum : unsigned { Port = 'B', Pin = 0 };
			constexpr uni::GPIOMode Mode = uni::GPIOMode::OUT;
		}
		namespace LedRed {
			enum : unsigned { Port = 'B', Pin = 1 };
			constexpr uni::GPIOMode Mode = uni::GPIOMode::OUT;
		}
		namespace KeyUp {
			enum : unsigned { Port = 'A', Pin = 0 };
			constexpr uni::GPIOMode Mode = uni::GPIOMode::IN_Pull;
			constexpr bool PullUp = false;
			constexpr uni::GPIORupt::RuptEdge Edge = uni::GPIORupt::Posedge;
		}
		namespace KeyDown {
			enum : unsigned { Port = 'H', Pin = 2 };
			constexpr uni::GPIOMode Mode = uni::GPIOMode::IN_Pull;
			constexpr bool PullUp = true;
			constexpr uni::GPIORupt::RuptEdge Edge = uni::GPIORupt::Negedge;
		}
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
			default: return false;
			}
		}
	}
	namespace Ltdc {
		struct PinConfig {
			unsigned Port, Pin, AlternateFunction;
		};
		namespace Pins {
			constexpr PinConfig R0{};
			constexpr PinConfig R1{};
			constexpr PinConfig R2{};
			constexpr PinConfig R3{ 'H', 9, 14 };
			constexpr PinConfig R4{ 'H', 10, 14 };
			constexpr PinConfig R5{ 'H', 11, 14 };
			constexpr PinConfig R6{ 'H', 12, 14 };
			constexpr PinConfig R7{ 'G', 6, 14 };
			constexpr PinConfig G0{};
			constexpr PinConfig G1{};
			constexpr PinConfig G2{ 'H', 13, 14 };
			constexpr PinConfig G3{ 'H', 14, 14 };
			constexpr PinConfig G4{ 'H', 15, 14 };
			constexpr PinConfig G5{ 'I', 0, 14 };
			constexpr PinConfig G6{ 'I', 1, 14 };
			constexpr PinConfig G7{ 'I', 2, 14 };
			constexpr PinConfig B0{};
			constexpr PinConfig B1{};
			constexpr PinConfig B2{};
			constexpr PinConfig B3{ 'G', 11, 14 };
			constexpr PinConfig B4{ 'I', 4, 14 };
			constexpr PinConfig B5{ 'I', 5, 14 };
			constexpr PinConfig B6{ 'I', 6, 14 };
			constexpr PinConfig B7{ 'I', 7, 14 };
			constexpr PinConfig CLK{ 'G', 7, 14 };
			constexpr PinConfig HSYNC{ 'I', 10, 14 };
			constexpr PinConfig VSYNC{ 'I', 9, 14 };
			constexpr PinConfig DE{ 'F', 10, 14 };
			constexpr PinConfig All[] = {
				R3, R4, R5, R6, R7,
				G2, G3, G4, G5, G6, G7,
				B3, B4, B5, B6, B7,
				CLK, HSYNC, VSYNC, DE,
			};
		}
		namespace Backlight {
			enum : unsigned { Port = 'B', Pin = 5 };
			constexpr bool ActiveHigh = true;
		}
		namespace Panel {
			// Shared by both LTDC hardware layers.
			// Values only: no global objects or initialization order dependencies.
			enum : unsigned {
				HorizontalSyncLength = 48,
				HorizontalBackPorch = 88,
				Width = 800,
				HorizontalFrontPorch = 40,
				VerticalSyncLength = 3,
				VerticalBackPorch = 32,
				Height = 480,
				VerticalFrontPorch = 13,
				BackgroundArgb = 0xFF000000u,
			};
		}
	}
	namespace Usb {
		namespace Otg2 {
			constexpr UsbRole DefaultRole = UsbRole::Host;
			enum : unsigned {
				DmPort = 'A', DmPin = 11,
				DpPort = 'A', DpPin = 12,
				AlternateFunction = 10,
				SdaPort = 'H', SdaPin = 5,
				SclPort = 'H', SclPin = 4,
				PowerAddress7 = 0x20, PowerBit = 3,
				PowerActiveHigh = 1,
				IicDelayLoops = 500,
				PowerStableMs = 500,
				DisconnectMs = 100,
				ClockTimeoutMs = 100,
				VbusSensing = 0,
				HostChannels = 8,
				MscWriteEnabled = 0,
				HostDebounceMs = 100,
				HostResetHoldMs = 100,
				HostEnableTimeoutMs = 100,
				HostRecoveryMs = 20,
				HostEnumerationTimeoutMs = 3000,
				HostRetryDelayMs = 100,
				HostRetryLimit = 2,
				// Prototype identity inherited from the board's USB examples.
				DeviceVendorId = 0x0483,
				DeviceProductId = 0x5740,
			};
		}
	}
	namespace Touch {
		// GT9xx on the RGB panel; GPIO port names are ASCII letters.
		enum : unsigned {
			SclPort = 'H', SclPin = 6,
			SdaPort = 'I', SdaPin = 3,
			ResetPort = 'I', ResetPin = 8,
			InterruptPort = 'H', InterruptPin = 7,
			Address7 = 0x14,
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

	namespace Sdmmc1 {
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
		// Keep the first MiB outside the allocator for the framebuffer and low-address tests.
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
		constexpr PinConfig A12{ 'G', 2, AlternateFunction };

		constexpr PinConfig BankAddress0{ 'G', 4, AlternateFunction };
		constexpr PinConfig BankAddress1{ 'G', 5, AlternateFunction };
		constexpr PinConfig ByteLane0{ 'E', 0, AlternateFunction };
		constexpr PinConfig ByteLane1{ 'E', 1, AlternateFunction };
		constexpr PinConfig Clock{ 'G', 8, AlternateFunction };
		constexpr PinConfig ClockEnable{ 'C', 3, AlternateFunction };
		constexpr PinConfig ChipEnable{ 'C', 2, AlternateFunction };
		constexpr PinConfig RowAddressStrobe{ 'F', 11, AlternateFunction };
		constexpr PinConfig ColumnAddressStrobe{ 'G', 15, AlternateFunction };
		constexpr PinConfig WriteEnable{ 'C', 0, AlternateFunction };

		constexpr PinConfig All[] = {
			D0, D1, D2, D3, D4, D5, D6, D7,
			D8, D9, D10, D11, D12, D13, D14, D15,
			A0, A1, A2, A3, A4, A5, A6,
			A7, A8, A9, A10, A11, A12,
			BankAddress0, BankAddress1, ByteLane0, ByteLane1,
			Clock, ClockEnable, ChipEnable,
			RowAddressStrobe, ColumnAddressStrobe, WriteEnable,
		};

		constexpr uni::SDRAMInit ControllerConfig = {
			.bank = uni::SDRAMBank::Bank1,
			.column = uni::SDRAMColumn::C9,
			.row = uni::SDRAMRow::R13,
			.dataWidth = uni::SDRAMDataWidth::W16,
			.bankNum = uni::SDRAMBankNum::Four,
			.cas = uni::SDRAMCas::CL2,
			.writeProtection = false,
			.clockPeriod = uni::SDRAMClock::C2,
			.readBurst = true,
			.readPipe = uni::SDRAMReadPipe::NoDelay,
		};
		constexpr uni::SDRAMTiming Timing = {
			.loadToActive = 2,
			.exitSelfRefresh = 8,
			.selfRefreshTime = 6,
			.rowCycle = 7,// TRC 6→7：60ns→70ns ≥ W9825G6KH tRC min 65ns
			.writeRecovery = 2,
			.rpDelay = 2,
			.rcdDelay = 2,
		};
		constexpr uni::SDRAMTarget CommandTarget = uni::SDRAMTarget::Bank1;
		enum : unsigned {
			PowerUpDelayUs = 500,// 时钟配置使能后至少延时 200us
			AutoRefreshCount = 8,// 自动刷新 8 次
			ModeRegister = 0x0220,// 突发长度 1、顺序突发、CAS 2、标准模式、单次写突发
			RefreshRate = 761,// COUNT = 64*1000*100/8192 - 20 = 761
		};
	}
	namespace Clock {
		constexpr unsigned HSEHz = 25000000;
		constexpr bool HSEBypass = false;

		constexpr uni::SysclkSource::RCCSysclockSource PLL1InputSource = uni::SysclkSource::HSE;
		constexpr uni::RCCClockConfig_t SystemConfig = {
			.UsePLL1 = true,
			.HSEHz = HSEHz,
			.HSEBypass = HSEBypass,
			.VoltageScale = 1,
			.FlashLatency = 2,
			.AHBPrescaler = 1,
			.APB3Prescaler = 1,
			.APB1Prescaler = 1,
			.APB2Prescaler = 1,
			.APB4Prescaler = 2,
			.PLL1InputRange = 2,
			.PLL1VcoSelect = 0,
			.PLL1Parameters = { 5, 160, 2, 4, 4, 0 },
			.SysTickFrequency = 1000,
		};

		// ---- LTDC 像素时钟：PLL3.R = 33MHz ----
		// HSE=25MHz：VCO = 25/5*160 = 800MHz，R = 800/24 ≈ 33.3MHz
		constexpr uni::PLLPara_t LTDC_PLL3 = { 5, 160, 2, 2, 24, 0 };
		constexpr uni::PLLSource::PLLSource LTDC_PLL3Source = uni::PLLSource::HSE;
		constexpr unsigned LTDC_PLL3InputRange = 2;
		constexpr unsigned LTDC_PLL3VcoSelect = 0;
	}
}

/* Easy-config Devices
* GPIO
* XART
*/

enum class DeviceType {
	LTDC,
	FmcSDRAM,
	Sdmmc1,
	DeviceCount
};

inline static bool ConfigDeviceGPIN(DeviceType dev) {
	switch (dev) {
	case DeviceType::LTDC:
		for (unsigned i = 0; i < sizeof(BoardConfig::Ltdc::Pins::All) / sizeof(BoardConfig::Ltdc::Pins::All[0]); ++i) {
			const auto& pin = BoardConfig::Ltdc::Pins::All[i];
			const auto& gpio = uni::GPIO[pin.Port][pin.Pin];
			gpio.setMode(uni::GPIOMode::OUT_AF_PushPull, uni::GPIOSpeed::Veryhigh);
			if (!gpio._set_alternate((byte)pin.AlternateFunction)) return false;
		}
		{
			const auto& backlight = uni::GPIO[BoardConfig::Ltdc::Backlight::Port][BoardConfig::Ltdc::Backlight::Pin];
			backlight.setMode(uni::GPIOMode::OUT_PushPull, uni::GPIOSpeed::Veryhigh);
			backlight.setPull(true);
			backlight = BoardConfig::Ltdc::Backlight::ActiveHigh;
		}
		return true;
	case DeviceType::Sdmmc1:
		for (const auto& pin : BoardConfig::Sdmmc1::Pins) {
			const auto& gpio = uni::GPIO[pin.Port][pin.Pin];
			gpio.setMode(uni::GPIOMode::OUT_AF_PushPull, uni::GPIOSpeed::High);
			if (!gpio._set_alternate((byte)pin.AlternateFunction)) return false;
		}
		return true;
	case DeviceType::FmcSDRAM:
		for (unsigned i = 0; i < sizeof(BoardConfig::Sdram::All) / sizeof(BoardConfig::Sdram::All[0]); ++i) {
			const auto& pin = BoardConfig::Sdram::All[i];
			const auto& gpio = uni::GPIO[pin.Port][pin.Pin];
			gpio.setMode(uni::GPIOMode::OUT_AF_PushPull, uni::GPIOSpeed::Veryhigh);
			gpio.setPull(true);
			if (!gpio._set_alternate((byte)pin.AlternateFunction)) return false;
		}
		return true;
	default:
		return false;
	}
}
inline static bool ConfigDeviceClock(DeviceType dev) {
	switch (dev) {
	case DeviceType::LTDC:
		if (!uni::RCC.PLL3.setMode(
			BoardConfig::Clock::LTDC_PLL3Source,
			BoardConfig::Clock::LTDC_PLL3InputRange,
			BoardConfig::Clock::LTDC_PLL3VcoSelect,
			BoardConfig::Clock::LTDC_PLL3))
			return false;
		break;
	default:
		return false;
	}
	return true;
}

#endif
#endif
