// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _DEBUG
#include <c/consio.h>
#include "../include/atx-x86-flap32.hpp"


use crate uni;
#ifdef _ARC_x86 // x86:

Letvar(Memory::p_basic, byte*, 0x1000); //.. 0x7000
Letvar(Memory::p_ext, byte*, mem_area_exten_beg);
usize Memory::areax_size = 0;

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

// Page size at least
void* Memory::physical_allocate(usize siz) {
	if (siz & 0xFFF) siz = (siz & ~_IMM(0xFFF)) + 0x1000;
	if (p_basic + siz <= (void*)0x6000) {
		void* ret = p_basic;
		p_basic += siz;
		// if (opt_info) printlog(_LOG_INFO, "malloc_low(0x%[32H], %[u])", ret, siz);
		//{} Pag-map for linear_allocate
		return ret;
	}
	else if (usize(p_ext) + siz <= 0x00100000 + Memory::areax_size) {
		void* ret = p_ext;
		p_ext += siz;
		// if (opt_info) printlog(_LOG_INFO, "malloc_hig(0x%[32H], %[u])", ret, siz);
		//{} Pag-map for linear_allocate
		//{} Add Kernel and Each App's PDT
		return ret;
	}
	//{} try swap
	return nullptr;
}

rostr Memory::text_memavail(uni::String& ker_buf) {
	usize mem = evaluate_size();
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

// ---- Paging ----

Paging kernel_paging;
extern stduint tmp;

_TEMP void page_init() {
	kernel_paging.Reset();// should take 0x1000
	kernel_paging.MapWeak(0x00000000, 0x00000000, 0x00400000, true, _Comment(R0 TOD) true);//{TODO} false
	kernel_paging.MapWeak(0xFFFFF000, _IMM(kernel_paging.page_directory), 0x00001000, false, _Comment(R0) false);// make loop PDT and do not unisym's
	// for(i, 0x400) pdt[0x3FF][...] Page Tables
	kernel_paging.MapWeak(0x80000000, 0x00000000, 0x00400000, true, _Comment(R0) false);
	//
	tmp = _IMM(kernel_paging.page_directory);
	__asm("movl tmp, %eax\n");
	__asm("movl %eax, %cr3\n");
	__asm("movl %cr0, %eax\n");
	__asm("or   $0x80000000, %eax\n");// enable paging
	__asm("movl %eax, %cr0\n");
	rostr test_page = (rostr)"\xFF\x70[Mecocoa]\xFF\x02 Paging Test OK!\xFF\x07" + 0x80000000;
	if (opt_test) Console.OutFormat("%s\n\r", test_page);
}

// ----

extern "C" {
	// linear allocator
	stduint strlen(const char* ptr) { return StrLength(ptr); }
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
	void* memset(void* str, int c, size_t n) {
		return MemSet(str, c, n);
	}
}

#endif

/*
delete (void*) new int;
*/

// ---- x86 ----

#ifdef _ARC_x86 // x86:

extern "C" void new_lgdt();

static const uint32 gdt_magic[] = {
	0x00000000, 0x00000000, // null
	0x0000FFFF, 0x00CF9300, // data
	0x0000FFFF, 0x00CF9B00, // code
	0x00000000, 0x00000000, // call
	0x0000FFFF, 0x000F9A00, // code-16
	0x00000000, 0x00008900, // tss
	// 0x0000FFFF, 0x00CFFA00, // code r3
	// 0x0000FFFF, 0x00CFF200, // data r3
};

//{TODO} make into a class
// 8*0 Null   , 8*1 Code R0, 8*2 Data R0, 8*3 Gate R3
// 8*4 Co16   , 8*5 TSS    , 8*6 Codu R3, 8*7 Datu R3 (-u User)
// Other TSS ... pass 8*(6,7)
// previous GDT may be broken, omit __asm("sgdt _buf");
void GDT_Init() {
	MemCopyN(mecocoa_global->gdt_ptr, gdt_magic, sizeof(gdt_magic));
	mecocoa_global->gdt_ptr->rout.setModeCall(_IMM(call_gate_entry()) | 0x80000000, SegCode);// 8*3 Call Gate
	tmp48_le = { mecocoa_global->gdt_len = sizeof(mec_gdt) - 1 , _IMM(mecocoa_global->gdt_ptr) };// physical address
	__asm("lgdt tmp48_le");
	tmp48_be = { _IMM(&new_lgdt) , SegCode };
	__asm("ljmp *tmp48_be");
	__asm("new_lgdt: mov $8, %eax;");
	__asm("mov %eax, %ds; mov %eax, %es; mov %eax, %fs; mov %eax, %gs; mov %eax, %ss;");
}

word GDT_GetNumber() {
	return (mecocoa_global->gdt_len + 1) / 8;
}

// allocate and apply, return the allocated number
word GDT_Alloc() {
	word ret = mecocoa_global->gdt_len + 1;
	tmp48_le = { mecocoa_global->gdt_len += 8 , _IMM(mecocoa_global->gdt_ptr) };
	__asm("lgdt tmp48_le");
	return ret;
}

#endif
