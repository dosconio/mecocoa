#include "../accmlib/inc/aaaaa.h"

int main(int argc, char** argv)
{
	int fd_inn = sysopen("/dev_tty0");// should-be 0
	int fd_out = sysopen("/dev_tty0");// should-be 1
	int pid = fork();
	if (pid) {
		sysouts("[appinit] There is the parent.\n\r");
	}
	else {
		sysouts("[appinit] There is the child.\n\r");
		pid = fork();
		if (pid) {
		}
		else {
			sysouts("[appinit] There is the child's child.\n\r");
		}
	}
	while (1) sysrest();//{TODO} exit()
}

