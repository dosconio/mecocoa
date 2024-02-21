// ASCII C TAB4 LF
// Attribute: Bits(32)
// LastCheck: 20240219
// AllAuthor: @dosconio
// ModuTitle: PIC - Intel 8259A Driver
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// BaseOn   : unisym/lib/asm/x86/interrupt/x86_i8259A.asm

#include "../../include/i8259A.h"
#include "../../include/RTC.h"
#include "../../cokasha/kernel.h"
#include <x86/x86.h>

extern void Handexc_General();
extern void Handint_General();
extern void Handint_RTC();

extern void Handexc_6_Invalid_Opcode();

dword MccaExceptTable[20] = {
	0,0,0,0,0,0,
	(dword)Handexc_6_Invalid_Opcode,
};

void i8259A_init(const struct _i8259A_ICW *inf)
{
	word port = inf->port;
	outpb(port, valword(inf->ICW1));
	outpb(port + 1, valword(inf->ICW2));
	outpb(port + 1, valword(inf->ICW3));
	if (inf->ICW1.ICW4_USED) outpb(port + 1, valword(inf->ICW4));
}

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
	//{TEMP} Omit Add `Linear`
	// The former 20 is for exceptions
	GateStructInterruptR0(&gate, (dword)Handexc_General, SegCode, 0);
	for (; i < 20; i++)
		((gate_t *)ADDR_IDT32)[i] = gate;
	((gate_t *)ADDR_IDT32)[6] = *GateStructInterruptR0(
			&gate, MccaExceptTable[6], SegCode, 0);// UD2
	// Then for interruption (256-20)
	GateStructInterruptR0(&gate, (dword)Handint_General, SegCode, 0);
	for (; i < 256; i++)
		((gate_t *)ADDR_IDT32)[i] = gate;
	// RTC
	((gate_t *)ADDR_IDT32)[PORT_RTC] = *GateStructInterruptR0(
			&gate, (dword)Handint_RTC, SegCode, 0);
	// LIDT
	*(word *)(IVTDptr + Linear) = 256 * sizeof(gate_t) - 1;
	*(dword *)(IVTDptr + Linear + 2) = ADDR_IDT32;
	InterruptDTabLoad((void *)(IVTDptr + Linear));
	// Enable 8259A
	i8259A_init(&Mas);
	i8259A_init(&Slv);
	RTC_Init();
}
