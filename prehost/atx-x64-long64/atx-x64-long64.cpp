// ASCII g++ TAB4 CRLF
// Attribute: Arch(AMD64)
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// led by bootx64 and atx-x86-flap32.loader; we do not mix long64 and uefi64
#define _STYLE_RUST
#include <cpp/unisym>

using namespace uni;

_ESYM_C void
_entry() {
	Letvar(p, uint8*, 0xB8000);
	*p = 'R';
	while (1);
}
