// ASCII g++ TAB4 CRLF
// Attribute: Arch(AMD64)
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include <cpp/unisym>


extern "C"
void _entry()
{
	while (true) {
		_ASM("hlt");
		_ASM("hlt");
	}
}
