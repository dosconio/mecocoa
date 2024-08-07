// ASCII C TAB4 LF
// Attribute: Bits(32)
// LastCheck: 20240219
// AllAuthor: @dosconio
// ModuTitle: PIC - Intel 8259A Driver
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// BaseOn   : unisym/lib/asm/x86/interrupt/x86_i8259A.asm

#include "arc-x86.h"
#include "../../include/interrupt.h"
#include "../../mecocoa/kernel.h"
#include "mecocoa.h"

symbol_t Handexc_Else;
symbol_t Handint_General;
symbol_t Handint_CLK;
symbol_t Handint_RTC;
symbol_t Handint_Keyboard;

extern void Handexc_0_Divide_By_Zero();
extern void Handexc_1_Step();
extern void Handexc_2_NMI();
extern void Handexc_3_Breakpoint();
extern void Handexc_4_Overflow();
extern void Handexc_5_Bound();
extern void Handexc_6_Invalid_Opcode();
extern void Handexc_7_Coprocessor_Not_Available();
extern void Handexc_8_Double_Fault();
extern void Handexc_9_Coprocessor_Segment_Overrun();
extern void Handexc_A_Invalid_TSS();
extern void Handexc_E_Page_Fault();

dword MccaExceptTable[0x20] = {
	(dword)Handexc_0_Divide_By_Zero,
	(dword)Handexc_1_Step,
	(dword)Handexc_2_NMI,
	(dword)Handexc_3_Breakpoint,
	(dword)Handexc_4_Overflow,
	(dword)Handexc_5_Bound,
	(dword)Handexc_6_Invalid_Opcode,
	(dword)Handexc_7_Coprocessor_Not_Available,
	(dword)Handexc_8_Double_Fault,
	(dword)Handexc_9_Coprocessor_Segment_Overrun,
	(dword)Handexc_A_Invalid_TSS,
	0,// B
	0,// C
	0,// D
	(dword)Handexc_E_Page_Fault,
	// ...
};

_NOT_ABSTRACTED void InterruptInitialize()
{
	_8259A_init_t Mas = {
		.port = _i8259A_MAS,
		.ICW1.ICW4_USED = 1,
		.ICW1.ENA = 1,
		.ICW2.IntNo = 0x20,
		.ICW3.CasPortMap = 0b00000100,
		.ICW4.Not8b = 1,
	};
	_8259A_init_t Slv = {
		.port = _i8259A_SLV,
		.ICW1.ICW4_USED = 1,
		.ICW1.ENA = 1,
		.ICW2.IntNo = 0x70,
		.ICW3.CasPortIdn = 2,
		.ICW4.Not8b = 1,
	};
	gate_t gate;
	stduint i = 0;
	//{TEMP} no check for Add `Linear`
	// The former 20 is for exceptions
	GateStructInterruptR0(&gate, (dword)Handexc_Else, SegCode, 0);
	for (; i < 20; i++)// assume not from 0 for each routine
		if (MccaExceptTable[i])
			GateStructInterruptR0(MccaIDTable + i, MccaExceptTable[i], SegCode, 0);
		else
			MccaIDTable[i] = gate;
	// Then for interruption (256-20)
	GateStructInterruptR0(&gate, (dword)Handint_General, SegCode, 0);
	for (; i < 256; i++)
		MccaIDTable[i] = gate;
	// Clock (PIT)
	MccaIDTable[PORT_PIT] = *GateStructInterruptR0(
			&gate, (dword)Handint_CLK, SegCode, 0);
	// RTC
	if (0) MccaIDTable[PORT_RTC] = *GateStructInterruptR0(
			&gate, (dword)Handint_RTC, SegCode, 0);
	// Keyboard
	MccaIDTable[PORT_KBD] = *GateStructInterruptR0(
			&gate, (dword)Handint_Keyboard, SegCode, 0);
	// LIDT
	*(word *)(IVTDptr + Linear) = 256 * sizeof(gate_t) - 1;
	*(dword *)(IVTDptr + Linear + 2) = ADDR_IDT32;
	InterruptDTabLoad((void *)(IVTDptr + Linear));
	// Enable 8259A
	i8259A_init(&Mas);
	i8259A_init(&Slv);
	if (0) RTC_Init();
	PIT_Init();
}
