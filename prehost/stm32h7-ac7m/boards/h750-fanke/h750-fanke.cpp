#include "../../../../include/mecocoa.hpp"
#include <cpp/MCU/ST/STM32H7>
#include <cpp/Device/USB/USBHost-MSC.hpp>
#include "unicfg.hpp"

_ESYM_C {
	char _IDN_BOARD[16]{ "STM32H750XBH6" };
}

#if !_GUI_ENABLE
OstreamTrait* con0_out;
#endif

void BoardInitialize() {
	auto& led = uni::GPIO[BoardConfig::Gpio::StatusLed::Port][BoardConfig::Gpio::StatusLed::Pin];
	led.setMode(BoardConfig::Gpio::StatusLed::Mode);
	led = !BoardConfig::Gpio::StatusLed::ActiveHigh;
}

namespace Devs {
	void handle_usb_msc_event(uni::device::SpaceUSB::USBHost_MSC&,
		uni::device::SpaceUSB::USBHost_MSC::Event, uint32, DeviceNode*) {}

	bool disconnect_usb_msc_storage(uni::device::SpaceUSB::USBHostDevice&) {
		return false;
	}
}

bool Devsman::InitializeOTG(stduint) {
	return true;
}

bool Devsman::ProcessOTGEvent(const DeviceEvent&) {
	return false;
}

void Devsman::PumpOTG() {}

void Devsman::ProcessOTGRoleRequest(const UsbRoleRequest&) {}

void Devsman::ProcessUSBMSCRequest(const UsbMSCRequest&, stduint) {}

void Devsman::PumpUSBMSCRequests() {}

void Devsman::ProcessUSBMSCProbe(void*) {}

void Devsman::RegisterUSBMSCStorage(void*) {}

void Devsman::MountUSBMSCStorage(void*) {}

void Devsman::UnmountUSBMSCStorage(void*) {}

void Devsman::RemoveUSBMSCStorage(void*) {}
