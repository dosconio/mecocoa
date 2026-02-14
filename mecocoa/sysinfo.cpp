// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <c/cpuid.h>
#include <c/consio.h>
#include <c/driver/mouse.h>
#include <c/driver/keyboard.h>
#include "../include/console.hpp"



#if ((_MCCA & 0xFF00) == 0x8600)

char _buf[64];
String ker_buf(_buf, byteof(_buf));
extern uint32 _start_eax, _start_ebx;

int* kernel_fail(void* _serious, ...) {
	Letvar(serious, loglevel_t, _IMM(_serious));
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
