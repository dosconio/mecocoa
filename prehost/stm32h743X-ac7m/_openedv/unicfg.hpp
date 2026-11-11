#ifndef _MCCA_H7_UNICFG_HPP_
#define _MCCA_H7_UNICFG_HPP_

#include <cpp/Device/GPIO>

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

}

/* Easy-config Devices
* GPIO
* XART
*/

enum class DeviceType {
	LTDC,
	DeviceCount
};

inline static bool ConfigDeviceGPIN(DeviceType dev) {
	(void)dev;
	return false;
}

#endif
