// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST

#include <c/consio.h>


use crate uni;
#ifdef _ARC_x86 // x86:
#include "../include/atx-x86-flap32.hpp"
#include "../prehost/atx-x86-flap32/multiboot2.h"
// for x86, one slice should begin with 0x100000 (above F000:FFFF)
// for x86, only consider single paging method

Letvar(Memory::p_basic, byte*, 0x1000); //.. 0x7000
Letvar(Memory::p_ext, byte*, mem_area_exten_beg);
stduint Memory::total_mem = 0;
usize Memory::areax_size = 0;
Slice Memory::avail_slices[4]{ 0 };
usize Memory::avail_pointer = 0xFFFFFFFF;
Bitmap* Memory::pagebmap = NULL;

byte _BUF_pagebmap[sizeof(Bitmap)];

// need not Bitmap
namespace uni {
	void* (*_physical_allocate)(stduint size) = 0;
}

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

bool map_ready = false;
// Allocate Physical-Continuous Memory
// unit: x86 Page 0x1000
static void add_range(stduint head_pos, stduint last_pos, bool what);
void* Memory::physical_allocate(usize siz) {
	if (siz & 0xFFF) siz = (siz & ~_IMM(0xFFF)) + 0x1000;
	void* ret = nil;
	if (map_ready) {
		// find a available page in bitmap
		siz >>= 12;
		if (!avail_pointer) {
			plogerro("no avail page");
			return nullptr;
		}
		stduint sum_cont = 0;
		stduint sum_beg = avail_pointer;
		for (stduint p = avail_pointer; p < 0x2000 * 8; ) {
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
		add_range(sum_beg, sum_beg + siz, false);
		kernel_paging.MapWeak(_IMM(ret), _IMM(ret), siz << 12, true, true);
		// printlog(_LOG_INFO, "malloc(0x%[32H], %[x])", ret, siz << 12);
	}
	else { // not support pg-mapping
		if (p_basic + siz <= (void*)0x6000) {
			void* ret = p_basic;
			p_basic += siz;
			return ret;
		}
		else if (usize(p_ext) + siz <= 0x00100000 + Memory::areax_size) {
			void* ret = p_ext;
			if (map_ready) {
				add_range(_IMM(p_ext) >> 12, (_IMM(p_ext) + siz) >> 12, false);
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

//{unchk} {TODO}
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
_ESYM_C Handler_t FILE_ENDO;

// e.g. add_range(0x1, 0x2) ofr 0x1000 ~ 0x1FFF
static void update_avail_pointer(stduint head_pos, stduint last_pos, bool what) {
	// ploginfo("update_avail_pointer(0x%[32H], 0x%[32H], %d)", head_pos, last_pos, what);
	if (what) {
		MIN(Memory::avail_pointer, head_pos);
	}
	else if (Memory::avail_pointer == head_pos) {
		Memory::avail_pointer = last_pos;
		while (!Memory::pagebmap->bitof(Memory::avail_pointer)) {
			Memory::avail_pointer++;
			if (Memory::avail_pointer >= (Memory::pagebmap->size * _BYTE_BITS_)) {
				plogerro("Memory is all used");
				return;
			}
		}
	}
}
static void add_range(stduint head_pos, stduint last_pos, bool what) {
	// if (!map_ready) ploginfo("add_range(0x%[32H], 0x%[32H])", head_pos, last_pos);
	if (head_pos >= last_pos || head_pos >= 0x2000 * 8 || last_pos >= 0x2000 * 8) return;
	stduint times = last_pos - head_pos;
	stduint curr_pos = head_pos;
	while (times && (curr_pos & 0b111)) {
		Memory::pagebmap->setof(curr_pos, what);
		curr_pos++;
		times--;
	}
	while (times >= _BYTE_BITS_) {
		cast<byte*>(Memory::pagebmap->offs)[curr_pos / _BYTE_BITS_] = what ? 0xFF : 0x00;
		curr_pos += _BYTE_BITS_;
		times -= _BYTE_BITS_;
	}
	while (times) {
		Memory::pagebmap->setof(curr_pos, what);
		curr_pos++;
		times--;
	}
	update_avail_pointer(head_pos, last_pos, what);
}
bool Memory::init(stduint eax, byte* ebx) {
	// make available memory into a group of slices
	switch (eax)
	{
	case 'ANIF':// from loader
		SW16_FUNCTION = _IMM(&MemoryList);
		__asm("call SwitchReal16");
		parse_norm(_IMM(MemoryListData));
		break;
	case MULTIBOOT2_BOOTLOADER_MAGIC:
		parse_grub(_IMM(ebx));
		break;
	default:
		return false;
	}
	// reduce the area that the kernel used
	stduint kernel_end = _IMM(&FILE_ENDO);
	kernel_end = vaultAlignHexpow(0x1000, kernel_end);
	Slice& slice = Memory::avail_slices[0];

	// ceil base and floor length
	slice.length = floorAlign(0x1000, slice.length);
	while ((slice.address || slice.length) && slice.address < kernel_end) {
		// ---------------  section
		// ....---------     kernel
		if (slice.address + slice.length <= kernel_end) {
			for0(i, numsof(Memory::avail_slices) - 1) {
				Memory::avail_slices[i] = Memory::avail_slices[i + 1];
			}
			Memory::avail_slices[numsof(Memory::avail_slices) - 1].address = 0;
			Memory::avail_slices[numsof(Memory::avail_slices) - 1].length = 0;
		}
		else {
			stduint diff = slice.address + slice.length - kernel_end;
			slice.address = kernel_end;
			slice.length = diff;
		}
	}
	Slice empty_slice = { 0,0 };
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
	// create and init mbitmap
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
	Memory::pagebmap = new (_BUF_pagebmap) Bitmap(mapaddr, mapsize);
	// fill the mbitmap
	// * here
	// - physical_allocate (which be with pagemap)
	// - free [TODO]
	// cons_init();
	for0(i, numsof(Memory::avail_slices)) {
		stduint slice_endpoint = Memory::avail_slices[i].address + Memory::avail_slices[i].length;
		if (!Memory::avail_slices[i].address && !Memory::avail_slices[i].length) break;
		if (Memory::avail_slices[i].address == 0x100000) {
			if (_IMM(Memory::p_ext) >= slice_endpoint) {
				return false;
			}
			add_range(_IMM(Memory::p_ext) >> 12, slice_endpoint >> 12, true);
		}
		else {
			add_range(Memory::avail_slices[i].address >> 12, slice_endpoint >> 12, true);
		}
	}
	add_range(0x80, 0xA0, false);// Extended BIOS Data Area
	map_ready = true;
	return true;
}

void dump_avail_memory()
{
	bool last_stat = false;
	stduint last_index = 0;
	outsfmt("[Memoman] dump avail memory:\n\r");
	for0(i, 0x2000 * 8) {
		bool b = Memory::pagebmap->bitof(i);
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


// ---- Paging ----

Paging kernel_paging;

// ----

extern "C" {
	// linear allocator
	void* malloc(size_t size) {
		Console.OutFormat("malloc(%[u])\n\r", size);
		return nullptr;
	}
	void* calloc(size_t nmemb, size_t size) {
		Console.OutFormat("calloc(%[u])\n\r", nmemb * size);
		return nullptr;
	}
	void free(void* ptr) {
		Console.OutFormat("free(0x%[32H])\n\r", ptr);
	}
	void memf(void* ptr) { free(ptr); }
}

#endif

// ---- GDT ----

#ifdef _ARC_x86 // x86:

extern "C" void new_lgdt();

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


// previous GDT may be broken, omit __asm("sgdt _buf");
void GDT_Init() {
	MemCopyN(mecocoa_global->gdt_ptr, gdt_magic, sizeof(gdt_magic));
	mecocoa_global->gdt_ptr->rout.setModeCall(_IMM(call_gate_entry()) | 0x80000000, SegCo32);// 8*3 Call Gate
	loadGDT(_IMM(mecocoa_global->gdt_ptr), mecocoa_global->gdt_len = sizeof(mec_gdt) - 1);// physical address
	jmpFar(_IMM(new_lgdt), SegCo32);
	__asm("new_lgdt: mov $8, %eax;");
	__asm("mov %eax, %ds; mov %eax, %es; mov %eax, %fs; mov %eax, %gs; mov %eax, %ss;");
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

#elif defined(_MCCA) && _MCCA == 0x8664

static descriptor_t GDT64[16];
static unsigned number_of_GDT64 = 6;

void GDT_Init() {
	GDT64[0]._data = nil;
	Descriptor64SetData(&GDT64[1], (_CPU_descriptor_type)0x2, 0);// +RW
	GDT64[1].setRange(0, 0xFFFFF);
	//
	Descriptor64SetCode(&GDT64[5], (_CPU_descriptor_type)10, 0);// ~XR
	//
	// ploginfo("GDT64[1] = 0x%[64H]", GDT64[1]._data);
	// ploginfo("GDT64[5] = 0x%[64H]", GDT64[5]._data);
	loadGDT(_IMM(&GDT64), sizeof(GDT64) * number_of_GDT64 - 1);
}


#endif
