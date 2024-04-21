// ASCII C TAB4 LF
// Attribute: Bits(32)
// LastCheck: 20240219
// AllAuthor: @dosconio
// ModuTitle: PIC - Intel 8259A Driver
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// BaseOn   : unisym/lib/asm/x86/interrupt/x86_i8259A.asm

#ifndef _INC_I8259A
#define _INC_I8259A


#include "console.h"
#include "driver/i8259A.h"

#define _i8259A_MAS     0X20
#define _i8259A_SLV     0XA0
#define _i8259A_SLV_IMR 0XA1

_NOT_ABSTRACTED void InterruptInitialize();

#endif
