//#include <c/stdinc.h>
#include "../../accmlib/inc/aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <c/ISO_IEC_STD/signal.h>

using namespace uni;

// mnt33 fat16
// mnt34 fat32

int main(int argc, char** argv)
{

	#if __BITS__ == 32
	rostr filename = "/mnt33/owo.txt";
	#else
	rostr filename = "/md0/owo.txt";
	#endif

	int fd = sysopen(filename);
	if (fd < 0) {
		fd = sys_createfil(filename);
	}

	if (fd >= 0) {
		outsfmt("Open success! FD=%d\n\r", fd);
		const char* msg = "Hello from Mecocoa VFS!\n";
		write(fd, (void*)msg, 24);
		close(fd);

		fd = sysopen(filename);
		if (fd >= 0) {
			char buf[32] = {0};
			read(fd, buf, 24);
			outsfmt("Read result: %s\n\r", buf);
			close(fd);
		}
	} else {
		outsfmt("Open failed with code %d\n\r", fd);
	}

	sys_removefil(filename);


}

