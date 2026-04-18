#include "inc/aaaaa.h"
#if (_ACCM & 0xFF00) == 0x8600
_ESYM_C
void _start(int argc, char** argv) {
	exit(main(argc, argv));
}
#endif
