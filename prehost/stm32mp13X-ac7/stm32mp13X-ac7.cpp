#include <cpp/MCU/ST/STM32MP13>
#include <c/driver/RCC/RCC-registers.hpp>
#include <c/proctrl/ARM.h>
#include <c/msgface.h>
#include "openedv-loader/metutor.h"

int main() {
	loop {
		LED.Toggle();
		SysDelay(100);
	}
}

void erro(const char*) {

}
