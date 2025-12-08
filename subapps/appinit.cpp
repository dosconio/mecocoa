#include "../accmlib/inc/aaaaa.h"

#define EXIT_CODE 0x123

int main(int argc, char** argv)
{
	int fd_inn = sysopen("/dev_tty0");// should-be 0
	int fd_out = sysopen("/dev_tty0");// should-be 1
	int pid = fork();
	if (pid) {
		sysouts("[appinit] There is the parent.\n\r");
		int s;
		while (true) {
			int child = wait(&s);
			if (s == EXIT_CODE) {
				sysouts("[appinit] The child exited with EXIT_CODE.\n\r");
				s = nil;
			}
			sysrest();
		}
	}
	else {
		sysouts("[appinit] There is the child.\n\r");
		// if (pid = fork()); else sysouts("[appinit] There is the child's child.\n\r");

		exit(EXIT_CODE);
		
	}
	while (1) sysrest();//{TODO} exit()
}

