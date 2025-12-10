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
int sysinnc()// 1
{
	return syscall(INNC, nil, nil, nil);
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
//{unchk}
stduint msgsend(stduint to_whom, const void* msgaddr, stduint bytlen, stduint type)
{
	struct CommMsg msg;
	msg.address = _IMM(msgaddr);
	msg.length = bytlen;
	msg.type = type;
	syscall(COMM, 0b01, to_whom, _IMM(&msg));
}
//{unchk}
stduint msgrecv(stduint fo_whom, void* msgaddr, stduint bytlen, stduint* type, stduint* src)
{
	struct CommMsg msg = { nil };
	msg.address = _IMM(msgaddr);
	msg.length = bytlen;
	if (type) msg.type = *type;
	if (src) msg.src = *src;
	stduint ret = syscall(COMM, 0b10, fo_whom, &msg);
	if (type) *type = msg.type;
	if (src) *src = msg.src;
	return ret;
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

static int execv(const char* path, char* argv[]);
static int execl(const char* path, const char* arg, ...)
{
	va_list parg = (va_list)(&arg);
	char **p = (char**)parg;
	return execv(path, p);
}
#define PROC_ORIGIN_STACK 128
static int execv(const char* path, char* argv[])
{
	stduint args[4];
	stdsint ret;
	char** p = argv;
	char arg_stack[PROC_ORIGIN_STACK];
	int stack_len = 0;

	while(*p++) {
		if (stack_len + 2 * sizeof(char*) < PROC_ORIGIN_STACK); else {
			plogerro("panic %s:%u", __FUNCIDEN__, __LINE__);
		}
		stack_len += sizeof(char*);
	}

	*((int*)(&arg_stack[stack_len])) = 0;
	stack_len += sizeof(char*);

	char ** q = (char**)arg_stack;
	for (p = argv; *p != 0; p++) {
		*q++ = &arg_stack[stack_len];
		if (stack_len + StrLength(*p) + 1 < PROC_ORIGIN_STACK); else {
			plogerro("panic %s:%u", __FUNCIDEN__, __LINE__);
		}
		StrCopy(&arg_stack[stack_len], *p);
		stack_len += StrLength(*p);
		arg_stack[stack_len] = 0;
		stack_len++;
	}

	args[1] = path;
	args[2] = arg_stack;
	args[3] = stack_len;
	msgsend(Task_TaskMan, args, sizeof(args), 4);
	msgrecv(Task_TaskMan, &ret, sizeof(ret), nil, nil);
	return ret;
}

