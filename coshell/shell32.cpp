// ASCII C++ TAB4 CRLF
// Attribute: 
// LastCheck: 20240216
// AllAuthor: @dosconio
// ModuTitle: Shell Flat Prot-32

//{TODO} to be COTLAB.

#define wait() HALT()

extern "C" {
	#include <../c/x86/interface.x86.h>
	#include "../include/conio32.h"
}

static void init() {
	InterruptEnable();
	scrrol(25);// clear screen 80x25
	curset(0);
}

int main(void) {
	init();
	outs("Hello, " _CONCOL_DarkIoWhite "\nworld!\n");
	outc('\r');
	while (true) wait();
	return 0;
}
