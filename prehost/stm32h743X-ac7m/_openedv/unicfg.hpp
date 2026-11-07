#ifndef _MCCA_H7_UNICFG_HPP_
#define _MCCA_H7_UNICFG_HPP_

namespace BoardConfig {
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
