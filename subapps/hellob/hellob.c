//#include <c/stdinc.h>
#include "../../accmlib/inc/aaaaa.h"

int main(int argc, char** argv)
{
	while (1) {
		unsigned id = systest('T', 'E', 'S');
		unsigned last_sec = 0;
		int stage = 0;
		stduint tmp;
		struct CommMsg msg;
		msg.address = _IMM(&tmp); msg.length = sizeof(tmp);
		sysouts("B");
		if (id == 2) {
			sysouts("subappb systest OK!\n\r");
		}
		else {
			sysouts("subappb systest failed!\n\r");
		}

		while (1) {
			sysrest();
			stduint now = syssecond();
			/*
			if (now == 1 && stage == 0) {
				syscomm(0, 4, &msg);// sync

				if (tmp == 0x1227) {
					sysouts("Recv[1] Perfect!\n\r");
				}
				else {
					sysouts("Recv[1] Failed!\n\r");
				}
				stage = 1;
			}
			if (now == 4 && stage == 1) {
				syscomm(0, 4, &msg);// sync

				if (tmp == 0xABCD) {
					sysouts("Recv[2] Perfect!\n\r");
				}
				else {
					sysouts("Recv[2] Failed!\n\r");
				}
				stage = 2;
			}
			*/
			if (now) {
				msg.src = msg.type = 0;
				syscomm(0, 0, &msg);// sync
				if (msg.src == 112 && msg.type == 1) {
					sysouts("(RTC!)");
				}
			}
			if (now >= 3 + last_sec) {

				sysouts("B");
				last_sec = now;
			}
		}



		

	}
}

