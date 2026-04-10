
#include "../../accmlib/inc/aaaaa.h"

auto main(int argc, char** argv) -> int
{
	Console.OutFormat("Task %s: Created!\n", __FILE__);
	while (true) {
		int ch = sysinnc();
		if (ch != -1) Console.OutChar(ch);
	}
}
