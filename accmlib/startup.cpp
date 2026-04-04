#include "inc/aaaaa.h"
#if (_ACCM & 0xFF00) == 0x8600
_ESYM_C
void _start() {
	//{} bss...
	while (1) exit(main(0,0));//{} main(c,v)
}
#endif
