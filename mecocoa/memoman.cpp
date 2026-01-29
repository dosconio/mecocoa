// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST

#include <c/consio.h>

use crate uni;

_ESYM_C Handler_t FILE_ENTO, FILE_ENDO;

#if _MCCA == 0x8632
#include "../include/atx-x86-flap32.hpp"
#include "../prehost/atx-x86-flap32/multiboot2.h"
// for x86, one slice should begin with 0x100000 (above F000:FFFF)
// for x86, only consider single paging method

#elif _MCCA == 0x8664
#include "../include/atx-x64.hpp"

#endif

#if (_MCCA & 0xFF00) == 0x8600
Memory mem;
BmMemoman* Memory::pagebmap = NULL;
bool map_ready = false;
#endif
#ifdef _UEFI

#endif

// - Memory::clear_bss
#if (_MCCA & 0xFF00) == 0x8600
void Memory::clear_bss() {
	MemSet(&BSS_ENTO, &BSS_ENDO - &BSS_ENTO, 0);
}
#endif


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
		ret = mem.allocate(siz);
	}
	else { // not support pg-mapping
		if (p_basic + siz <= (void*)0x6000) {
			void* ret = p_basic;
			p_basic += siz;
			return ret;
		}
		else {
			void* ret = p_ext;
			p_ext += siz;
			return ret;
		}
	}
	return ret;
}

#endif


// ---- INTERFACE ----

#if (_MCCA & 0xFF00) == 0x8600
#if _MCCA == 0x8632
void* (*uni::_physical_allocate)(stduint size) = 0;
#endif

void* Memory::allocate(stduint siz) {
	if (!map_ready) return nullptr;
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
	// ploginfo("malc %x %x %[x]", sum_beg, sum_beg + siz, ret);
	Memory::pagebmap->add_range(sum_beg, sum_beg + siz, false);

	#if _MCCA == 0x8632
	kernel_paging.MapWeak(_IMM(ret), _IMM(ret), siz << 12, true, true);
	#endif

	// printlog(_LOG_INFO, "malloc(0x%[32H], %[x])", ret, siz << 12);
	return ret;
}
bool Memory::deallocate(void* ptr, stduint size _Comment(zero_for_block)) {
	_TODO return false;
}
#endif

// ---- PAGING ----

#ifdef _ARC_x86
Paging kernel_paging;
#endif

// ---- SEGMENT ----

#ifdef _ARC_x86 // x86:

static const uint32 gdt_magic[] = {
	0x00000000, 0x00000000, // null
	0x0000FFFF, 0x00CF9200, // data
	0x0000FFFF, 0x00CF9A00, // code
	0x00000000, 0x00000000, // call
	0x0000FFFF, 0x000F9A00, // code-16
	0x00000000, 0x00209800, // code-64
	0x00000000, 0x00008900, // tss
	// 0x0000FFFF, 0x00CFFA00, // code r3
	// 0x0000FFFF, 0x00CFF200, // data r3
};

_ESYM_C void RefreshGDT();
// previous GDT may be broken, omit __asm("sgdt _buf");
void GDT_Init() {
	MemCopyN(mecocoa_global->gdt_ptr, gdt_magic, sizeof(gdt_magic));
	mecocoa_global->gdt_ptr->rout.setModeCall(_IMM(call_gate_entry()) | 0x80000000, SegCo32);// 8*3 Call Gate
	loadGDT(_IMM(mecocoa_global->gdt_ptr), mecocoa_global->gdt_len = sizeof(mec_gdt) - 1);// physical address
	RefreshGDT();
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

// No dynamic core

#elif _MCCA == 0x8664

static descriptor_t GDT64[16];
static unsigned number_of_GDT64 = 6;

static stduint rsp;
void GDT_Init() {
	GDT64[0]._data = nil;
	Descriptor64SetData(&GDT64[SegData >> 3], (_CPU_descriptor_type)0x2, 0);// +RW
	GDT64[1].setRange(0, 0xFFFFF);
	//
	Descriptor64SetCode(&GDT64[SegCo64 >> 3], (_CPU_descriptor_type)10, 0);// ~XR
	//
	// ploginfo("GDT64[1] = 0x%[64H]", GDT64[1]._data);
	// ploginfo("GDT64[5] = 0x%[64H]", GDT64[5]._data);
	loadGDT(_IMM(&GDT64), sizeof(descriptor_t) * number_of_GDT64 - 1);
	setDSAll(SegData);
	setCSSS(SegCo64, SegData);
}


#endif

// ---- STDLIB ----

#if (_MCCA & 0xFF00) == 0x8600
#if _MCCA == 0x8632
// linear allocator
_ESYM_C void* malloc(size_t size) {
	Console.OutFormat("malloc(%[u])\n\r", size);
	return nullptr;
}
_ESYM_C void* calloc(size_t nmemb, size_t size) {
	Console.OutFormat("calloc(%[u])\n\r", nmemb * size);
	return nullptr;
}
#elif _MCCA == 0x8664
void operator delete(void*) {}
void operator delete(void* ptr, unsigned long size) noexcept { _TODO }
#if defined(_UEFI)
void operator delete(void* ptr, unsigned long size, std::align_val_t) noexcept { ::operator delete(ptr, size); }
#endif
#endif
_ESYM_C void free(void* ptr) {
	#if _MCCA == 0x8632
	Console.OutFormat("free(0x%[32H])\n\r", ptr);
	#endif
}
_ESYM_C void memf(void* ptr) { free(ptr); }
#endif


