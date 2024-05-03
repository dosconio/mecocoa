// ASCII C++ TAB4 CRLF
// Attribute: 
// LastCheck: 20240216
// AllAuthor: @dosconio
// ModuTitle: Shell Flat Prot-32
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

//{TODO} to be COTLAB. Jump to here as console.

#include "shell32.h"
#include "mecocoa.h"

// debug
word tasks[3];

int main(void) {
	init(true);
	outs("Hello, " _CONCOL_DarkIoWhite "\b\nworld!\n\r");
	int crttask = 0;
	for (int i = 0; i < 12; i++) {
		delay001s();
		CallFar(0, tasks[i % numsof(tasks)] << 3);
	}
	return 0xFEDC3210;
	while (true) wait();
}

extern "C" void dbgfn();
static void init(bool cls) {
	if (cls)
	{
		scrrol(25);// clear screen 80x25
		curset(0);
	}
	// create tasks of HelloX subapps.
	if (1) {
		memalloc(0xA000);// store programs

		//{TODO} dynamically allocate memory for Task
		outs("Setup Hello-A, entry:");
		tasks[0] = UserTaskLoadFromELF32((pureptr_t)0x32000);
		outi32hex(getTaskEntry(tasks[0])); outs("\n\r");
		
		outs("Setup Hello-B, entry:");
		tasks[1] = UserTaskLoadFromELF32((pureptr_t)0x36000);
		outi32hex(getTaskEntry(tasks[1])); outs("\n\r");

		outs("Setup Hello-C, entry:");
		tasks[2] = UserTaskLoadFromELF32((pureptr_t)0x3A000);
		outi32hex(getTaskEntry(tasks[2])); outs("\n\r");

		if (0) dbgfn();
		// then you can jmpFar or CallFar one's TSS
	}
	if (true) {
		outs("\n\r\x02Memory Bitmap with 16:\x01");
		outi16hex(*(word *)0x8000052A);
		outs(", 32:\x01");
		outi32hex(*(dword *)0x8000052C);
		byte *memmap_ptr = (byte *)0x60000;
		for (stduint i = 0; i < 64; i++) {
			if (!(i % 16)) {
				outs("\n\r");
				outi32hex(0x0008000 * i);// 0x00080000 * (i / 16)
				outs(" ");
			}
			outi8hex(memmap_ptr[i]);
			outc(' ');
		}
		outs("\n\r");
	}
	

	InterruptEnable();
}
