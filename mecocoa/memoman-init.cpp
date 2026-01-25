// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST

#include <c/consio.h>

use crate uni;

_ESYM_C Handler_t FILE_ENTO, FILE_ENDO;
#if _MCCA == 0x8632
#include "../include/atx-x86-flap32.hpp"
#include "../prehost/atx-x86-flap32/multiboot2.h"
// for x86, one slice should begin with 0x100000 (above F000:FFFF)
// for x86, only consider single paging method

#elif _MCCA == 0x8664
#include "../include/atx-x64-uefi64.hpp"

#endif

//
_PACKED(struct) memory_info_entry {
	uint64 addr;
	uint64 len;
	uint32 type;// 1 for avail, 2 for not
};// ARDS
#if (_MCCA & 0xFF00) == 0x8600
byte _BUF_pagebmap[sizeof(BmMemoman)];
#ifdef _UEFI
const stduint mapsize = 0x20000;
static byte mapping_4G[mapsize];
#endif 
#endif
#if _MCCA == 0x8632
extern Handler_t MemoryList;
extern word SW16_FUNCTION;
extern memory_info_entry MemoryListData[20];
static void mm_init_cuthead();
//
static stduint parse_norm(stduint addr);
static stduint parse_grub(stduint addr);

#elif _MCCA == 0x8664
//
static void parse_uefi(const MemoryMap& memory_map);
#endif
#if _MCCA == 0x8632
bool Memory::initialize(stduint eax, byte* ebx) {
	// make available memory into a group of slices
	//{TODO} cancel avail_slices and parse_* skip (0..kernel_endo)
	switch (eax)
	{
	case 'ANIF':// from loader
		SW16_FUNCTION = _IMM(&MemoryList);
		__asm("call SwitchReal16");
		parse_norm(_IMM(MemoryListData));
		mm_init_cuthead();
		break;
	case MULTIBOOT2_BOOTLOADER_MAGIC:
		parse_grub(_IMM(ebx));
		mm_init_cuthead();
		break;
	default:
		return false;
	}
	Slice& slice = Memory::avail_slices[0];
	stduint mapsize = 0x20000;
	void* mapaddr;
	if (slice.address + slice.length <= 0x00100000 && slice.length > mapsize) {
		mapaddr = (pureptr_t)slice.address;
		slice.address += mapsize;
		slice.length -= mapsize;
	} // consider into kernel
	else {
		mapaddr = Memory::physical_allocate(mapsize);
	}
	MemSet(mapaddr, 0, mapsize);
	Memory::pagebmap = new (_BUF_pagebmap) BmMemoman(mapaddr, mapsize);
	for0(i, numsof(Memory::avail_slices)) {
		stduint slice_endpoint = Memory::avail_slices[i].address + Memory::avail_slices[i].length;
		if (!Memory::avail_slices[i].address && !Memory::avail_slices[i].length) break;
		if (Memory::avail_slices[i].address == 0x100000) {
			if (_IMM(Memory::p_ext) >= slice_endpoint) {
				return false;
			}
			Memory::pagebmap->add_range(_IMM(Memory::p_ext) >> 12, slice_endpoint >> 12, true);
		}
		else {
			Memory::pagebmap->add_range(Memory::avail_slices[i].address >> 12, slice_endpoint >> 12, true);
		}
	}
	Memory::pagebmap->add_range(0x80, 0xA0, false);// Extended BIOS Data Area
	map_ready = true;
	return true;
}
#elif _MCCA == 0x8664
bool Memory::initialize(stduint eax, byte* ebx) {
	void* mapaddr = mapping_4G;
	MemSet(mapaddr, 0, mapsize);
	Memory::pagebmap = new (_BUF_pagebmap) BmMemoman(mapaddr, mapsize);
	// fill the mbitmap
	// * here
	// - physical_allocate (which be with pagemap)
	// - free [TODO]
	switch (eax)
	{
	case 'UEFI':
		parse_uefi(*reinterpret_cast<MemoryMap*>(ebx));
		break;
	default:
		return false;
	}
	map_ready = true;
	return true;
}
#endif


