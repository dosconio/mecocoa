//#include <c/stdinc.h>
#include "../../accmlib/inc/aaaaa.h"

int main(int argc, char** argv)
{
	while (1) {
		unsigned id = systest('T', 'E', 'S');
		// sysouts("C");
		while (1) {// wait for fileman ready
			sysrest();
			stduint now = syssecond();
			if (now >= 4) break;
		}

		int fd;
		int ok;
		while (1) {
			sysrest();
			//
			fd = sysopen("/dev_tty0");
			syswrite(fd, (void*)"Hello CCCCC!\n\r", 15);
			//
			break;
		}
		while (1) sysrest();
	}
}

