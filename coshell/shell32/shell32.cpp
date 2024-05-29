// ASCII C++ TAB4 CRLF
// Attribute: 
// LastCheck: 20240216
// AllAuthor: @dosconio
// ModuTitle: Shell Flap-32
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

//{TODO} to be COTLAB. Jump to here as console.

#include "arc-x86.h"
#include "shell32.h"

// debug
word tasks[4];

int main(void) {
	init(true);
	outsfmt("Ciallo, " CON_DarkIoWhite "\b\n%d-%d-%d" CON_None "!\n\r", 2024, 5, 4);
	int crttask = 0;
	for (int i = 0; i < 8; i++) {
		for0 (i, 500) delay001ms(); // half of delay001s();
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

		//{TODO} UserTaskLoadFromFilename
		outs("Setup Hello-A, entry:");
		tasks[0] = UserTaskLoadFromELF32((pureptr_t)0x32000);
		outi32hex(getTaskEntry(tasks[0])); outs("\n\r");
		
		outs("Setup Hello-B, entry:");
		tasks[1] = UserTaskLoadFromELF32((pureptr_t)0x36000);
		outi32hex(getTaskEntry(tasks[1])); outs("\n\r");

		outs("Setup Hello-C, entry:");
		tasks[2] = UserTaskLoadFromELF32((pureptr_t)0x3A000);
		outi32hex(getTaskEntry(tasks[2])); outs("\n\r");

		//{TEMP for Rust's linking} 0x3FFF'F000~0x3FFF'FFFF at 0x5'0000
		*(dword *)0x50000 = 7 | 0x00400000;
		*(dword *)0x50004 = 7 | 0x00401000;
		*(dword *)0x50008 = 7 | 0x00402000;
		*(dword *)0x80002004 = 7 | 0x50000;

		outs("Setup Hello-D, entry:");
		tasks[3] = UserTaskLoadFromELF32((pureptr_t)0x44000);
		outi32hex(getTaskEntry(tasks[3])); outs("\n\r");

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
