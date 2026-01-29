// ASCII g++ TAB4 CRLF
// Attribute: Arch(AMD64)
// Codifiers: @ArinaMgk
// Docutitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// led by bootx64 and atx-x86-flap32.loader; we do not mix long64 and uefi64
#define _STYLE_RUST
#include "../../include/atx-x64.hpp"

_ESYM_C void mecocoa() {
	if (!Memory::initialize('ANIF', NULL)) HALT();
	//{TODO} GDT, PAGE
	auto paging_addr = mem.allocate(0x1000 * 5);
	Letvar(paging, uint64*, paging_addr);
	paging[0x800 / sizeof(uint64)] = paging[0] = _IMM(paging_addr) + 0x1007;
	paging[0x1000 / sizeof(uint64)] = _IMM(paging_addr) + 0x2007;
	paging[0x1000 / sizeof(uint64) + 1] = _IMM(paging_addr) + 0x3007;
	paging[0x1000 / sizeof(uint64) + 2] = _IMM(paging_addr) + 0x4007;
	paging[0x1000 / sizeof(uint64) + 3] = _IMM(paging_addr) + 0x5007;
	for0(i, 512 * 4) paging[0x2000 / sizeof(uint64) + i] = 0x200000 * i + 0x83;// 4G
	setCR3 _IMM(paging_addr);

	cons_init();
	Console.OutFormat("Ciallo~\r\n");

	Memory::pagebmap->dump_avail_memory();

	loop HALT();
}