#if _MCCA == 0x8632
static void mm_init_cuthead() {
	// reduce the area that the kernel used
	stduint kernel_end = _IMM(&FILE_ENDO);
	kernel_end = vaultAlignHexpow(0x1000, kernel_end);
	Slice& slice = Memory::avail_slices[0];
	const Slice empty_slice = { 0,0 };

	// ceil base and floor length
	slice.length = floorAlign(0x1000, slice.length);
	while ((slice.address || slice.length) && slice.address < kernel_end) {
		// ---------------  section
		// ....---------     kernel
		if (slice.address + slice.length <= kernel_end) {
			for0(i, numsof(Memory::avail_slices) - 1) {
				Memory::avail_slices[i] = Memory::avail_slices[i + 1];
			}
			Memory::avail_slices[numsof(Memory::avail_slices) - 1] = empty_slice;
		}
		else {
			stduint diff = slice.address + slice.length - kernel_end;
			slice.address = kernel_end;
			slice.length = diff;
		}
	}
	for0(i, numsof(Memory::avail_slices)) {
		if (!Memory::avail_slices[i].address && !Memory::avail_slices[i].length) break;
		Memory::avail_slices[i].address = vaultAlign(0x1000, Memory::avail_slices[i].address);
		Memory::avail_slices[i].length = floorAlign(0x1000, Memory::avail_slices[i].length);
		if (!Memory::avail_slices[i].length) {
			for (unsigned j = i; j < numsof(Memory::avail_slices); j++) {
				Memory::avail_slices[j] = j + 1 < numsof(Memory::avail_slices) ? Memory::avail_slices[j + 1] : empty_slice;
			}
			i--;
		}
	}
}
#endif

// ---- parse* ---- //

#if _MCCA == 0x8632

static stduint parse_norm(stduint addr) {
	memory_info_entry* entry = (memory_info_entry*)addr;
	stduint count = 0;
	stduint picked = 0;
	for0(i, numsof(MemoryListData)) {
		if (!entry->addr && !entry->len) break;
		count++;
		// outsfmt("[Memoman] 0x%[64H]..0x%[64H] : 0x%x\n\r", entry->addr, entry->addr + entry->len, entry->type);
		if (entry->type == 1) {
			Memory::avail_slices[picked].address = entry->addr;
			Memory::avail_slices[picked].length = entry->len;
			Memory::total_mem += entry->len;
			picked++;
		}
		entry++;
	}
	return count;
}

static stduint parse_grub(stduint addr)
{
	stduint count = 0;
	stduint picked = 0;
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
			Memory::avail_slices[picked].address = (u32)entry->addr;
			Memory::avail_slices[picked].length = (u32)entry->len;
			Memory::total_mem += entry->len;
			picked++;
		}
		cast<stduint>(entry) += mtag->entry_size;
	}
	return count;
}

#elif _MCCA == 0x8664

static void parse_uefi(const MemoryMap& memory_map) {
	byte bmap_FFFF[2];// 0x10 pages / 8 = 2
	// UEFI had reduced the area that the kernel used
	for (uintptr_t iter = reinterpret_cast<uintptr_t>(memory_map.buffer);
		iter < reinterpret_cast<uintptr_t>(memory_map.buffer) + memory_map.map_size;
		iter += memory_map.descriptor_size) {
		auto desc = reinterpret_cast<MemoryDescriptor*>(iter);
		if (MemIsAvailable((MemoryType)desc->type)) {
			if (desc->physical_start + desc->number_of_pages * 4096 >= 0x100000000ULL) break;
			stduint beg = vaultAlign(0x1000, desc->physical_start) >> 12;
			Memory::pagebmap->add_range(beg, beg + desc->number_of_pages, true);
			// ploginfo("type = %u, %[x]..%[x], attr=0x%[x]", desc->type, desc->physical_start, desc->physical_start + desc->number_of_pages * 4096, desc->attribute);
		}
	}
	Bitmap BM_FFFF(bmap_FFFF, 2);
	for0(i, 0x10) {
		bool b = Memory::pagebmap->bitof(i);
		BM_FFFF.setof(i, b);
	}
	Memory::pagebmap->add_range(0, 0x10, false);
	Memory::pagebmap->add_range(0x80, 0x100, false);// BIOS and Upper Memory Area
}


#endif






