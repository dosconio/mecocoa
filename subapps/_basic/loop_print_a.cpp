
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
	stduint hid = -1;
	sysouts("Task 0: Created!\n");
	int ret = -1;
	ret = get_core_id(&hid);
	Console.OutFormat("gethid() return: %d\n", ret);
	while (1) {
		sysouts("Task 0: Running...\n");
		task_delay(DELAY);
	}
}
