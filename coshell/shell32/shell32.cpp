// ASCII C++ TAB4 CRLF
// Attribute: 
// LastCheck: 20240216
// AllAuthor: @dosconio
// ModuTitle: Shell Flat Prot-32
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

//{TODO} to be COTLAB. Jump to here as console.

#include "shell32.h"
#include "mecocoa.h"

int main(void) {
	init(true);
	outs("Hello, " _CONCOL_DarkIoWhite "\nworld!\n\r");
	int crttask = 0;
	while (true) {
		delay001s();
		CallFar(0, (0x11 + (((crttask++) % 3)<<1)) << 3);
		// outs("<Ring~> ");
	}
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
		stduint n;// how many PHBlock loaded
		void *hello_entries[3]; // Hello-A -B -C

		//{TODO} dynamically allocate memory for Task
		outs("Setup Hello-A, entry:");
		n = ELF32_LoadExecFromMemory((pureptr_t)0x32000, &hello_entries[0]);
		TaskFlat_t helloa = {
			.LDTSelector = MccaAlocGDT(),
			.TSSSelector = MccaAlocGDT(),
			.parent = 8 * 7,
			.LDT = (descriptor_t *)memalloc(0x100),
			.TSS = (TSS_t *)memalloc(0x100),
			.ring = 3,
			.esp0 = (dword)memalloc(0x200)+0x200,
			.esp1 = (dword)memalloc(0x200)+0x200,
			.esp2 = (dword)memalloc(0x200)+0x200,
			.esp3 = (dword)memalloc(0x200)+0x200,
			.entry = (dword)hello_entries[0],
		};
		TaskFlatRegister(&helloa, MccaGDT);
		outi32hex((stduint)hello_entries[0]); outs("\n\r");

		outs("Setup Hello-B, entry:");
		n = ELF32_LoadExecFromMemory((pureptr_t)0x36000, &hello_entries[1]);
		TaskFlat_t hellob = {
			.LDTSelector = MccaAlocGDT(),
			.TSSSelector = MccaAlocGDT(),
			.parent = 8 * 7,
			.LDT = (descriptor_t *)memalloc(0x100),
			.TSS = (TSS_t *)memalloc(0x100),
			.ring = 3,
			.esp0 = (dword)memalloc(0x200)+0x200,
			.esp1 = (dword)memalloc(0x200)+0x200,
			.esp2 = (dword)memalloc(0x200)+0x200,
			.esp3 = (dword)memalloc(0x200)+0x200,
			.entry = (dword)hello_entries[1],
		};
		TaskFlatRegister(&hellob, MccaGDT);
		outi32hex((stduint)hello_entries[1]); outs("\n\r");

		outs("Setup Hello-C, entry:");
		n = ELF32_LoadExecFromMemory((pureptr_t)0x3A000, &hello_entries[2]);
		TaskFlat_t helloc = {
			.LDTSelector = MccaAlocGDT(),
			.TSSSelector = MccaAlocGDT(),
			.parent = 8 * 7,
			.LDT = (descriptor_t *)memalloc(0x100),
			.TSS = (TSS_t *)memalloc(0x100),
			.ring = 3,
			.esp0 = (dword)memalloc(0x200)+0x200,
			.esp1 = (dword)memalloc(0x200)+0x200,
			.esp2 = (dword)memalloc(0x200)+0x200,
			.esp3 = (dword)memalloc(0x200)+0x200,
			.entry = (dword)hello_entries[2],
		};
		TaskFlatRegister(&helloc, MccaGDT);
		outi32hex((stduint)hello_entries[2]); outs("\n\r");

		if (0) dbgfn();
		// then you can jmpFar or CallFar one's TSS
	}
	if (true) {
		outs("\n\rMemory Bitmap with 16:");
		outi16hex(*(word *)0x8000052A);
		outs(" 32:");
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
