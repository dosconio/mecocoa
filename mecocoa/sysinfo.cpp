// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// #pragma GCC optimize("O2")
#include "../include/mecocoa.hpp"

#include <c/cpuid.h>
#include <c/driver/mouse.h>
#include <c/driver/keyboard.h>
#include <c/driver/RealtimeClock.h>
#include "../include/console.hpp"

String dump_availmem();
void mecfetch() {
	#if ((_MCCA & 0xFF00) == 0x8600)
	// const rostr blue = ento_gui ? "\xFE\xF8\xC8\x58" : "\xFF\x30";
	// const rostr pink = ento_gui ? "\xFE\xB8\xA8\xF8" : "\xFF\x50";
	// const rostr white = ento_gui ? "\xFE\xFF\xFF\xFF" : "\xFF\x70";
	const rostr blue = "\033[48;2;88;200;248m";
	const rostr pink = "\033[48;2;248;168;184m";
	const rostr white = "\033[48;2;255;255;255m";
	const unsigned attrl = ento_gui ? 4 : 2;
	const unsigned width = ento_gui ? 48 : 16;
	const unsigned height = ento_gui ? 3 : 1;

	#if _GUI_LOGO
	Console.OutFormat(blue, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\033[0m\n\r%s", blue); }
	Console.OutFormat(pink, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\033[0m\n\r%s", pink); }
	Console.OutFormat(white, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\033[0m\n\r%s", white); }
	Console.OutFormat(pink, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\033[0m\n\r%s", pink); }
	Console.OutFormat(blue, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\033[0m\n\r%s", blue); }
	Console.OutFormat("\033[0m");// Console.out("\xFF\xFF", 2);
	#endif

	Console.OutFormat("CPU Brand: %s\n\r", text_brand());

	tm datime = {};
	#if _MCCA == 0x8632 //((_MCCA & 0xFF00) == 0x8600)
	CMOS_Readtime(&datime);
	#endif
	Console.OutFormat("Date Time: %d/%d/%d %d:%d:%d\n\r",
		datime.tm_year, datime.tm_mon, datime.tm_mday,
		datime.tm_hour, datime.tm_min, datime.tm_sec
	);
	#endif

	Console.OutFormat("Avail Mem: %s\n\r", dump_availmem().reference());
}// like neofetch

#if ((_MCCA & 0xFF00) == 0x8600)

char _buf[64];
String ker_buf(_buf, byteof(_buf));
extern uint32 _start_eax, _start_ebx;

void kernel_fail(void* _serious, ...) {
	Letvar(serious, loglevel_t, _IMM(_serious));
	if (serious == _LOG_FATAL) {
		outsfmt("\n\rKernel panic!\n\r");
		__asm("cli; hlt");
	}
}

rostr text_brand() {
	CpuBrand(ker_buf.reflect());
	ker_buf.reflect()[_CPU_BRAND_SIZE] = 0;
	const char* ret = ker_buf.reference();
	while (*ret == ' ') ret++;
	return ret;
}

#endif

String dump_availmem() {
	double mem = Memory::total_memsize;
	char unit[]{ ' ', 'K', 'M', 'G', 'T' };
	int level = 0;
	// assert mem != 0
	while (mem > 1024) {
		level++;
		mem /= 1024;
	}
	mem *= 100;
	mem = (int)mem;
	mem /= 100;
	String ret;
	if (level) ret.Format("%lf %cB", mem, unit[level]);
	else ret.Format("0x%[x] B", Memory::total_memsize);
	return ret;
}
