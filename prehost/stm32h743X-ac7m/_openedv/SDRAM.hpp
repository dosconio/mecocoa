// ASCII CPP-ISO11 TAB4 CRLF

#ifndef _OPEDEV_SDRAM_H
#define _OPEDEV_SDRAM_H

#include <cpp/MCU/ST/STM32H7>
#include <cpp/Device/FMC>

#define SDRAM_BANK1_BASE FMC_SDRAM_BANK1_BASE

void sdram_init();

#endif
