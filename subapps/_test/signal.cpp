//#include <c/stdinc.h>
#include "aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <c/ISO_IEC_STD/signal.h>

using namespace uni;

static void sigint_handler(int signo) {
	outsfmt("\n\r[TestApp] Received SIGINT (%d)! Interrupted successfully!\n\r", signo);
}

static void sigfpe_handler(int signo) {
	outsfmt("\n\r[TestApp] Received SIGFPE (%d) from divide-by-zero!\n\r", signo);
	_exit(1);
}

int main(int argc, char** argv)
{
	struct _POSIX_sigaction act;
	act.sa_handler = sigint_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_restorer = nullptr;
	sigaction(SIGINT, &act, nullptr);

	struct _POSIX_sigaction act_fpe;
	act_fpe.sa_handler = sigfpe_handler;
	sigemptyset(&act_fpe.sa_mask);
	act_fpe.sa_flags = 0;
	act_fpe.sa_restorer = nullptr;
	sigaction(SIGFPE, &act_fpe, nullptr);

	// Trigger exception test if required:
	if (0) {
		volatile int a = 1;
		volatile int b = 0;
		volatile int c = a / b;
		(void)c;
	}

	unsigned id = getpid();// TEST
	outsfmt("C(%d)\n\r", id);// OUTC

	sysrest(0 _Comment(s), 5);
}

