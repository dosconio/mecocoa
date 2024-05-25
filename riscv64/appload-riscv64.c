// ASCII C/C++ TAB4 LF
// LastCheck: 20240523
// AllAuthor: @dosconio
// ModuTitle: APP Loader
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "appload-riscv64.h"
#include "trap.h"
#include "logging.h"
#include <c/ustring.h>

static int app_cur, app_num;
static uint64 *app_info_ptr;
extern char _app_num[], // [number of apps, offsets of apps]
	userret[], boot_stack_top[], ekernel[];

void loader_init()
{
	if ((uint64)ekernel >= BASE_ADDRESS) {
		log_panic("kernel too large...\n");
	}
	app_info_ptr = (uint64 *)_app_num;
	app_cur = -1;
	app_num = *app_info_ptr;
}

_ALIGN(4096) char user_stack[USER_STACK_SIZE];
_ALIGN(4096) char trap_page[TRAP_PAGE_SIZE];

int load_app(uint64 *info)
{
	uint64 start = info[0], end = info[1], length = end - start;
	MemSet((void *)BASE_ADDRESS, 0, MAX_APP_SIZE);
	MemAbsolute((void *)BASE_ADDRESS, (void *)start, length);// load app fixed place
	return length;
}

int run_next_app()
{
	Letvar(trapframe, struct trapframe *, trap_page);
	app_cur++;
	app_info_ptr++;
	if (app_cur >= app_num)
		return -1;
	uint64 length = load_app(app_info_ptr);
	log_debug("load and run app %d, range = [%p, %p)", app_cur, *app_info_ptr, *app_info_ptr + length);
	MemSet(trapframe, 0, 4096);
	trapframe->epc = BASE_ADDRESS;
	trapframe->sp = (uint64)user_stack + USER_STACK_SIZE;
	ReturnTrapFromU(trapframe);
	return 0;
}
