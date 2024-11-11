// ASCII C/C++ TAB4 LF
// LastCheck: 20240523
// AllAuthor: @dosconio
// ModuTitle: APP Loader
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "appload-riscv64.h"
#include "trap.h"
#include "logging.h"
#include "process-riscv64.h"
#include <c/ustring.h>

static int app_num;
static uint64 *app_info_ptr;
extern char _app_num[], // [number of apps, offsets of apps]
	ekernel[];

// Count finished programs. If all apps exited, shutdown.
int finished()
{
	static int fin = 0;
	if (++fin >= app_num)
		log_panic("all apps over", 0);
	return 0;
}

// Get user progs' infomation through pre-defined symbol in `link_app.S`
void loader_init()
{
	if (_IMM(ekernel) >= BASE_ADDRESS) {
		log_panic("kernel too large...\n", 0);
	}
	app_info_ptr = (uint64 *)_app_num;
	app_num = *app_info_ptr;
	app_info_ptr++;
}

int load_app(int n, uint64* info)
{
	uint64 start = info[n], end = info[n + 1], length = end - start;
	MemSet((void*)BASE_ADDRESS + n * MAX_APP_SIZE, 0, MAX_APP_SIZE);
	MemAbsolute((void*)BASE_ADDRESS + n * MAX_APP_SIZE, (void*)start, length);// load app fixed place
	return length;
}

int run_all_app()
{
	for (int i = 0; i < app_num; ++i) {
		struct proc* p = allocproc();
		struct trapframe* trapframe = p->trapframe;
		load_app(i, app_info_ptr);
		uint64 entry = BASE_ADDRESS + i * MAX_APP_SIZE;
		log_trace("load app %d at %p", i, entry);
		trapframe->epc = entry;
		trapframe->sp = (uint64)p->ustack + USER_STACK_SIZE;
		p->state = RUNNABLE;
		//{TODO} uC LAB1 initialize new fields of proc here
	}
	return 0;
}
