// ASCII g++ TAB4 CRLF
// Attribute: Arch(AMD64)
// Codifiers: @ArinaMgk
// Docutitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// led by bootx64 and atx-x86-flap32.loader; we do not mix long64 and uefi64
#define _STYLE_RUST
#include "../../include/atx-x64.hpp"

// x86: GDT, PG, MEM (for ladder auto-enable PG)
// x64: GDT, MEM, PG

_ESYM_C void mecocoa() {
	if (!Memory::initialize('ANIF', NULL)) HALT();
	
	cons_init();
	//{} Cache_t::enAble();
	Taskman::Initialize();


	// IVT and Device
	InterruptControl APIC(_IMM(mem.allocate(256 * sizeof(gate_t))));
	APIC.Reset(SegCo64, 0x00000000);

	Console.OutFormat("Ciallo~\r\n");

	tryUD();

	Memory::pagebmap->dump_avail_memory();
	pureptr_t ptr;
	delete (ptr = new int);
	ploginfo("[Mempool] I try a new int, and it was at %[x]", ptr);
	ploginfo("[ Paging] ROOT PT at %[x]", kernel_paging.root_level_page);


	loop HALT();
}

