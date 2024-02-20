// ASCII C TAB4 LF
// Attribute: Bits(32)
// LastCheck: 20240219
// AllAuthor: @dosconio
// ModuTitle: PIC - Intel 8259A Driver
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// BaseOn   : unisym/lib/asm/x86/interrupt/x86_i8259A.asm

#ifndef _INC_I8259A
#define _INC_I8259A

#include <alice.h>
#include <x86/interface.x86.h>
#include "conio32.h"

#define _i8259A_MAS     0X20
#define _i8259A_SLV     0XA0
#define _i8259A_SLV_IMR 0XA1

typedef struct _i8259A_ICW
{
	word port;
	struct
	{
		byte ICW4_USED : 1;
		byte CAS : 1;// for MAS and SLV
		byte ADI : 1;
		byte NotEdge : 1;
		byte ENA : 1; // [1]
		byte : 3; // [X]*3
	} ICW1;
	struct
	{
		byte IntNo;
	} ICW2;
	union
	{
		byte CasPortMap;// 8 bits Bit
		byte CasPortIdn;// 3 bits Hex
	} ICW3;
	struct
	{
		byte Not8b : 1;
		byte AutoEOI : 1;
		byte IsMas : 1;
		byte Buf : 1;
		byte SFNM : 1;
		byte : 3; // [X]*3
	} ICW4;
} _8259A_init_t;

void i8259A_init(const struct _i8259A_ICW* inf);

_NOT_ABSTRACTED void InterruptInitialize();



#endif
