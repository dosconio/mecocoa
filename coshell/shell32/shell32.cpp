// ASCII C++ TAB4 CRLF
// Attribute: 
// LastCheck: 20240216
// AllAuthor: @dosconio
// ModuTitle: Shell Flat Prot-32
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

//{TODO} to be COTLAB. Jump to here as console.

#include "shell32.h"

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
		stduint n;// how many PHBlock loaded
		void *hello_entries[3]; // Hello-A -B -C

		outs("Setup Hello-A, entry:");
		n = ELF32_LoadExecFromMemory((pureptr_t)0x32000, &hello_entries[0]);
		dword helloa_esps[] = {0x2C200,0x2C400,0x2C600,0x2C800};
		Task3FromELF32((TSS_t*)0x2A100,
			*((descriptor_t**)0x80000506), (MccaAlocGDT()-1), // LDT
			(descriptor_t *)0x2A000,
			(void *)0x32000, 8*7, helloa_esps);
		GlobalDescriptor32Set(&(*((descriptor_t**)0x80000506))[MccaAlocGDT()-1],
			0x2A100, 103, 9, // 9: 386TSS
			0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */
		);// TSS [0x00408900]
		outi32hex((stduint)hello_entries[0]); outs("\n\r");

		outs("Setup Hello-B, entry:");
		n = ELF32_LoadExecFromMemory((pureptr_t)0x36000, &hello_entries[1]);
		dword hellob_esps[] = {0x2C800,0x2CA00,0x2CC00,0x2CE00};
		Task3FromELF32((TSS_t*)0x2A300,
			*((descriptor_t**)0x80000506), (MccaAlocGDT()-1), // LDT
			(descriptor_t *)0x2A200,
			(void *)0x36000, 8*7, hellob_esps);
		GlobalDescriptor32Set(&(*((descriptor_t**)0x80000506))[MccaAlocGDT()-1],
			0x2A300, 103, 9, // 9: 386TSS
			0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */
		);// TSS [0x00408900]
		outi32hex((stduint)hello_entries[1]); outs("\n\r");

		outs("Setup Hello-C, entry:");
		n = ELF32_LoadExecFromMemory((pureptr_t)0x3A000, &hello_entries[2]);
		dword helloc_esps[] = {0x2D200,0x2D400,0x2D600,0x2D800};
		Task3FromELF32((TSS_t*)0x2A500,
			*((descriptor_t**)0x80000506), (MccaAlocGDT()-1), // LDT
			(descriptor_t *)0x2A400,
			(void *)0x3A000, 8*7, helloc_esps);
		GlobalDescriptor32Set(&(*((descriptor_t**)0x80000506))[MccaAlocGDT()-1],
			0x2A500, 103, 9, // 9: 386TSS
			0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */
		);// TSS [0x00408900]
		outi32hex((stduint)hello_entries[2]); outs("\n\r");

		if (0) dbgfn();
		jmpFar(0, (0x10 + 1) << 3); // TSS
		jmpFar(0, (0x10 + 1) << 3); // TSS
		CallFar(0, (0x10 + 1) << 3);
		CallFar(0, (0x10 + 1) << 3);
		CallFar(0, (0x12 + 1) << 3);
		CallFar(0, (0x12 + 1) << 3);
		CallFar(0, (0x14 + 1) << 3);
		CallFar(0, (0x14 + 1) << 3);
	}
	InterruptEnable();
}
