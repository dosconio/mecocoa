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

void BmMemoman::update_avail_pointer(stduint head_pos, stduint last_pos, bool what) {
	// ploginfo("update_avail_pointer(0x%[32H], 0x%[32H], %d)", head_pos, last_pos, what);
	if (what) {
		MIN(avail_pointer, head_pos);
	}
	else if (avail_pointer == head_pos) {
		avail_pointer = last_pos;
		while (!this->bitof(avail_pointer)) {
			avail_pointer++;
			if (avail_pointer >= (this->size * _BYTE_BITS_)) {
				plogerro("Memory is all used");
				return;
			}
		}
	}
}
void BmMemoman::add_range(stduint head_pos, stduint last_pos, bool what) {
	// if (!map_ready) ploginfo("add_range(0x%[32H], 0x%[32H])", head_pos, last_pos);
	if (head_pos >= last_pos || head_pos >= 0x2000 * 8 || last_pos >= 0x2000 * 8) return;
	stduint times = last_pos - head_pos;
	stduint curr_pos = head_pos;
	while (times && (curr_pos & 0b111)) {
		this->setof(curr_pos, what);
		curr_pos++;
		times--;
	}
	while (times >= _BYTE_BITS_) {
		cast<byte*>(this->offs)[curr_pos / _BYTE_BITS_] = what ? 0xFF : 0x00;
		curr_pos += _BYTE_BITS_;
		times -= _BYTE_BITS_;
	}
	while (times) {
		this->setof(curr_pos, what);
		curr_pos++;
		times--;
	}
	update_avail_pointer(head_pos, last_pos, what);
}
void BmMemoman::dump_avail_memory()
{
	bool last_stat = false;
	stduint last_index = 0;
	outsfmt("[Memoman] dump avail memory:\n\r");
	for0(i, 0x2000 * 8) {
		bool b = this->bitof(i);
		if (b != last_stat) {
			if (!last_stat) {
				last_index = i;
				last_stat = b;
			}
			else {
				outsfmt("- 0x%[x]..0x%[x] \n\r", last_index * 0x1000, i * 0x1000);
				last_stat = b;
			}
		}
	}
	if (last_stat == 1) {
		outsfmt("- 0x%[x]..0x%[x] \n\r", last_index * 0x1000, 0x2000 * 0x1000);
	}
}

#if (_MCCA & 0xFF00) == 0x8600
Memory mem;
BmMemoman* Memory::pagebmap = NULL;
bool BmMemoman::map_ready = false;
//
byte _BUF_pagebmap[sizeof(BmMemoman)];

#if _MCCA == 0x8632
void* (*uni::_physical_allocate)(stduint size) = 0;
#endif


#endif

// - Memory::clear_bss
#if (_MCCA & 0xFF00) == 0x8600
void Memory::clear_bss() {
	MemSet(&BSS_ENTO, &BSS_ENDO - &BSS_ENTO, 0);
}



#endif






#ifdef _ARC_x86 // x86:

Letvar(Memory::p_basic, byte*, 0x1000); //.. 0x7000
Letvar(Memory::p_ext, byte*, mem_area_exten_beg);
stduint Memory::total_mem = 0;
usize Memory::areax_size = 0;
Slice Memory::avail_slices[4]{ 0 };

usize Memory::evaluate_size() {
	// TEMP check 0x00100000~0x80000000, 2-power size.
	volatile union { word* _p; usize _i; } p;
	p._i = mem_area_exten_beg;
	*p._p = 0x5AA5;
	if (*p._p == 0x5AA5) do {
		p._i = p._i << 1;
		*(p._p - 1) = 0x1227;
		// printlog(_LOG_INFO, "%s at %[32H]", __FUNCIDEN__, p._p - 1);
		if (*(p._p - 1) != 0x1227)
		{
			p._i = p._i >> 1;
			break;
		}
	} while (p._i < 0x80000000);
	areax_size = p._i - mem_area_exten_beg;
	return mem_area_exten_beg + areax_size;
}


// Allocate Physical-Continuous Memory
// unit: x86 Page 0x1000
void* Memory::physical_allocate(usize siz) {
	if (siz & 0xFFF) siz = (siz & ~_IMM(0xFFF)) + 0x1000;
	void* ret = nil;
	if (BmMemoman::map_ready) {
		ret = Memory::allocate(siz);
	}
	else { // not support pg-mapping
		if (p_basic + siz <= (void*)0x6000) {
			void* ret = p_basic;
			p_basic += siz;
			return ret;
		}
		else if (usize(p_ext) + siz <= 0x00100000 + Memory::areax_size) {
			void* ret = p_ext;
			if (BmMemoman::map_ready) {
				Memory::pagebmap->add_range(_IMM(p_ext) >> 12, (_IMM(p_ext) + siz) >> 12, false);
			}
			p_ext += siz;
			return ret;
		}
	}
	return ret;
}

rostr Memory::text_memavail(uni::String& ker_buf) {
	usize mem = Memory::total_mem ? Memory::total_mem : evaluate_size();
	char unit[]{ ' ', 'K', 'M', 'G', 'T' };
	int level = 0;
	// assert mem != 0
	while (!(mem % 1024)) {
		level++;
		mem /= 1024;
	}
	ker_buf.Format(level ? "%d %cB" : "0x%[32H] B", mem, unit[level]);
	return ker_buf.reference();
}


