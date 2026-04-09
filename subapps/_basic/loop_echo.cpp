
#include "../../accmlib/inc/aaaaa.h"

auto main(int argc, char** argv) -> int
{
	Console.OutFormat("Task %s: Created!\n", __FILE__);
	while (true) {
		int ch = sysinnc();
		if (ch != -1) Console.OutChar(ch);
	}
}

#if (_MCCA & 0xFF00) == 0x1000
extern "C" void *memset(void *str, int c, size_t n) { return MemSet(str, c, n); }


#endif
