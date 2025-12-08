#include "inc/aaaaa.h"
volatile static unsigned* const callid = (volatile unsigned*)0x0000500;// 0x80000500;

void sysouts(const char* str)// 0
{
	while (*str)
	{
		syscall(OUTC, (stduint)*str, nil, nil);
		str++;
	}
	// [Other Method]
	//    fd = sysopen("/dev_tty0");
	//    syswrite(fd, (void*)"Hello CCCCC!\n\r", 15);
	//    sysclose(fd);
}
void sysinnc()// 1
{
	_TODO INNC;
}
void exit(int code)// 2
{
	syscall(EXIT, code, nil, nil);
}

stduint syssecond()// 3
{
	return syscall(TIME, nil, nil, nil);
}

void sysrest()// 4
{
	syscall(REST, nil, nil, nil);
}

void syscomm(int send_recv, stduint obj, struct CommMsg* msg)
{
	syscall(COMM, send_recv ? 0b01 : 0b10, obj, _IMM(msg));
}



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

stduint systest(unsigned t, unsigned e, unsigned s)// FF
{
	syscall(0xFF, t, e, s);
}

int sys_createfil(rostr fullpath)
{
	stduint r = syscall(OPEN, _IMM(fullpath), 0b01, nil);// open -> desc
	return *(int*)&r;
}

int sysopen(rostr fullpath) {
	stduint r = syscall(OPEN, _IMM(fullpath), 0b10, nil);// open -> desc
	return *(int*)&r;
}
int sysclose(int fd) {
	return syscall(CLOS, fd, nil, nil);
}
stduint sysread(int fd, void* buf, stduint size)
{
	return syscall(READ, fd, _IMM(buf), size);
}
stduint syswrite(int fd, void* buf, stduint size) {
	return syscall(WRIT, fd, _IMM(buf), size);
}

int sys_removefil(rostr fullpath)
{
	return syscall(DELF, _IMM(fullpath), nil, nil);
}
int _Comment(pid) wait(int* status)
{
	return syscall(WAIT, _IMM(status), nil, nil);
}
int fork() {
    return syscall(FORK, nil, nil, nil);
}

