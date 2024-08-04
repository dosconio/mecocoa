// ASCII NASM0207 TAB4 LF
// Attribute: CPU(x86) File(HerELF)
// LastCheck: 20240219
// AllAuthor: @dosconio
// ModuTitle: Kernel Flap-32
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// NegaiMap : { USYM --> Mecocoa --> USYM --> Any OS }

//{TODO} make kernel.asm (except 16-bit) here

#include "arc-x86.h"
#include "mecocoa.h"

void hand_cycle_1s() {
	static byte i = ~0;
	const word limit = TasksAvailableSelectors[0];
	if (!(ReadyFlag1 & ReadyFlag1_MASK_SwitchTask)) return;
	Ranginc(i, limit);
	jmpFar(0, TasksAvailableSelectors[1 + i] << 3);
}

