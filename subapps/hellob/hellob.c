//#include <c/stdinc.h>
#include "../../accmlib/inc/aaaaa.h"
volatile static unsigned* const callid = (volatile unsigned*)0x0000500;// 0x80000500;

int main()
{
	while (1) {
		int a = 0x45;
		//sysdelay(2 * 1000000);// 2s
		//for0(i, 0x8000) for0(j, 0x1000) ;

		unsigned id = systest('T', 'E', 'S');
		unsigned last_sec = 0;
		sysouts("B");
		if (id == 2) {
			sysouts("subappb systest OK!\n\r");
		}
		else {
			sysouts("subappb systest failed!\n\r");
		}

		stduint esp; __asm__("mov %%esp, %0" : "=r"(esp));
		while (1) {
			sysrest();
			stduint now = syssecond();
			if (now >= 3 + last_sec) {
				sysouts("B");
				last_sec = now;
			}
			//stduint newesp; __asm__("mov %%esp, %0" : "=r"(newesp));
			//if (newesp != esp) {
			//	sysouts("ESP: %u\n\r");
			//	esp = newesp;
			//}
			//if (a != 0x45) {
			//	sysouts("A != 0x45\n\r");
			//	a = 0x45;
			//}
		}



		

	}
}

