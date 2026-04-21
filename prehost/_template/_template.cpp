// ASCII GAS/ARM64 TAB4 LF
// Attribute: 
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include <c/stdinc.h>

void uart_putc(char c);
int main() {
	const char* p = "Ciallo\n";
	for (; *p; p++)uart_putc(*p);
	while (1);
}

void uart_putc(char c) {
	// PL011 UART
	volatile unsigned int *uart = (unsigned int *)0x09000000;
	*uart = c;
}
