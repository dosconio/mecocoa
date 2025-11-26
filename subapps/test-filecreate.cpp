//#include <c/stdinc.h>
#include "../../accmlib/inc/aaaaa.h"

int main(int argc, char** argv)
{
	while (1) {
		unsigned id = systest('T', 'E', 'S');
		while (1) {
			stduint now = syssecond();
			sysrest();
			if (1) {
				int ok;
				//{TODO} make test-programs...
				/*
				sysouts("Try Opening file\n\r");
				int ok = sys_createfil("/a.txt");
				sysouts(ok >= 0 ? "Fin open\n\r" : "Fail open\n\r");
				sysouts("Try Opening file Again\n\r");
				ok = sys_createfil("/a.txt");
				sysouts(!(ok >= 0) ? "Fin check\n\r" : "Fail check\n\r");
				*/
				/* Open and Close:
				* o(1) o(0) c(1) o(1) c(1) c(0) after create (dup-check)
				* o(1) c(1) c(0) after create (no-dup-check)
				*/
				int fd;
				/*
				fd = ok = sysopen("/a.txt");
				sysouts((ok >= 0) ? "Fin check\n\r" : "Fail check\n\r");
				ok = sysopen("/a.txt");
				sysouts(!(ok >= 0) ? "Fin check\n\r" : "Fail check\n\r");
				ok = sysclose(fd);
				sysouts(ok == 0 ? "Fin check\n\r" : "Fail check\n\r");
				fd = ok = sysopen("/a.txt");
				sysouts((ok >= 0) ? "Fin check\n\r" : "Fail check\n\r");
				ok = sysclose(fd);
				sysouts(ok == 0 ? "Fin check\n\r" : "Fail check\n\r");
				ok = sysclose(fd);
				sysouts(ok != 0 ? "Fin check\n\r" : "Fail check\n\r");
				*/
				/*
				fd = ok = sysopen("/a.txt");
				sysouts((ok >= 0) ? "Fin check\n\r" : "Fail check\n\r");
				ok = sysclose(fd);
				sysouts(ok == 0 ? "Fin check\n\r" : "Fail check\n\r");
				ok = sysclose(fd);
				sysouts(ok != 0 ? "Fin check\n\r" : "Fail check\n\r");
				*/
				break;

			}
		}
		while (1) sysrest();;
	}
}

