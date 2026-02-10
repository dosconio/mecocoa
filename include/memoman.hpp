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

struct mec_gdt {
	descriptor_t null;
	descriptor_t data;
	descriptor_t code;
	descriptor_t co16;
	descriptor_t co64;
	descriptor_t dar3;
	descriptor_t cor3;
	//
	gate_t rout;
	_CPU_descriptor_tss tss;// cpu0
};// on global linear area

enum {
	SegNull = 8 * 0,
	SegData = 8 * 1,
	SegCo16 = 8 * 2,
	SegCo32 = 8 * 3,
	SegCo64 = 8 * 4,
	SegDaR3 = 8 * 5,
	SegCoR3 = 8 * 6,
	//
	SegCall = offsetof(mec_gdt, rout),
	SegTSS0 = offsetof(mec_gdt, tss),
	// flap32: LDT_App1, TSS_App1, LDT_App2, TSS_App2, ...
};

struct mecocoa_global_t {
	uint16 ADDR_PARA0;
	uint16 ADDR_PARA1;
	uint16 ADDR_PARA2;
	uint16 ADDR_PARA3;
	volatile timeval_t system_time;
	word gdt_len;
	mec_gdt* gdt_ptr;
};
#define bda ((BIOS_DataArea*)0x400)
inline static mecocoa_global_t* mecocoa_global{ (mecocoa_global_t*)0x500 };
#define ADDR_BIOS_GDT 0x600// Migrate if over 0x400

_ESYM_C stduint CallCo16(stduint func);
__attribute__((optimize("O0")))
inline static uint16 call_ladder(uint16 func, uint16 para1 = 0, uint16 para2 = 0, uint16 para3 = 0) {
	Letvar(p, volatile uint16*, 0x500);
	p[1] = para1;
	p[2] = para2;
	p[3] = para3;
	CallCo16(func);
	return *p;
}
enum {
	R16FN_SMAP = 0,// (->addr)
	R16FN_VMOD = 1,// (mode->addr)
	R16FN_LSVM = 2,// list vmodes to 0x78000 (->)
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
	virtual void* allocate(stduint size, stduint alignment = 0) override;
	virtual bool deallocate(void* ptr, stduint size = 0 _Comment(zero_for_block)) override;
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
