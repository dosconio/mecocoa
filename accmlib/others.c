#include "inc/aaaaa.h"
volatile static unsigned* const callid = (volatile unsigned*)0x0000500;// 0x80000500;
void sysouts(const char* str) {
	while (*str)
	{
		syscall(0x00, (stduint)*str, nil, nil);
		str++;
	}
}
void sysquit(int code) {
	syscall(0x02, nil, nil, nil);
}

void sysinnc() {}

//{} with retval
void sysdelay(unsigned dword) {
	unsigned last_tick = callid[0x1C / 4];
	unsigned last_sec = callid[0x18 / 4];
	while (callid[0x1C / 4] - last_tick + (callid[0x18 / 4] - last_sec) * 1000000 <= dword) {
		if (dword >= 1000000 && callid[0x18 / 4] > last_sec) {
			last_sec++;
			dword -= 1000000;
		}
	}
}

stduint systest(unsigned t, unsigned e, unsigned s) {
	syscall(0xFF, t, e, s);
}
