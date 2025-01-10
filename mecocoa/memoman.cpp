// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @dosconio
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _DEBUG
#include <c/consio.h>
#include "../include/atx-x86-flap32.hpp"


use crate uni;
#ifdef _ARC_x86 // x86:

Letvar(Memory::p_basic, byte*, 0x1000); //.. 0x7000
Letvar(Memory::p_ext, byte*, mem_area_exten_beg);
usize Memory::areax_size = 0;

// need not Bitmap
namespace uni {
	void* (*_physical_allocate)(stduint size) = 0;
}

usize Memory::evaluate_size() {
	// TEMP check 0x00100000~0x80000000, 2-power size.
	volatile union { word* _p; usize _i; } p;
	p._i = mem_area_exten_beg;
	*p._p = 0x5AA5;
	if (*p._p == 0x5AA5) do {
		p._i = p._i << 1;
		*(p._p - 1) = 0x1227;
		// printlog(_LOG_INFO, "%s at %[32H]", __FUNCIDEN__, p._p - 1);
		if (*(p._p - 1) != 0x1227)
		{
			p._i = p._i >> 1;
			break;
		}
	} while (p._i < 0x80000000);
	areax_size = p._i - mem_area_exten_beg;
	return mem_area_exten_beg + areax_size;
}

// Page size at least
void* Memory::physical_allocate(usize siz) {
	if (siz & 0xFFF) siz = (siz & ~_IMM(0xFFF)) + 0x1000;
	if (p_basic + siz <= (void*)0x7000) {
		void* ret = p_basic;
		p_basic += siz;
		if (opt_info) printlog(_LOG_INFO, "malloc_low(0x%[32H], %[u])", ret, siz);
		//{} Pag-map
		return ret;
	}
	else if (usize(p_ext) + siz <= 0x00100000 + Memory::areax_size) {
		void* ret = p_ext;
		p_ext += siz;
		//{} Pag-map
		return ret;
	}
	return nullptr;
}

rostr Memory::text_memavail(uni::String& ker_buf) {
	usize mem = evaluate_size();
	char unit[]{ ' ', 'K', 'M', 'G', 'T' };
	int level = 0;
	// assert mem != 0
	while (!(mem % 1024)) {
		level++;
		mem /= 1024;
	}
	ker_buf.Format(level ? "%d %cB" : "0x%[32H] B", mem, unit[level]);
	return ker_buf.reference();
}

// ---- Paging ----

Paging kernel_paging;
extern stduint tmp;

_TEMP void page_init() {
	Paging::page_directory = (PageDirectory*)Memory::physical_allocate(0x1000);
	kernel_paging.Reset();
	PageDirectory& pdt = *Paging::page_directory;
	//
	for0(i, 0x400)// kernel linearea 0x00000000 ~ 0x00400000
		pdt[0][i].setMode(true, true, _TEMP true, i << 12);
		// pdt[0][i].setMode(true, true, false, i << 12);
	// pdt[0x3FF][0x3FF].setMode(true, true, false, _IMM(Paging::page_directory));// make loop PDT and do not unisym's
	pdt[0x3FF][0x3FF].setMode(true, true, _TEMP true, _IMM(Paging::page_directory));// make loop PDT and do not unisym's
	for0(i, 0x400)// global linearea 0x80000000 ~ 0x80400000
		pdt[0x200][i].setMode(true, true, true, i << 12);
	//
	tmp = _IMM(Paging::page_directory);
	__asm("movl tmp, %eax\n");
	__asm("movl %eax, %cr3\n");
	__asm("movl %cr0, %eax\n");
	__asm("or   $0x80000000, %eax\n");// enable paging
	__asm("movl %eax, %cr0\n");
	rostr test_page = (rostr)"\xFF\x70[Mecocoa]\xFF\x02 Paging Test OK!\xFF\x07" + 0x80000000;
	if (opt_test) Console.FormatShow("%s\n\r", test_page);
}

// ----

extern "C" {
	// linear allocator
	stduint strlen(const char* ptr) { return StrLength(ptr); }
	void* malloc(size_t size) {
		Console.FormatShow("malloc(%[u])\n\r", size);
		return nullptr;
	}
	void* calloc(size_t nmemb, size_t size) {
		Console.FormatShow("calloc(%[u])\n\r", nmemb * size);
		return nullptr;
	}
	void free(void* ptr) {
		Console.FormatShow("free(0x%[32H])\n\r", ptr);
	}
	void memf(void* ptr) { free(ptr); }
	void* memset(void* str, int c, size_t n) {
		return MemSet(str, c, n);
	}
}

#endif

/*
delete (void*) new int;
*/
