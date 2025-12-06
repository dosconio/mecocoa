// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @dosconio
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#include <c/stdinc.h>
#include <c/system/paging.h>
#include <cpp/string>

#ifdef _ARC_x86 // x86 and ... 32bit

extern "C" byte BSS_ENTO, BSS_ENDO;

#define mem_area_exten_beg 0x00100000
class Memory {
// area-basic: 0x7E00 .. 0x80000
// area-exten: mem_area_exten_beg .. mem_area_exten_beg + areax_size
public:
	static byte* p_basic;
	static byte* p_ext;
	static usize areax_size;
public:
	static void clear_bss() { MemSet(&BSS_ENTO, &BSS_ENDO - &BSS_ENTO, 0); }
	static usize align_basic_4k() {
		usize& uaddr = *(usize*)&p_basic;
		usize last = uaddr;
		if (uaddr & 0xFFF) {
			uaddr = (uaddr & ~_IMM(0xFFF)) + 0x1000;
		}
		return uaddr - last;
	}
	_TEMP static usize evaluate_size();
	static void* physical_allocate(usize siz);
	static rostr text_memavail(uni::String& ker_buf);
};



//

extern void page_init();

extern Paging kernel_paging;// cpu0 running

// [x86]
void GDT_Init();
word GDT_GetNumber();
word GDT_Alloc();

#endif
