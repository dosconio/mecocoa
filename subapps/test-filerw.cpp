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
			if (now >= 6) break;
		}
		while (1) {
			
			sysrest();
			//
			int fd;
			int ok;
			const char filename[] = "/a2.txt";
			const char writebuf[] = "phina";
			char read_buf[10];
			//// create
			//ok = sys_createfil(filename);
			//sysouts("[1] ");
			//sysouts(ok >= 0 ? "Fin create\n\r" : "Fail create\n\r");
			// open
			fd = sysopen(filename);
			sysouts("[2] ");
			sysouts(fd >= 0 ? "Fin open\n\r" : "Fail open\n\r");
			// write
			ok = syswrite(fd, (void*)writebuf, 6);
			sysouts("[3] ");
			sysouts(ok == 6 ? "Fin write\n\r" : "Fail write\n\r");
			// close
			ok = sysclose(fd);
			sysouts("[4] ");
			sysouts(ok == 0 ? "Fin close\n\r" : "Fail close\n\r");
			// open
			fd = sysopen(filename);
			sysouts("[5] ");
			sysouts(fd >= 0 ? "Fin open\n\r" : "Fail open\n\r");
			// read
			ok = sysread(fd, (void*)read_buf, 6);
			sysouts("[6] ");
			if (read_buf[0] == 'p' && read_buf[1] == 'h' && read_buf[2] == 'i' && read_buf[3] == 'n' && read_buf[4] == 'a' && read_buf[5] == '\0') {
				sysouts("Fin read\n\r");
			} else {
				sysouts("Fail read\n\r");
			}
			// close
			ok = sysclose(fd);
			sysouts("[7] ");
			sysouts(ok == 0 ? "Fin close\n\r" : "Fail close\n\r");
			//
			break;
		}
		while (1) sysrest();
	}
}

