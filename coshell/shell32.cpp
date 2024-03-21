// ASCII C++ TAB4 CRLF
// Attribute: 
// LastCheck: 20240216
// AllAuthor: @dosconio
// ModuTitle: Shell Flat Prot-32
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

//{TODO} to be COTLAB. Jump to here as console.

#include "shell32.h"

int main(void) {
	// while (true) wait();
	init(1);
	outs("Hello, " _CONCOL_DarkIoWhite "\nworld!\n\r");
	while (true) {
		delay001s();
		outs("<Ring~> ");
	}
	return 0;
}

static void init(bool cls) {
	if (cls)
	{
		scrrol(25);// clear screen 80x25
		curset(0);
	}
	InterruptEnable();
}
