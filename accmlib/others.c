#include "inc/aaaaa.h"
unsigned* const callid = (unsigned*)0x0000500;// 0x80000500;
void sysouts(const char* str) {
	*callid = 0x00;
	while (*str)
	{
		callid[1] = *str;
		syscall();
		str++;
	}
}
void sysquit(int code) {
	*callid = 0x02;
	syscall();
}
void sysdelay(unsigned dword) {
	unsigned last_tick = callid[0x1C / 4];
	unsigned last_sec = callid[0x18 / 4];
	while (callid[0x1C / 4] - last_tick < dword) {
		if (dword >= 1000000 && callid[0x18 / 4] > last_sec) {
			last_sec++;
			dword -= 1000000;
		}
	}
}
