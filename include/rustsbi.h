// ASCII C++ TAB4 CRLF
// LastCheck: 20240505
// AllAuthor: @dosconio
// ModuTitle: RustSBI Interface
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#ifndef _DEP_RustSBI
#define _DEP_RustSBI

typedef enum SBI_Identifiers
{
	SBI_SET_TIMER = 0,
	SBI_CONSOLE_PUTCHAR = 1,
	SBI_CONSOLE_GETCHAR = 2,
	SBI_CLEAR_IPI = 3,
	SBI_SEND_IPI = 4,
	SBI_REMOTE_FENCE_I = 5,
	SBI_REMOTE_SFENCE_VMA = 6,
	SBI_REMOTE_SFENCE_VMA_ASID = 7,
	SBI_SHUTDOWN = 8,
} SBI_Identifiers;

void sysoutc(int);
int sysgetc();
void shutdown();
void set_timer(uint64 stime);

#endif

