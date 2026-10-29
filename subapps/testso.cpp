#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

static volatile int dynamic_program_data = 0x1234;

int main(int argc, char* argv[]) {
	if (argc == 5 && !strcmp(argv[1], "--stage15-hold")) {
		const int ready_fd = atoi(argv[2]);
		const int release_fd = atoi(argv[3]);
		const int marker = atoi(argv[4]);
		if (ready_fd < 0 || release_fd < 0 || marker < 1 || marker > 2)
			return 2;
		errno = 0x500 + marker;
		const char ready = 'R';
		if (write(ready_fd, &ready, 1) != 1) return 3;
		close(ready_fd);
		char release = 0;
		if (read(release_fd, &release, 1) != 1 || release != 'G')
			return 4;
		close(release_fd);
		if (errno != 0x500 + marker) return 5;
		printf("Stage 15 libc worker %d released.\n", marker);
		return 0;
	}
	if (argc == 5 && !strcmp(argv[1], "--stage14-hold")) {
		const int ready_fd = atoi(argv[2]);
		const int release_fd = atoi(argv[3]);
		const int marker = atoi(argv[4]);
		if (ready_fd < 0 || release_fd < 0 || marker < 1 || marker > 2)
			return 2;
		dynamic_program_data = marker;
		const char ready = 'R';
		if (write(ready_fd, &ready, 1) != 1) return 3;
		close(ready_fd);
		char release = 0;
		if (read(release_fd, &release, 1) != 1 || release != 'G')
			return 4;
		close(release_fd);
		if (dynamic_program_data != marker) return 5;
		printf("Stage 14 worker %d released.\n", marker);
		return 0;
	}
    if (argc <= 1) {
        printf("\n");
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) {
            printf(" ");
        }
    }
    printf("\n");
    return 0;
}
