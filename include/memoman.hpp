// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @dosconio
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#include <c/stdinc.h>
#include <c/bitmap.h>
#include <c/system/paging.h>
#include <cpp/string>

#if (_MCCA & 0xFF00) == 0x8600

/*
*  00 NULL
*  01 Data 16/32/64
*  02 Code 32
*  03 Call
*  04 Code 16
*  05 Code 64
*  .. TSS and LDT
*/
enum {
	SegNull = 8 * 0,
	SegData = 8 * 1,
	SegCo32 = 8 * 2,
	SegCall = 8 * 3,
	SegCo16 = 8 * 4,
	#if __BITS__ == 64
	SegCo64 = 8 * 5,
	#elif __BITS__ == 32
	SegTSS = 8 * 5,
	// LDT_App1, TSS_App1, LDT_App2, TSS_App2, ...
	#endif
};

#endif

#ifdef _ARC_x86 // x86 and ... 32bit

extern "C" byte BSS_ENTO, BSS_ENDO;

#define mem_area_exten_beg 0x00100000
class Memory {
// area-basic: 0x7E00 .. 0x80000
// area-exten: mem_area_exten_beg .. mem_area_exten_beg + areax_size
public: // previously used
	static byte* p_basic;
	static byte* p_ext;
	static usize total_mem;
	static usize areax_size;
public:
	static usize avail_pointer;// to the lowest available page
	static Slice avail_slices[4];
	//
	static Bitmap* pagebmap;// 0x100000 pages / 8 bpB = 0x20000 bytes
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
	static bool init(stduint eax, byte* ebx);
};



extern void dump_avail_memory();

//

extern Paging kernel_paging;// cpu0 running

// [x86]
void GDT_Init();
word GDT_GetNumber();
word GDT_Alloc();

#endif
