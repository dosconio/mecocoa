// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#include <c/stdinc.h>
#include <c/consio.h>
#include <c/cpuid.h>

#define _sign_entry() extern "C" void start()

use crate uni;

// area-basic: 0x7E00 .. 0x80000
// area-exten: 0x00100000 .. 0x00100000 + areax_size
_TEMP void* malloc_temp(usize siz) {
	static Letvar(p_basic, byte*, 0x7E00);
	static Letvar(p_ext, byte*, 0x00100000);
	usize areax_size = 0;
	if (p_basic + siz < (void*)0x80000) {
		void* ret = p_basic;
		p_basic += siz;
		return ret;
	}
	else if (usize(p_ext) + siz < 0x00100000 + areax_size) {
		void* ret = p_ext;
		p_ext += siz;
		return ret;
	}
	return nullptr;
}

extern byte* BSS_ENTO, * BSS_ENDO;
void clear_bss() {
    for (byte* p = BSS_ENTO; p < BSS_ENDO; p++)
        *p = 0;
}


char buf[257];

//{WHY} any class with `virtual` method will make you cry, like Console.FormatShow

_sign_entry() {
	__asm("movl $0x1E00, %esp");// mov esp, 0x1E00; set stack
	//{TODO} clear_bss();
	//{TODO} show booter, auto boot from flo/hd
	//{TODO} Console entity
	CpuBrand(buf); buf[_CPU_BRAND_SIZE] = 0;
	outsfmt("CPU Brand: %s\n\r", buf);
	__asm("cli");
	__asm("hlt");
	loop;
}
