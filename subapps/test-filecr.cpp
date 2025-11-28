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
		rostr files[] = { "/file1", "/file2", "/file3" };// dev_tty0
		rostr filer[] = { "/file1", "/file2", "/file0", "/dev_tty0" };
		int fd;
		int ok;
		while (1) {
			sysrest();
			//
			for0a(i, files) {
				fd = sys_createfil(files[i]);
				sysouts(fd >= 0 ? "Fin  create " : "Fail create ");
				sysouts(files[i]);
				sysouts("\n\r");
				sysclose(fd);
			}
			for0a(i, filer) {
				ok = !sys_removefil(filer[i]);
				sysouts(ok ? "Fin  remove " : "Fail remove ");
				sysouts(filer[i]);
				sysouts("\n\r");
			}

			//
			break;
		}
		while (1) sysrest();
	}
}

