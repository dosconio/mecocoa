#include "aaaaa.h"
#include "cpp/queue"
#include <sys/wait.h>

int main(int argc, char** argv)
{
	int ribbon = spawnl("/md0/ribbon", "ribbon", nullptr);
	if (ribbon < 0) {
		outsfmt("[Appinit] Failed to start ribbon.\n\r");
	}

	int s;
	while (true) {
		int child = wait(&s);
		if (child > 0) {
			outsfmt("[Appinit] %d exited with %d.\n\r", child, s);
			continue;
		}
		s = 0;
		sysrest(0, 1);//{}== yield 1s
	}
}
