// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _DEBUG
#include <c/consio.h>
#include <c/driver/keyboard.h>
#include "../include/atx-x86-flap32.hpp"

use crate uni;
#ifdef _ARC_x86 // x86:

extern "C" void new_lgdt();

static const uint32 gdt_magic[] = {
	0x00000000, 0x00000000, // null
	0x0000FFFF, 0x00CF9300, // data
	0x0000FFFF, 0x00CF9B00, // code
	0x00000000, 0x00000000, // call
	0x0000FFFF, 0x000F9A00, // code-16
	0x00000000, 0x00008900, // tss
	0x0000FFFF, 0x00CFFA00, // code r3
	0x0000FFFF, 0x00CFF200, // data r3
};

//{TODO} make into a class
// 8*0 Null   , 8*1 Code R0, 8*2 Data R0, 8*3 Gate R3
// 8*4 Co16   , 8*5 TSS    , 8*6 Codu R3, 8*7 Datu R3 (-u User)
// Other TSS ... pass 8*(6,7)
// previous GDT may be broken, omit __asm("sgdt _buf");
void GDT_Init() {
	MemCopyN(mecocoa_global->gdt_ptr, gdt_magic, sizeof(gdt_magic));
	mecocoa_global->gdt_ptr->rout.setModeCall(_IMM(call_gate_entry()) | 0x80000000, SegCode);// 8*3 Call Gate
	tmp48_le = { mecocoa_global->gdt_len = sizeof(mec_gdt) - 1 , _IMM(mecocoa_global->gdt_ptr) };// physical address
	__asm("lgdt tmp48_le");
	tmp48_be = { _IMM(&new_lgdt) , SegCode };
	__asm("ljmp *tmp48_be");
	__asm("new_lgdt: mov $8, %eax;");
	__asm("mov %eax, %ds; mov %eax, %es; mov %eax, %fs; mov %eax, %gs; mov %eax, %ss;");
}

word GDT_GetNumber() {
	return (mecocoa_global->gdt_len + 1) / 8;
}

// allocate and apply, return the allocated number
word GDT_Alloc() {
	word ret = mecocoa_global->gdt_len + 1;
	tmp48_le = { mecocoa_global->gdt_len += 8 , _IMM(mecocoa_global->gdt_ptr) };
	__asm("lgdt tmp48_le");
	return ret;
}

#endif
