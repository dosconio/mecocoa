// ASCII C TAB4 LF
// Attribute: Bits(32)
// LastCheck: 20240220
// AllAuthor: @dosconio
// ModuTitle: RTC - PIC 0xA0 
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#ifndef _INC_RealtimeClock
#define _INC_RealtimeClock

#include <alice.h>
#include <x86/interface.x86.h>
#include "conio32.h"

#define PORT_RTC 0x70

void RTC_Init();

#endif
