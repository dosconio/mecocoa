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
				sysouts("Try Opening file\n\r");
				stduint ok = syscall(0x6, _IMM("/a.txt"), 0x1234, nil);// open -> desc
				sysouts(ok ? "\n\rfin open\n\r" : "\n\rfail open\n\r");
				break;

			}
		}


		
		while (1) sysrest();;
	}
}

