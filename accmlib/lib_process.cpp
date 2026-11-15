#include "aaaaa.h"
#include <unistd.h>
#include <sys/wait.h>

namespace {
	typedef void (*ExitFunction)(void);
	typedef void (*CxaExitFunction)(void*);

	struct ExitCallback {
		ExitCallback* next;
		union {
			ExitFunction plain;
			CxaExitFunction cxa;
		} function;
		void* argument;
		void* dso_handle;
		bool plain_function;
	};

	ExitCallback* exit_callbacks = nullptr;

	ExitCallback* AllocateExitCallback() {
		return (ExitCallback*)malloc(sizeof(ExitCallback));
	}

	int RegisterCxaExitCallback(CxaExitFunction function, void* argument,
		void* dso_handle) {
		if (!function) return -1;
		ExitCallback* callback = AllocateExitCallback();
		if (!callback) return -1;
		callback->function.cxa = function;
		callback->argument = argument;
		callback->dso_handle = dso_handle;
		callback->plain_function = false;
		callback->next = exit_callbacks;
		exit_callbacks = callback;
		return 0;
	}

	int RegisterPlainExitCallback(ExitFunction function) {
		if (!function) return -1;
		ExitCallback* callback = AllocateExitCallback();
		if (!callback) return -1;
		callback->function.plain = function;
		callback->argument = nullptr;
		callback->dso_handle = nullptr;
		callback->plain_function = true;
		callback->next = exit_callbacks;
		exit_callbacks = callback;
		return 0;
	}
}

// Itanium C++ ABI 3.3.5: register a destructor for one DSO object.
extern "C" int __cxa_atexit(CxaExitFunction function, void* argument,
	void* dso_handle) {
	return RegisterCxaExitCallback(function, argument, dso_handle);
}

// Itanium C++ ABI 3.3.5: finalize matching callbacks in reverse registration order.
extern "C" void __cxa_finalize(void* dso_handle) {
	while (true) {
		ExitCallback** link = &exit_callbacks;
		while (*link && dso_handle && (*link)->dso_handle != dso_handle)
			link = &(*link)->next;
		if (!*link) return;

		ExitCallback* callback = *link;
		*link = callback->next;
		const bool plain_function = callback->plain_function;
		if (plain_function) {
			const ExitFunction function = callback->function.plain;
			free(callback);
			function();
		}
		else {
			const CxaExitFunction function = callback->function.cxa;
			void* const argument = callback->argument;
			free(callback);
			function(argument);
		}
	}
}

extern "C" int atexit(ExitFunction function) {
	return RegisterPlainExitCallback(function);
}

extern "C" void _exit(int code)
{
	syscall(syscall_t::EXIT, code, nil, nil);// 戰至最後一刻！
	*((volatile byte*)nullptr) = nil;// 自刎歸天⚔️（介錯） __builtin_trap()
	while (1);// 歸天失敗
}

extern "C" void exit(int code)
{
	__cxa_finalize(nullptr);
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

