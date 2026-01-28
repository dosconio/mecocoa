// ASCII g++ TAB4 CRLF
// Attribute: Arch(AMD64)
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// led by bootx64 and atx-x86-flap32.loader; we do not mix long64 and uefi64
#define _STYLE_RUST
#include "../../include/atx-x64.hpp"
#include <cpp/unisym>
#include <c/bitmap.h>
#include <c/consio.h>

void cons_init();

_ESYM_C
void mecocoa() {
	if (!Memory::initialize('ANIF', NULL))
		HALT();
	
	cons_init();
	Console.OutFormat("Ciallo~\r\n");

	Memory::pagebmap->dump_avail_memory();

	loop HALT();
}

//{FUTURE}
// - treat<uint16>(0x502ull) = 0x180; CallCo16(1);
