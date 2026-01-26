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

using namespace uni;

_ESYM_C stduint CallCo16(stduint func);

_ESYM_C
void mecocoa() {
	Letvar(p, uint16*, 0xB8000);
	*p++ = 'R' | 0x0700;
	
	if (Memory::initialize('ANIF', (byte*)(CallCo16(0))))
		*p++ = 'X' | 0x0700;

	while (1);
}

//

void outtxt(const char* str, stduint len) {
	///
}

