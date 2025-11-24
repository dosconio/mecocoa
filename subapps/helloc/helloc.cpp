//#include <c/stdinc.h>
#include "../../accmlib/inc/aaaaa.h"

int main(int argc, char** argv)
{
	
	while (1) {
		
		unsigned id = systest('T', 'E', 'S');
		unsigned last_sec = 0;
		int stage = 0;
		stduint tmp = 0x1227;
		struct CommMsg msg;
		msg.address = _IMM(&tmp); msg.length = sizeof(tmp);
		sysouts("C");


		// while (1);
		while (1) {
			break;
			sysrest();
			stduint now = syssecond();
			if (now == 2 && stage == 0) {
				//syssend(Task_AppB, &msg);// sync
				stage = 1;
			}
			if (now == 3 && stage == 1) {
				tmp = 0xABCD;
				//syssend(Task_AppB, &msg);// sync
			}
		}
		// {
		// 	struct CommMsg msg0 = { 0 };
		// 	stduint tmp;
		// 	msg0.address = _IMM(&tmp); msg0.length = sizeof(tmp);
		// 	msg0.type = 0;
		// 	syssend(Task_FileSys, &msg);
		// }
		while (1) {
			stduint now = syssecond();
			sysrest();
			if (now >= 3 + last_sec) {
				sysouts("C");
				last_sec = now;
			}
			if (now > 3) {
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
				fd = ok = sysopen("/a.txt");
				sysouts((ok >= 0) ? "Fin check\n\r" : "Fail check\n\r");
				ok = sysclose(fd);
				sysouts(ok == 0 ? "Fin check\n\r" : "Fail check\n\r");
				ok = sysclose(fd);
				sysouts(ok != 0 ? "Fin check\n\r" : "Fail check\n\r");

				break;

			}
		}


		
		while (1) sysrest();;
	}
}

