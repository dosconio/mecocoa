// ASCII g++ TAB4 CRLF
// Attribute: Arch(AMD64)
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#include <cpp/unisym>


extern "C"
void _entry(uint64 frame_buffer_base, uint64 frame_buffer_size)
{
	byte* frame_buffer = reinterpret_cast<byte*>(frame_buffer_base);
	for0 (i, frame_buffer_size) {
		frame_buffer[i] = i & 0xFF;
	}
	loop _ASM("hlt");
}
