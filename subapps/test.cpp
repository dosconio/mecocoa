//#include <c/stdinc.h>
#include "../accmlib/inc/aaaaa.h"
#include "c/consio.h"

using namespace uni;

// mnt33 fat16
// mnt34 fat32

int main(int argc, char** argv)
{
	unsigned id = systest('T', 'E', 'S');// TEST
	outsfmt("C(%d)\n\r", id);// OUTC
	for0(i, 3) {
		outsfmt("C(%d)\n\r", syssecond());// TIME
		sysrest(0 _Comment(s), 1);// REST
	}// delay 3 s


	#if 0
	
	// rostr filename = "/mnt33/owo.txt";
	rostr filename = "/md0/owo.txt";
	
	int fd = sysopen(filename);
	if (fd < 0) {
		fd = sys_createfil(filename);
	}

	if (fd >= 0) {
		outsfmt("Open success! FD=%d\n\r", fd);
		const char* msg = "Hello from Mecocoa VFS!\n";
		syswrite(fd, (void*)msg, 24);
		sysclose(fd);

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

	sys_removefil(filename);
	#endif

	#if 1
	stduint buf[4] = {21};
	CommMsg msg;
	msg.data.address = _IMM(buf);
	msg.data.length = sizeof(buf);
	msg.type = 4;// ConsoleMsg::FNEW;
	syscomm(1, Task_Console, &msg);
	#endif



	return 0;
}

