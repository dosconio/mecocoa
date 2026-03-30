
#include "../../accmlib/inc/aaaaa.h"

#define DELAY 4000

__attribute__((optimize("O0")))
static void task_delay(volatile int count)
{
	count *= 50000;
	while (count--);
}

auto main(int argc, char** argv) -> int
{
	unsigned int hid = -1;
	sysouts("Task 0: Created!\n");
	int ret = -1;
	ret = get_core_id(&hid);
	if (!ret) {
		sysouts("system call returned!, hart id is %d\n");
	} else {
		sysouts("gethid() failed, return: %d\n");
	}
	while (1) {
		sysouts("Task 0: Running...\n");
		task_delay(DELAY);
	}
}

