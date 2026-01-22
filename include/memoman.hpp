// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @dosconio
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#include <c/stdinc.h>
#include <c/bitmap.h>
#include <c/system/paging.h>
#include <cpp/string>
#include <cpp/trait/MallocTrait.hpp>

struct BmMemoman : public Bitmap {
	static bool map_ready;
	usize avail_pointer = ~_IMM0;// to the lowest available page
	BmMemoman(pureptr_t offs, stduint size) : Bitmap(offs, size) {}
	//
	// e.g. add_range(0x1, 0x2) ofr 0x1000 ~ 0x1FFF
	void update_avail_pointer(stduint head_pos, stduint last_pos, bool what);
	//
	void add_range(stduint head_pos, stduint last_pos, bool what);
	//
	void dump_avail_memory();
};


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

extern byte BSS_ENTO, BSS_ENDO;

#if _MCCA == 0x8632
#define mem_area_exten_beg 0x00100000
// area-basic: 0x7E00 .. 0x80000
// area-exten: mem_area_exten_beg .. mem_area_exten_beg + areax_size
#endif

#endif





// class Memory
#ifdef _ARC_x86 // x86 and ... 32bit

class Memory {
public: // previously used
	static byte* p_basic;
	static byte* p_ext;
	static usize total_mem;
	static usize areax_size;
public:
	static BmMemoman* pagebmap;// [single for 4G] 0x100000 pages / 8 bpB = 0x20000 bytes


	
public:
	static void clear_bss();
	static bool init(stduint eax, byte* ebx);
	
public:
	static Slice avail_slices[4];
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

	static void* allocate(stduint siz);
	static bool deallocate(void* ptr, stduint size _Comment(zero_for_block));

};

#elif _MCCA == 0x8664
// {TEMP} 0x00000000_00000000..0x00000001_00000000
class Memory : public trait::Malloc
{
public:
	static BmMemoman* pagebmap;// 1 map, manage first 4G
public:
	static void clear_bss();
	static bool initialize(stduint eax, byte* ebx);
public:// trait
	virtual void* allocate(stduint size);
	virtual bool deallocate(void* ptr, stduint size = 0 _Comment(zero_for_block));
};
extern Memory mem;
#endif

// ---- PAGING ----

extern Paging kernel_paging;

// ---- SEGMENT ----
#if (_MCCA & 0xFF00) == 0x8600
void GDT_Init();
#ifdef _ARC_x86
word GDT_GetNumber();
word GDT_Alloc();
#endif
#endif
