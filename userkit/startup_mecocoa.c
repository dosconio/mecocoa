#include "inc/ukitinc.h"

extern int main();

void entry() {
	sysinit();
	while (1) sysquit(main());
}
