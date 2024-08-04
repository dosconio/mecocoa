#include <c/stdinc.h>
#include "../../include/console.h"
#include "../../userkit/inc/ukitinc.h"

int main()
{
	sysinit();
	sysouts("(B)");
	sysquit();
	while (1) {
		sysouts("B");
		sysdelay(250);
	}
}

