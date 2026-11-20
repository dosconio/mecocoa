#include "../../../../include/mecocoa.hpp"
#include <c/prochip/CortexM7.h>
#include <cpp/MCU/ST/STM32H7>
#include "unicfg.hpp"

extern "C" char _IDN_BOARD[16]{ "STM32H743IIT6" };

void erro(const char* str);
void sysinfo_classic(OstreamTrait& output, byte function);
void dump_processors(OstreamTrait& output);

namespace {
	void KeyInterruptUp() {
		sysinfo_classic(XART1, 'w');
		dump_processors(XART1);// sysinfo_classic(XART1, 's');
		sysinfo_classic(XART1, 'f');
		sysinfo_classic(XART1, 'H');
	}

	void KeyInterruptDown() {
		erro(nullptr);
	}
}

void BoardInitialize() {
	GPIN& key_up = GPIO[BoardConfig::Gpio::KeyUp::Port][BoardConfig::Gpio::KeyUp::Pin];
	GPIN& key_down = GPIO[BoardConfig::Gpio::KeyDown::Port][BoardConfig::Gpio::KeyDown::Pin];
	key_up.setMode(BoardConfig::Gpio::KeyUp::Mode).setPull(BoardConfig::Gpio::KeyUp::PullUp);
	key_up.setMode(BoardConfig::Gpio::KeyUp::Edge);
	key_up.setInterrupt(KeyInterruptUp);
	key_up.enInterrupt();
	key_down.setMode(BoardConfig::Gpio::KeyDown::Mode).setPull(BoardConfig::Gpio::KeyDown::PullUp);
	key_down.setMode(BoardConfig::Gpio::KeyDown::Edge);
	key_down.setInterrupt(KeyInterruptDown);
	key_down.enInterrupt();
}
