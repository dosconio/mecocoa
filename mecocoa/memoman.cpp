// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @dosconio
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../include/memoman.hpp"
#include <c/consio.h>


use crate uni;
#ifdef _ARC_x86 // x86:

Letvar(Memory::p_basic, byte*, 0x7E00);
Letvar(Memory::p_ext, byte*, mem_area_exten_beg);
usize Memory::areax_size = 0;

// need not Bitmap
namespace uni {
	void* (*_physical_allocate)(stduint size) = 0;
}

_TEMP void* Memory::physical_allocate(usize siz) {
	if (p_basic + siz < (void*)0x80000) {
		void* ret = p_basic;
		p_basic += siz;
		return ret;
	}
	else if (usize(p_ext) + siz < 0x00100000 + Memory::areax_size) {
		void* ret = p_ext;
		p_ext += siz;
		return ret;
	}
	return nullptr;
}

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
