#include <c/stdinc.h>
#include "../../include/console.h"
#include "../../userkit/inc/ukitinc.h"

int main()
{
	sysinit();
	sysouts("(C)");
	sysquit();
	while (1) {
		sysouts("C");
		sysdelay(2000);
	}
}

