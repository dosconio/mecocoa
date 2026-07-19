#include "aaaaa.h"
#include "cpp/queue"
#include <sys/wait.h>

int main(int argc, char** argv)
{
	int s;
	while (true) {
		int child = wait(&s);
		if (child > 0) {
			ploginfo("[Appinit] %d exited with %d.", child, s);
			continue;
		}
		s = 0;
		sysrest(0, 1);//{}== yield 1s
	}
}
