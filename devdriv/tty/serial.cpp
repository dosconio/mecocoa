// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: Serial / UART
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

/*
* Intel 8250
* NS16450, NS16550(A)

Send 0x3FA (FCR) 0xC7（Enable and Clear FIFO）,  Read 0x3FA MS2B (IIR)。
- 00  8250
- 10  16550
- 11  16550A

*/

#include "../../include/mecocoa.hpp"
#include "c/driver/UART.h"

_ESYM_C void R_COM1_INIT();

#if _MCCA == 0x8632
#if 1
__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_COM1{
	.init = R_COM1_INIT,
	.name = "COM1",
};
#endif

x86_COM com1;
extern OstreamTrait* con0_out;
void R_COM1_INIT() {
	outpb(PORT_COM1_DATA + 1, 0x00);	// Disable all interrupts
	outpb(PORT_COM1_DATA + 3, 0x80);	// Enable DLAB (set baud rate divisor)
	outpb(PORT_COM1_DATA + 0, 0x03);	// Set divisor to 3 (lo byte) 38400 baud
	outpb(PORT_COM1_DATA + 1, 0x00);	// Set divisor to 3 (hi byte)
	outpb(PORT_COM1_DATA + 3, 0x03);	// 8 bits, no parity, one stop bit (DLAB = 0)
	outpb(PORT_COM1_DATA + 2, 0xC7);	// Enable FIFO, clear them, with 14-byte threshold
	outpb(PORT_COM1_DATA + 4, 0x0B);	// IRQs enabled, RTS/DSR set (OUT2 bit must be 1) 

	// Enable "Received Data Available" interrupt explicitly after DLAB is cleared 
	outpb(PORT_COM1_DATA + 1, 0x01);

	con0_out = &com1;
	// Ensure IRQ_COM13_RS232_P1 maps to IRQ 4 (IDT index 0x24 if base is 0x20) [cite: 282]
	IC[IRQ_COM13_RS232_P1].setRange(mglb(Handint_COM1_Entry), SegCo32);
}

int x86_COM::inn() {
	_TODO return -1;
}

void Handint_COM1()
{
	// unchked
	uint8_t iir = innpb(PORT_COM1_DATA + 2);
	while ((iir & 0x01) == 0) {// Pending
		uint8_t int_id = (iir >> 1) & 0x07;// reason

		switch (int_id) {
		case 0x02: // Received Data Available 
		case 0x06: // Character Timeout Indication (16550+ FIFO timeout)
		{
			uint8_t data = innpb(PORT_COM1_DATA);
			com1.OutFormat("Receive %c\n\r", data);
			break;
		}
		case 0x01: // Transmitter Holding Register Empty 
			// Feed chars if send bytes using interrupt
			break;
		case 0x03: // Receiver Line Status 
			innpb(PORT_COM1_DATA + 5); // Clear Pending
			break;
		case 0x00: // Modem Status
			innpb(PORT_COM1_DATA + 6); // Clear Pending
			break;
		}
		// Check other pending
		iir = innpb(PORT_COM1_DATA + 2);
	}

	// outpb(0x20, 0x20); EOI
}

#endif
