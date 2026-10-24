#include "aaaaa.h"
#include "cpp/queue"
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

static bool file_exists(const char* path)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) return false;
	close(fd);
	return true;
}

int main(int argc, char** argv)
{
	static constexpr const char* kCotPath = "/md0/cot";
	static constexpr const char* kInitScriptPath = "/md0/init.cot";
	if (file_exists(kCotPath)) {
		if (file_exists(kInitScriptPath)) {
			spawnl(kCotPath, "cot", "-f", kInitScriptPath, nullptr);
		}
		else {
			// spawnl(kCotPath, "cot", nullptr);// no run
		}
	}

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
