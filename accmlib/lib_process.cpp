#include "inc/aaaaa.h"
#include <unistd.h>
#include <sys/wait.h>

void _exit(int code)
{
	syscall(syscall_t::EXIT, code, nil, nil);// 戰至最後一刻！
	*((byte*)nullptr) = nil;// 自刎歸天⚔️（介錯）
	while (1);// 歸天失敗
}

pid_t fork() {
	return syscall(syscall_t::FORK, nil, nil, nil);
}

pid_t getpid(void) {
	return syscall(syscall_t::TEST, 'T', 'E', 'S');
}

//

pid_t _Comment(pid) wait(int* status)
{
	return syscall(syscall_t::WAIT, _IMM(status), nil, nil);
}

