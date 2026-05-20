#include "../accmlib/inc/aaaaa.h"

int main(int argc, char** argv) {
	const char* cmd = "hlt";
	if (argc >= 2) {
		cmd = argv[1];
	}
	if (StrCompare(cmd, "hlt") == 0) {
		asm("hlt");
	}
	else if (StrCompare(cmd, "page") == 0) {
		int* p = reinterpret_cast<int*>(0x100);
		*p = 42;
	}
	else if (StrCompare(cmd, "zero") == 0) {
		volatile int z = 0;
		outsfmt("100/%d = %d\n", z, 100 / z);
	}
}