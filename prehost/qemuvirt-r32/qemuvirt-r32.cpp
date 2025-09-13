// ASCII GAS/RISCV64 TAB4 LF
// Attribute: 
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include <c/stdinc.h>
const char hello[] = "Hello Mecocoa~ =OwO=\n";
extern "C"
void _entry()
{
	// _TEMP say hello
	// UART INIT
	if (1) {
		(*(((volatile byte*)(0x10000000L + 1))) = (0x00));
		uint8_t lcr = (*(((volatile uint8_t*)(0x10000000L + 3))));
		(*(((volatile uint8_t*)(0x10000000L + 3))) = (lcr | (1 << 7)));
		(*(((volatile uint8_t*)(0x10000000L + 0))) = (0x03));
		(*(((volatile uint8_t*)(0x10000000L + 1))) = (0x00));
		lcr = 0;
		(*(((volatile uint8_t*)(0x10000000L + 3))) = (lcr | (3 << 0)));
	}
	// UART HELLO
	if (1) {
		for0a(i, hello) {
			while (((*(((volatile uint8_t*)(0x10000000L + 5)))) & (1 << 5)) == 0);
			(*(((volatile uint8_t*)(0x10000000L + 0))) = hello[i]);
		}
	}
	while (1);
}
