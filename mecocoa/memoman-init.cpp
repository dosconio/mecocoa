// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <c/consio.h>
#include <c/mempool.h>

_ESYM_C Handler_t FILE_ENTO, FILE_ENDO;

//
_PACKED(struct) memory_info_entry {
	uint64 addr;
	uint64 len;
	uint32 type;// 1 for avail, 2 for not
};// ARDS
#if (_MCCA & 0xFF00) == 0x8600
byte _BUF_pagebmap[sizeof(BmMemoman)];

extern memory_info_entry MemoryListData[20];
extern Mempool mempool;

const stduint bmapsize = 0x100000 / 8;

// 0x0000000000000000 .. 0x0000000100000000
byte memoman_4G_00000000[bmapsize];

//
static stduint parse_norm(stduint addr);
#endif


#if _MCCA == 0x8632

static stduint parse_grub(stduint addr);

#elif _MCCA == 0x8664
//
static void parse_uefi(const MemoryMap& memory_map);

#endif

#if (_MCCA & 0xFF00) == 0x8600
// fill the mbitmap
// - init
// - allocate (which be with pagemap)
// - free (which be with unmap) [TODO]
bool Memory::initialize(stduint eax, byte* ebx) {
	#if _MCCA == 0x8632
	_physical_allocate = Memory::physical_allocate;
	kernel_paging.Reset();// should take 0x1000
	kernel_paging.MapWeak(0x00000000, 0x00000000, 0x00400000, true, _Comment(R0) true);
	kernel_paging.MapWeak(0x80000000, 0x00000000, 0x00400000, true, _Comment(R0) false);
	setCR3(_IMM(kernel_paging.root_level_page));
	PagingEnable();
	#endif
	GDT_Init();

	void* mapaddr = memoman_4G_00000000;
	MemSet(mapaddr, 0, bmapsize);
	Memory::pagebmap = new (_BUF_pagebmap) BmMemoman(mapaddr, bmapsize);
	switch (eax)
	{
	case 'ANIF':
		#ifndef _UEFI
		ebx = (byte*)(stduint)call_ladder(R16FN_SMAP);
		#endif
		parse_norm(_IMM(ebx));
		break;
		#if _MCCA == 0x8632
	case MULTIBOOT2_BOOTLOADER_MAGIC:
		parse_grub(_IMM(ebx));
		break;
		#endif

		#if _MCCA == 0x8664
	case 'UEFI':
		parse_uefi(*reinterpret_cast<MemoryMap*>(ebx));
		break;
		#endif
	default:
		return false;
	}
	byte bmap_FFFF[2];// 0x10 pages / 8 = 2
	Bitmap BM_FFFF(bmap_FFFF, 2);
	for0(i, 0x10) {
		bool b = Memory::pagebmap->bitof(i);
		BM_FFFF.setof(i, b);
	}
	Memory::pagebmap->add_range(
		floorAlign(0x1000, _IMM(&FILE_ENTO)) >> 12,
		vaultAlign(0x1000, _IMM(&FILE_ENDO)) >> 12,
		false
	);// kernel
	Memory::pagebmap->add_range(0, 0x10, false);
	Memory::pagebmap->add_range(0x78, 0x100, false);
	// - 0x78000~0x7FFFF Video Modes List
	// - 0x80000~0xFFFFF BIOS and Upper Memory Area
	map_ready = true;

	uni_default_allocator = &mem;
	// mempool (kernel heap)
	const unsigned mempool_len0 = 0x4000;
	mempool.Reset(Slice{ _IMM(mem.allocate(mempool_len0)), mempool_len0 });
	uni_hostenv_allocator = &mempool;

	// paging
	//{TEMP}
	#if _MCCA == 0x8664
	kernel_paging.Reset();
	kernel_paging.Map(0x00000000, 0x00000000, 0x100000000ULL * 16,
		21, PGPROP_present | PGPROP_writable
	);// pgsize 30 may be bad for Bochs, QEMU need map many times of 4G
	setCR3 _IMM(kernel_paging.root_level_page);

	#endif


	return true;
}
#endif


// ---- parse* ---- //
#if (_MCCA & 0xFF00) == 0x8600
static stduint parse_norm(stduint addr) {
	memory_info_entry* entry = (memory_info_entry*)addr;
	stduint count = 0;
	for0(i, numsof(MemoryListData)) {
		if (!entry->addr && !entry->len) break;
		count++;
		// outsfmt("[Memoman] 0x%[64H]..0x%[64H] : 0x%x\n\r", entry->addr, entry->addr + entry->len, entry->type);
		if (entry->type == 1) {
			stduint beg = vaultAlign(0x1000, entry->addr) >> 12;
			stduint end = floorAlign(0x1000, entry->addr + entry->len) >> 12;
			Memory::pagebmap->add_range(beg, end, true);
		}
		entry++;
	}
	#if _MCCA == 0x8632
	stduint end = vaultAlign(0x1000, _IMM(Memory::p_ext)) >> 12;
	Memory::pagebmap->add_range(mem_area_exten_beg >> 12, end, false);
	#endif
	return count;
}
#endif

#if _MCCA == 0x8632

static stduint parse_grub(stduint addr)
{
	stduint count = 0;
	stduint size = *(uint32*)addr;
	multiboot_tag* tag = (multiboot_tag*)(addr + 8);
	while (tag->type != MULTIBOOT_TAG_TYPE_END)
	{
		if (tag->type == MULTIBOOT_TAG_TYPE_MMAP)
			break;
		tag = (multiboot_tag*)(_IMM(tag) + ((tag->size + 7) & ~7));
	}
	//
	multiboot_tag_mmap* mtag = (multiboot_tag_mmap*)tag;
	multiboot_mmap_entry* entry = mtag->entries;
	while ((u32)entry < (u32)tag + tag->size)
	{
		// outsfmt("[Memoman] base 0x%[x]..0x%[x] : %d\n\r", (u32)entry->addr, (u32)entry->addr + (u32)entry->len, (u32)entry->type);
		count++;
		if (entry->type == MULTIBOOT_MEMORY_AVAILABLE && entry->len > 0)
		{
			stduint beg = vaultAlign(0x1000, entry->addr) >> 12;
			stduint end = floorAlign(0x1000, entry->addr + entry->len) >> 12;
			Memory::pagebmap->add_range(beg, end, true);
		}
		cast<stduint>(entry) += mtag->entry_size;
	}
	return count;
}

#elif _MCCA == 0x8664


static void parse_uefi(const MemoryMap& memory_map) {
	
	// UEFI had reduced the area that the kernel used
	stduint top = _IMM(memory_map.buffer) + memory_map.map_size;
	for (stduint iter = _IMM(memory_map.buffer); iter < top; iter += memory_map.descriptor_size) {
		auto desc = reinterpret_cast<MemoryDescriptor*>(iter);
		if (MemIsAvailable((MemoryType)desc->type)) {
			if (desc->physical_start + desc->number_of_pages * 4096 >= 0x100000000ULL) break;
			stduint beg = vaultAlign(0x1000, desc->physical_start) >> 12;
			Memory::pagebmap->add_range(beg, beg + desc->number_of_pages, true);
			// ploginfo("type = %u, %[x]..%[x], attr=0x%[x]", desc->type, desc->physical_start, desc->physical_start + desc->number_of_pages * 4096, desc->attribute);
		}
	}
	Memory::pagebmap->add_range(_IMM(memory_map.buffer) >> 12, vaultAlign(0x1000, top) >> 12, false);
}


#endif






