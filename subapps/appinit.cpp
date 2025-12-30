#include "../accmlib/inc/aaaaa.h"
#include "cpp/queue"

#define EXIT_CODE 0x123
char inbuf[128];
char* argv[25];

int run(char* cmd)
{
	sysouts("torun: ");
	sysouts(inbuf);
	sysouts("\n\r");
	// //
	char* p = cmd;
	char* s;
	int word = 0, argc = 0;
	char ch;
	do {
		ch = *p++;
		if (*p != ' ' && *p && !word) {
			s = p;
			word = 1;
		}
		if ((*p == ' ' || !*p) && word) {
			word = 0;
			argv[argc++] = s;
			*p = 0;
		}
		if (argc >= numsof(argv)) break;
	} while (ch);
	argv[argc] = 0;
	//
	//int fd = open(argv[0], O_RDWR);
	//if (fd == -1) { return -1; }
	//	close(fd); fd = 0;
	//	if (fork()) {
	//		int s;
	//		wait(&s);
	//	}
	//	else execv(argv[0], argv);
	//
	return _TEMP 0;
}

int main(int argc, char** argv)
{
	int fd_inn = sysopen("/dev_tty0");// should-be 0
	int fd_out = sysopen("/dev_tty0");// should-be 1
	int pid = fork();
	if (pid) {
		sysouts("[Appinit] There is the parent.\n\r");
		int s;
		while (true) {
			int child = wait(&s);
			if (s == EXIT_CODE) {
				sysouts("[Appinit] The child exited with EXIT_CODE.\n\r");
				s = nil;
			}
			sysrest();
		}
	}
	else { // here is the shell (primitive COTLAB)
		uni::QueueLimited queue((uni::Slice) { _IMM(inbuf), sizeof(inbuf) });
		char outbuf[2] = { 0 };
		sysouts("[Appinit] Init SHell started.\n\r");
		// if (pid = fork()); else sysouts("[appinit] There is the child's child.\n\r");
		while (true) {
			if ((outbuf[0] = sysinnc()) > 0) {
				if (outbuf[0] == '\n') {
					sysouts("\n\r");// run new proc
					queue.out(&outbuf[1], 1);
					run(inbuf);
					queue.clear();
				}
				else if (outbuf[0] == '\b') {
					sysouts("\b \b");
					if (!queue.is_empty()) {
						queue.p--;
					}
				}
				else {
					sysouts(outbuf);
					queue.out(outbuf, 1);
				}
			}
		}
		// exit(EXIT_CODE);
	}
	while (1) sysrest();//{TODO} exit()
}

