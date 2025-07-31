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
			// sysrest();
			stduint now = syssecond();
			if (now == 2 && stage == 0) {
				syscomm(1, 2, &msg);// sync
				stage = 1;
			}
			if (now == 3 && stage == 1) {
				tmp = 0xABCD;
				syscomm(1, 2, &msg);// sync
				stage = 2;
			}
			if (now >= 3 + last_sec) {
				sysouts("C");
				last_sec = now;
			}
		}



		

	}
}

