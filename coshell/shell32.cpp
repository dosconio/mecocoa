// ASCII C++ TAB4 CRLF
// Attribute: 
// LastCheck: 20240216
// AllAuthor: @dosconio
// ModuTitle: Shell Flat Prot-32
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

//{TODO} to be COTLAB. Jump to here as console.

#define wait() HALT()

extern "C" {
	#include <../c/x86/interface.x86.h>
	#include "../include/conio32.h"
}

static void init(int cls);

int main(void) {
	//while (true) wait();
	init(0);
	outs("Hello, " _CONCOL_DarkIoWhite "\nworld!\n");
	outc('\r');
	while (true) wait();
	return 0;
}

static void init(int cls) {
	if (cls)
	{
		scrrol(25);// clear screen 80x25
		curset(0);
	}
	InterruptEnable();
}
