// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @dosconio
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#include <c/stdinc.h>
#include <c/system/paging.h>

#ifdef _ARC_x86 // x86 and ... 32bit

#define mem_area_exten_beg 0x00100000
class Memory {
// area-basic: 0x7E00 .. 0x80000
// area-exten: mem_area_exten_beg .. mem_area_exten_beg + areax_size
public:
	static byte* p_basic;
	static byte* p_ext;
	static usize areax_size;
public:
	static usize align_basic_4k() {
		usize& uaddr = *(usize*)&p_basic;
		usize last = uaddr;
		if (uaddr & 0xFFF) {
			uaddr = (uaddr & ~_IMM(0xFFF)) + 0x1000;
		}
		return uaddr - last;
	}
	_TEMP static usize evaluate_size() {
		// TEMP check 0x00100000~0x80000000, 2-power size.
		union { word* _p; usize _i; } p;
		p._i = mem_area_exten_beg;
		*p._p = 0x5AA5;
		if (*p._p == 0x5AA5) do {
			p._i <<= 1;
			*(p._p - 1) = 0x5AA5;
			if (*(p._p - 1) != 0x5AA5)
			{
				p._i >>= 1;
				break;
			}
		} while (p._i < 0x80000000);
		areax_size = p._i - mem_area_exten_beg;
		return mem_area_exten_beg + areax_size;
	}
	static void* physical_allocate(usize siz);
};


#endif
