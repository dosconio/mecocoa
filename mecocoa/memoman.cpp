// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include "c/arith.h"
// for x86, one slice should begin with 0x100000 (above F000:FFFF)

_ESYM_C Handler_t FILE_ENTO, FILE_ENDO;

Memory mem;
BmMemoman* Memory::pagebmap = NULL;
stduint Memory::total_memsize = 0;
bool map_ready = false;


Mempool mempool = {};
#if 0 // for small flash board
#define mempool (*pmempool)
#endif

// - Memory::clear_bss
void Memory::clear_bss() {
	MemSet(&BSS_ENTO, &BSS_ENDO - &BSS_ENTO, 0);
}



// ---- x86 ----
#ifdef _ARC_x86 // x86:
Letvar(Memory::p_basic, byte*, 0x1000); //.. 0x7000
Letvar(Memory::p_ext, byte*, mem_area_exten_beg);
stduint Memory::total_mem = 0;
usize Memory::areax_size = 0;

// Allocate Physical-Continuous Memory
// unit: x86 Page 0x1000
void* Memory::physical_allocate(usize siz) {
	if (siz & 0xFFF) siz = (siz & ~_IMM(0xFFF)) + 0x1000;
	void* ret = nil;
	if (map_ready) {
		ret = mem.allocate(siz, 12);
	}
	else { // not support pg-mapping
		void* ret = p_ext;
		p_ext += siz;
		return ret;
	}
	return ret;
}

#endif


// ---- INTERFACE ----

#if 1
#if _MCCA == 0x8632
void* (*uni::_physical_allocate)(stduint size) = 0;
#endif

void* Memory::allocate(stduint siz, stduint alignment, stduint boundary) {
	(void)boundary;
	if (!map_ready) {
		plogerro("no pg-mapping");
		return nullptr;
	}
	if (siz & 0xFFF) siz = (siz & ~_IMM(0xFFF)) + 0x1000;
	void* ret = nil;
	// find a available page in bitmap
	siz >>= 12;
	if (!Memory::pagebmap->avail_pointer) {
		plogerro("no avail page");
		return nullptr;
	}
	stduint sum_cont = 0;
	stduint sum_beg = Memory::pagebmap->avail_pointer;
	for (stduint p = Memory::pagebmap->avail_pointer; p < 0x100000; p++) {
		if (Memory::pagebmap->bitof(p)) {
			if (!sum_cont) sum_beg = p;
			sum_cont++;
			if (sum_cont >= siz) break;
		}
		else {
			sum_cont = 0;
		}
	}
	if (!(sum_cont >= siz)) {
		plogerro("no avail pages");
		return nullptr;
	}
	ret = (void*)(sum_beg << 12);
	Memory::pagebmap->add_range(sum_beg, sum_beg + siz, false);

	// printlog(_LOG_INFO, "malloc(0x%[32H], %[x])", ret, siz << 12);
	return ret;
}
bool Memory::deallocate(void* ptr, stduint size _Comment(zero_for_block)) {
	_TODO return false;
}

#endif

// ---- PAGING ----

Paging kernel_paging;

// ---- SEGMENT ----

#if (_MCCA & 0xFF00) == 0x8600

uint64 GDT_LIST[]{
	0x0000000000000000ull,//(SegNull) Ring0
	0x000F9A000000FFFFull,//(SegCo16) Ring0
	0x0020980000000000ull,//(SegCo64) Ring0
	0x00CF92000000FFFFull,//(SegData) Ring0
	0x00CF9A000000FFFFull,//(SegCo32) Ring0
	0x00CFF2000000FFFFull,//(Rg3Data) Ring3
	0x0020FA0000000000ull,//(Rg3Code) Ring3
	0x0000000000000000ull,//(SegCall) Ring3
	0x0000000000000000ull,//(SegGldt) Ring3
	#if __BITS__ == 64
	0x0000000000000000ull,
	#endif
	0x0000890000000000ull,//(SegTTS0) Ring0
	#if __BITS__ == 64
	0x0000000000000000ull,
	#endif
};// no address and limit for x64

#endif

#if (_MCCA & 0xFF00) == 0x8600
_ESYM_C void RefreshGDT();
// previous GDT may be broken, omit __asm("sgdt _buf");

void GDT_Init() {
	(*mecocoa_global).gdt_ptr = (mec_gdt*)(0x600);
	MemCopyN(mecocoa_global->gdt_ptr, GDT_LIST, sizeof(GDT_LIST));
	#if _MCCA == 0x8632
	mecocoa_global->gdt_ptr->rout.setModeCall(mglb(Handint_SYSCALL_Entry), SegCo32);
	mecocoa_global->gdt_ptr->cor3 = mecocoa_global->gdt_ptr->code;
	mecocoa_global->gdt_ptr->cor3.DPL = 3;
	#endif
	loadGDT(_IMM(mecocoa_global->gdt_ptr), mecocoa_global->gdt_len = sizeof(mec_gdt) - 1);
	#if _MCCA == 0x8632
	RefreshGDT();
	#elif _MCCA == 0x8664
	setDSAll(SegData);
	setCSSS(SegCo64, SegData);
	#endif
}
void GDT_Next() {
	(*mecocoa_global).gdt_ptr = (mec_gdt*)mglb(0x600);
	loadGDT(_IMM(mecocoa_global->gdt_ptr), mecocoa_global->gdt_len = sizeof(mec_gdt) - 1);
	#if _MCCA == 0x8632
	RefreshGDT();
	#elif _MCCA == 0x8664
	setDSAll(SegData);
	setCSSS(SegCo64, SegData);
	#endif
}

word GDT_GetNumber() {
	return (mecocoa_global->gdt_len + 1) / 8;
}

// allocate and apply, return the allocated number
word GDT_Alloc() {
	word ret = mecocoa_global->gdt_len + 1;
	loadGDT(_IMM(mecocoa_global->gdt_ptr), mecocoa_global->gdt_len += 8);
	return ret;
}
#endif

// ---- STDLIB ----

#if 1

// linear allocator
_ESYM_C void* malloc(size_t size) {
	auto ret = (mempool.allocate(size));
	if (!ret)
		printlog(ret ? _LOG_INFO : _LOG_ERROR, "malloc %u at 0x%[x]", size, ret);
	return ret;
}
_ESYM_C void* calloc(size_t nmemb, size_t size) {
	// ploginfo("calloc %u %u", nmemb, size);
	void* ret = malloc(nmemb * size);
	if (ret) MemSet(ret, 0, nmemb * size);
	return ret;
}
void* operator new(size_t size) {
	return malc(size);
}
void* operator new[](size_t size) {
	// ploginfo("new[] %u", size);
	return malc(size);
}
void* operator new[](size_t size, std::align_val_t ali) {
	auto bit = intlog2_iexpo _IMM(ali);
	return mempool.allocate(size, bit);
}
void operator delete(void* p) {
	free(p);
}
void operator delete[](void* p) {
	free(p);
}
void operator delete(void* ptr, stduint size) noexcept {
	if (!mempool.deallocate(ptr, size)) plogerro("del BAD");
}
void operator delete[](void* ptr, stduint size) {
	::operator delete(ptr, size);
}
#if defined(_UEFI)
void operator delete(void* ptr, stduint size, std::align_val_t) noexcept { ::operator delete(ptr, size); }
#endif
//
_ESYM_C void free(void* p) {
	bool a = (mempool.deallocate(p));
	if (!a) printlog(a ? _LOG_INFO: _LOG_ERROR, "mfree 0x%[x]", p);
}
_ESYM_C void memf(void* ptr) { free(ptr); }
#endif


