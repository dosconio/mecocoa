// ASCII GAS/RISCV64 TAB4 LF
// Attribute: 
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include <c/stdinc.h>
//
#include <cpp/Device/UART>

using namespace uni;

void _global_object_init() {
	new (&UART0) UART_t(0, 115200);
	
}

extern "C"
void _entry()
{
	_global_object_init();
	//[!] cannot use FLOATING operation (why)
	UART0.setMode(115200);
	#if _MCCA == 0x1064
	UART0.OutFormat("%s\n", "Hello Mcca-RV64~ =OwO=");
	#else
	UART0.OutFormat("%s\n", "Hello Mcca-RV32~ =OwO=");
	#endif
	

	while (1);
}

extern "C" { void* __dso_handle = 0; }
extern "C" { void __cxa_atexit(void) {} }
extern "C" { void __gxx_personality_v0(void) {} }
extern "C" { void __stack_chk_fail(void) {} }

extern "C" void __cxa_pure_virtual() {
	while (1);
}
extern "C" void *memset(void *str, int c, size_t n) { return MemSet(str, c, n); }
