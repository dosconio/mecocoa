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

extern bool map_ready;

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
	SegCo64 = 8 * 5,
	SegTSS = 8 * 6,
	// flap32: LDT_App1, TSS_App1, LDT_App2, TSS_App2, ...
};

_PACKED(struct) VideoInfoEntry {
	uint16 mode;// 0x00
	uint16 width;// 0x02
	uint16 height;// 0x04
	union {
		uint16 bitmode;// 0x06
		struct {
			uint8 B_bytes : 4;// 0x06
			uint8 G_bytes : 4;
			uint8 R_bytes : 4;// 0x07
			uint8 A_bytes : 4;
		};
	};
};// RGB Direct Mode Only


extern byte BSS_ENTO, BSS_ENDO;

#if _MCCA == 0x8632
#define mem_area_exten_beg 0x00100000
// area-basic: 0x7E00 .. 0x80000
// area-exten: mem_area_exten_beg .. mem_area_exten_beg + areax_size
#endif
#endif

// class Memory
#if (_MCCA & 0xFF00) == 0x8600
// {TEMP} 0x00000000_00000000..0x00000001_00000000
class Memory : public trait::Malloc
{
public: // previously used
	#if _MCCA == 0x8632
	static byte* p_basic;
	static byte* p_ext;
	static usize total_mem;
	static usize areax_size;
	#endif
public:
	static BmMemoman* pagebmap;// 1 map for first 4G, 0x100000 pages / 8 bpB = 0x20000 bytes
	public:
	static void clear_bss();
	static bool initialize(stduint eax, byte* ebx);
	static void* physical_allocate(usize siz);
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
