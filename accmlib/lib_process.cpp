#include "aaaaa.h"
#include <unistd.h>
#include <sys/wait.h>

extern "C" void _exit(int code)
{
	syscall(syscall_t::EXIT, code, nil, nil);// 戰至最後一刻！
	*((volatile byte*)nullptr) = nil;// 自刎歸天⚔️（介錯） __builtin_trap()
	while (1);// 歸天失敗
}

extern "C" void exit(int code)
{
	_exit(code);
}

pid_t fork() {
	return syscall(syscall_t::FORK, nil, nil, nil);
}

pid_t getpid(void) {
	return syscall(syscall_t::TEST, 'T', 'E', 'S');
}

//

pid_t wait(int* status)
{
	return syscall(syscall_t::WAIT, 0, _IMM(status), nil);
}

int waitid(idtype_t idtype, id_t id, siginfo_t* infop, int options)
{
	stduint target_pid = (idtype == P_PID) ? id : 0;
	int status;
	pid_t pid = syscall(syscall_t::WAIT, target_pid, _IMM(&status), nil);
	if (pid > 0 && infop) {
		infop->si_pid = pid;
		infop->si_status = status;
		infop->si_code = WEXITED;
		return 0;
	}
	return (pid == 0 && (options & WNOHANG)) ? 0 : -1;
}

int execve(const char* path, char* const argv[], char* const envp[]) {
	return syscall(syscall_t::EXET, _IMM(path), _IMM(argv), _IMM(envp));
}

extern "C" char** environ;
int execv(const char* path, char* const argv[]) {
	return execve(path, argv, environ);
}

