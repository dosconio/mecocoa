// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _HIS_TIME_H
#include <cpp/unisym>
use crate uni;
#include <c/cpuid.h>
#include <c/consio.h>
#include <c/driver/mouse.h>
#include <c/driver/keyboard.h>
#include "../include/console.hpp"

#if _MCCA == 0x8632
#include "../include/atx-x86-flap32.hpp"
#elif _MCCA == 0x8664
#include "../include/atx-x64.hpp"
#include "../prehost/atx-x64-uefi64/atx-x64-uefi64.loader/loader-graph.h"
#endif

#if ((_MCCA & 0xFF00) == 0x8600)

char _buf[64];
String ker_buf(_buf, byteof(_buf));
extern uint32 _start_eax, _start_ebx;

int* kernel_fail(loglevel_t serious) {
	if (serious == _LOG_FATAL) {
		outsfmt("\n\rKernel panic!\n\r");
		__asm("cli; hlt");
	}
	return nullptr;
}

rostr text_brand() {
	CpuBrand(ker_buf.reflect());
	ker_buf.reflect()[_CPU_BRAND_SIZE] = 0;
	const char* ret = ker_buf.reference();
	while (*ret == ' ') ret++;
	return ret;
}

#endif