_PACKED(struct) memory_info_entry {
	uint64 addr;
	uint64 len;
	uint32 type;// 1 for avail, 2 for not
};// ARDS
_ESYM_C{
	extern memory_info_entry MemoryListData[20];
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

_ESYM_C Handler_t MemoryList;
_ESYM_C word SW16_FUNCTION;



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
bool Memory::init(stduint eax, byte* ebx) {
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
	BmMemoman::map_ready = true;
	return true;
}

#elif _MCCA == 0x8664
const stduint mapsize = 0x20000;
static byte mapping_4G[mapsize];
static byte bmap_FFFF[2];// 0x10 pages / 8 = 2
static void parse_uefi(const MemoryMap& memory_map) {
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
}
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
	BmMemoman::map_ready = true;
	return true;
}

#endif


// ---- . ----

#if (_MCCA & 0xFF00) == 0x8600
void* Memory::allocate(stduint siz) {
	if (!BmMemoman::map_ready) return nullptr;
	if (siz & 0xFFF) siz = (siz & ~_IMM(0xFFF)) + 0x1000;
	void* ret = nil;
	// find a available page in bitmap
	siz >>= 12;
	if (!Memory::pagebmap->avail_pointer) {
		plogerro("no avail page");
		return nullptr;
	}
	stduint sum_cont = 0;
	stduint sum_beg = Memory::pagebmap->avail_pointer;
	for (stduint p = Memory::pagebmap->avail_pointer; p < 0x2000 * 8; ) {
		if (Memory::pagebmap->bitof(p)) {
			if (!sum_cont) sum_beg = p;
			sum_cont++;
			if (sum_cont >= siz) break;
		}
		else {
			sum_cont = 0;
		}
	}
	if (!(sum_cont >= siz)) {
		plogerro("no avail pages");
		return nullptr;
	}
	ret = (void*)(sum_beg << 12);
	Memory::pagebmap->add_range(sum_beg, sum_beg + siz, false);

	#if _MCCA == 0x8632
	kernel_paging.MapWeak(_IMM(ret), _IMM(ret), siz << 12, true, true);
	#endif

	// printlog(_LOG_INFO, "malloc(0x%[32H], %[x])", ret, siz << 12);
	return ret;
}
bool Memory::deallocate(void* ptr, stduint size _Comment(zero_for_block)) {
	_TODO return false;
}
#endif

// ---- PAGING ----

#ifdef _ARC_x86
Paging kernel_paging;
#endif

// ---- SEGMENT ----

#ifdef _ARC_x86 // x86:

static const uint32 gdt_magic[] = {
	0x00000000, 0x00000000, // null
	0x0000FFFF, 0x00CF9200, // data
	0x0000FFFF, 0x00CF9A00, // code
	0x00000000, 0x00000000, // call
	0x0000FFFF, 0x000F9A00, // code-16
	0x00000000, 0x00008900, // tss
	// 0x0000FFFF, 0x00CFFA00, // code r3
	// 0x0000FFFF, 0x00CFF200, // data r3
};

_ESYM_C void RefreshGDT();
// previous GDT may be broken, omit __asm("sgdt _buf");
void GDT_Init() {
	MemCopyN(mecocoa_global->gdt_ptr, gdt_magic, sizeof(gdt_magic));
	mecocoa_global->gdt_ptr->rout.setModeCall(_IMM(call_gate_entry()) | 0x80000000, SegCo32);// 8*3 Call Gate
	loadGDT(_IMM(mecocoa_global->gdt_ptr), mecocoa_global->gdt_len = sizeof(mec_gdt) - 1);// physical address
	RefreshGDT();
}

word GDT_GetNumber() {
	return (mecocoa_global->gdt_len + 1) / 8;
}

// allocate and apply, return the allocated number
word GDT_Alloc() {
	word ret = mecocoa_global->gdt_len + 1;
	loadGDT(_IMM(mecocoa_global->gdt_ptr), mecocoa_global->gdt_len += 8);
	return ret;
}

// No dynamic core

#elif _MCCA == 0x8664

static descriptor_t GDT64[16];
static unsigned number_of_GDT64 = 6;

static stduint rsp;
void GDT_Init() {
	GDT64[0]._data = nil;
	Descriptor64SetData(&GDT64[SegData >> 3], (_CPU_descriptor_type)0x2, 0);// +RW
	GDT64[1].setRange(0, 0xFFFFF);
	//
	Descriptor64SetCode(&GDT64[SegCo64 >> 3], (_CPU_descriptor_type)10, 0);// ~XR
	//
	// ploginfo("GDT64[1] = 0x%[64H]", GDT64[1]._data);
	// ploginfo("GDT64[5] = 0x%[64H]", GDT64[5]._data);
	loadGDT(_IMM(&GDT64), sizeof(descriptor_t) * number_of_GDT64 - 1);
	setDSAll(SegData);
	setCSSS(SegCo64, SegData);
}


#endif

// ---- STDLIB ----

#if (_MCCA & 0xFF00) == 0x8600
#if _MCCA == 0x8632
// linear allocator
_ESYM_C void* malloc(size_t size) {
	Console.OutFormat("malloc(%[u])\n\r", size);
	return nullptr;
}
_ESYM_C void* calloc(size_t nmemb, size_t size) {
	Console.OutFormat("calloc(%[u])\n\r", nmemb * size);
	return nullptr;
}
#elif _MCCA == 0x8664
void operator delete(void*) {}
void operator delete(void* ptr, unsigned long size) noexcept { _TODO }
void operator delete(void* ptr, unsigned long size, std::align_val_t) noexcept { ::operator delete(ptr, size); }
#endif
_ESYM_C void free(void* ptr) {
	#if _MCCA == 0x8632
	Console.OutFormat("free(0x%[32H])\n\r", ptr);
	#endif
}
_ESYM_C void memf(void* ptr) { free(ptr); }
#endif


