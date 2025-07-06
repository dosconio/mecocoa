//#include <c/stdinc.h>
//#include "../../include/console.h"
#include "../../accmlib/inc/aaaaa.h"
volatile static unsigned* const callid = (volatile unsigned*)0x0000500;// 0x80000500;

int main()
{
	while (1) {
		//sysdelay(2 * 1000000);// 2s
		//for0(i, 0x8000) for0(j, 0x1000) ;
		unsigned last_sec = callid[0x18 / 4];
		unsigned id = systest('T', 'E', 'S');
		//sysouts("B");

		if (id == 1) {
			sysouts("subappb systest OK!\n\r");
		}
		else {
			sysouts("subappb systest failed!\n\r");
		}

		stduint esp; __asm__("mov %%esp, %0" : "=r"(esp));
		while (1) {
			volatile int a = 0x45;
			if (callid[0x18 / 4] != last_sec) {
				last_sec = callid[0x18 / 4];
				sysouts("B");
			}
			stduint newesp; __asm__("mov %%esp, %0" : "=r"(newesp));
			if (newesp != esp) {
				sysouts("ESP: %u\n\r");
				esp = newesp;
			}
			if (a != 0x45) {
				sysouts("A != 0x45\n\r");
				a = 0x45;
			}
		}

	}
}

