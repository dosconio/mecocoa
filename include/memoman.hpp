// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @dosconio
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#include <c/stdinc.h>
#include <c/bitmap.h>
#include <c/mempool.h>
#include <c/system/paging.h>
#include <cpp/string>
#include <cpp/trait/MallocTrait.hpp>

#ifndef __MEMOMAN_HPP__
#define __MEMOMAN_HPP__

struct Spinlock;
struct DeviceNode;
class ProcessBlock;

#define PAGE_SIZE 0x1000 // lev-0-page

extern bool map_ready;

#if (_MCCA & 0xFF00) == 0x8600

struct mec_gdt {
	descriptor_t null;
	descriptor_t co16;
	descriptor_t co64;
	descriptor_t data;
	descriptor_t code;// 32
	descriptor_t dar3;
	descriptor_t cor3;
	//
	gate_t rout;
	descriptor_t ldt;
	_CPU_descriptor_tss tss;// cpu0
};// on global linear area

enum {
	SegNull = 8 * 0,
	SegCo16 = 8 * 1,
	SegCo64 = 8 * 2,
	SegData = 8 * 3,
	SegCo32 = 8 * 4,
	SegDaR3 = 8 * 5,
	SegCoR3 = 8 * 6,
	//
	SegCall = offsetof(mec_gdt, rout),
	SegGLDT = offsetof(mec_gdt, ldt),
	SegTSS0 = offsetof(mec_gdt, tss),
	// flap32: LDT_App1, TSS_App1, LDT_App2, TSS_App2, ...
};

_PACKED(struct) mecocoa_global_t {
	uint16 ADDR_PARA0;
	uint16 ADDR_PARA1;
	uint16 ADDR_PARA2;
	uint16 ADDR_PARA3;
	uint64 kernel_cr3;// offset 0x08
	uint16 gdt_len;// offset 0x10 for 64-bit
	mec_gdt* gdt_ptr;// 0x12
	volatile timeval_t system_time;//{TORM}
};

#define bda ((BIOS_DataArea*)0x400)
#define mcca_glblk 0x500

inline static mecocoa_global_t* mecocoa_global{ (mecocoa_global_t*)mcca_glblk };
#define ADDR_BIOS_GDT 0x600// Migrate if over 0x400

