//#include <c/stdinc.h>
#include "../../accmlib/inc/aaaaa.h"

int main(int argc, char** argv)
{
	while (1) {
		unsigned id = systest('T', 'E', 'S');
		unsigned last_sec = 0;
		int stage = 0;
		volatile stduint tmp;
		struct CommMsg msg;
		msg.address = _IMM(&tmp); msg.length = sizeof(tmp);
		sysouts("B");
		if (id == Task_AppB) {
			sysouts("subappb systest OK!\n\r");
		}
		else {
			sysouts("subappb systest failed!\n\r");
		}

		while (1) {
			sysrest();
			stduint now = syssecond();
			if (now == 1 && stage == 0) {
				//sysrecv(Task_AppC, &msg);// sync
				//sysouts(tmp == _IMM(0x1227) ? "Recv[1] Perfect!\n\r" : "Recv[1] Failed!\n\r");
				stage = 1;
			}
			if (now == 4 && stage == 1) {
				//sysrecv(Task_AppC, &msg);// sync
				//sysouts(tmp == _IMM(0xABCD) ? "Recv[2] Perfect!\n\r" : "Recv[2] Failed!\n\r");
				stage = 2;
			}
			/*
			if (now) {
				msg.src = msg.type = 0;
				syscomm(0, 0, &msg);// sync
				if (msg.src == 112 && msg.type == 1) {
					sysouts("(RTC!)");
				}
			}*/
			if (now >= 3 + last_sec) {

				sysouts("B");
				last_sec = now;
			}
			
		}



		

	}
}

