//#include <c/stdinc.h>
#include "../../accmlib/inc/aaaaa.h"
#include "c/consio.h"

using namespace uni;

int main(int argc, char** argv)
{
	unsigned id = systest('T', 'E', 'S');
	outsfmt("C(%d)\n\r", 1);
/*
	rostr filename = "/mnt33/testvfs.txt";
	
	int fd = sysopen(filename);
	if (fd < 0) {
		fd = sys_createfil(filename);
	}

	if (fd >= 0) {
		outsfmt("Open success! FD=%d\n\r", fd);
		const char* msg = "Hello from Mecocoa VFS!\n";
		syswrite(fd, (void*)msg, 24);
		sysclose(fd);
		
		outsfmt("Writing success. Reading back...\n\r");
		fd = sysopen(filename);
		if (fd >= 0) {
			char buf[32] = {0};
			sysread(fd, buf, 24);
			outsfmt("Read result: %s\n\r", buf);
			sysclose(fd);
		}
	} else {
		outsfmt("Open failed with code %d\n\r", fd);
	}
	
*/
	return 0;
}