_ESYM_C stduint CallCo16(stduint func);
__attribute__((optimize("O0")))
inline static uint16 call_ladder(uint16 func, uint16 para1 = 0, uint16 para2 = 0, uint16 para3 = 0) {
	Letvar(p, volatile uint16*, 0x500);
	p[0] = func;
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

extern stduint acpi_rsdp_addr;
extern stduint acpi_mcfg_addr;
extern stduint acpi_dmar_addr;
extern uint64 acpi_pcie_ecam_base;
extern uint16 acpi_pcie_segment_group;
extern uint8 acpi_pcie_start_bus;
extern uint8 acpi_pcie_end_bus;
extern bool acpi_pcie_ecam_available;

namespace Iova {
	enum class DmaMappingFlag : uint32 {
		None = 0,
		Identity = 1 << 0,
	};

	struct DmaMapping {
		uint64 device_address;
		bool translated;
	};

	bool ProbeVtd(stduint dmar_address);
	bool EnableVtdPassthrough(DeviceNode* pci_root);
	bool MapDeviceDma(DeviceNode* device, stduint physical,
		stduint length, DmaMappingFlag flags, DmaMapping& mapping);
	bool UnmapDeviceDma(DeviceNode* device, uint64 device_address,
		stduint length);
}
#if _MCCA == 0x8632
#define mem_area_exten_beg 0x00101000
// area-basic: 0x7E00 .. 0x80000
// area-exten: mem_area_exten_beg .. mem_area_exten_beg + areax_size
#endif
#endif

extern byte BSS_ENTO, BSS_ENDO;
extern uni::Mempool mempool0;
class LockedAllocator : public trait::Malloc {
public:
	Mempool* base_allocator;
public:
	LockedAllocator(Mempool* base) : base_allocator(base) {}
	virtual void* allocate(stduint size, stduint alignment = 0, stduint boundary = 0) override;
	virtual bool deallocate(void* ptr, stduint size = 0 _Comment(zero_for_block)) override;
	//
	void Reset() { base_allocator->Reset(); }
	void Reset(const Slice& slice) { base_allocator->Reset(slice); }
	void Append(const Slice& slice) { base_allocator->Append(slice); }
};
extern LockedAllocator mempool;

#if CONFIG_ENABLE_MMU
struct InitialPageRegion {
	stduint begin;
	stduint end;
	stduint type;
	stduint permissions;
};

enum class ManagedPageSource {
	Anonymous,
	File,
	ELF
};

struct ManagedPageInfo {
	stduint physical_address;
	ManagedPageSource source;
	stduint mapping_refs;
	stduint cache_refs;
};
#endif

// class Memory
#if 1
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
	static stduint total_memsize;
	public:
	static void clear_bss();
	static bool initialize(stduint eax, byte* ebx);
	static void* physical_allocate(usize siz);
public:// trait
	virtual void* allocate(stduint size, stduint alignment = 0, stduint boundary = 0) override;
	virtual bool deallocate(void* ptr, stduint size = 0 _Comment(zero_for_block)) override;
public:// for loaders
	static void AppendAvailableRange(stduint beg, stduint end);
	#if CONFIG_ENABLE_MMU
	// The caller serializes the Paging instance; page_table_lock protects its write when provided.
	static bool MapPages(Paging& paging, stduint linear_address,
		stduint physical_address, stduint length,
		PageSizeShift page_size, stduint properties,
		Spinlock* page_table_lock = nullptr);
	static bool UnmapPages(Paging& paging, stduint linear_address,
		stduint length, Spinlock* page_table_lock = nullptr);
	static void ReleasePagingMappings(Paging& paging);
	// AdoptManagedPage takes ownership of physical_address, including on failure.
	static bool AdoptManagedPage(stduint physical_address,
		ManagedPageSource source);
	static bool RetainManagedPageMapping(stduint physical_address);
	static bool ReleaseManagedPageMapping(stduint physical_address);
	static bool RetainManagedPageCache(stduint physical_address);
	static bool ReleaseManagedPageCache(stduint physical_address);
	static bool QueryManagedPage(stduint physical_address,
		ManagedPageInfo& info);
	static void InvalidateProcessPage(ProcessBlock* pb, stduint address);
	#if _MCCA == 0x8632
	static void HandleTLBShootdownIPI();
	#endif
	static bool BuildInitialPagePlan(stduint boot_magic,
		const void* boot_info, InitialPageRegion*& plan,
		stduint& plan_count, stduint& plan_storage_bytes);
	static bool MapInitialPagePlan(Paging& paging,
		const InitialPageRegion* plan, stduint plan_count);
	#if defined(_ARC_x86) || defined(_ARC_x64)
	static bool MapFramebufferPages(stduint physical_address, stduint length);
	#endif
	#endif
	
};
extern Memory mem;
#endif

// ---- PAGING ----

#if CONFIG_ENABLE_MMU
extern Paging kernel_paging;
#endif
bool PrepareUserBuffer(ProcessBlock* pb, stduint address,
	size_t length, bool write,
	bool mapping_lock_held = false _Comment(caller_holds_pb_mutex_mapping));

// ---- SEGMENT ----
#if (_MCCA & 0xFF00) == 0x8600
void GDT_Init();
void GDT_Next();
#ifdef _ARC_x86
word GDT_GetNumber();
word GDT_Alloc();
#endif
#endif

#if (_MCCA & 0xFF00) == 0x8600
void DmaLowPoolInit(stduint base_phys);
void* DmaLowAlloc(stduint bytes);
bool DmaLowFree(void* ptr, stduint bytes = 0);
bool DmaLowIsInRange(void* ptr);
#endif

#endif // __MEMOMAN_HPP__
