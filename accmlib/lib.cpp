#include "aaaaa.h"
#include "c/ustring.h"
#include "c/consio.h"

volatile static unsigned* const callid = (volatile unsigned*)0x0000500;// 0x80000500;

// POSIX Based

#if (_ACCM & 0xFF00) == 0x1000
_ESYM_C stduint syscall_bridge(stduint a0, stduint a1, stduint a2, stduint a3, stduint a4, stduint a5, stduint a6, stduint a7);
stduint syscall(syscall_t callid, stduint p1, stduint p2, stduint p3) {
	return syscall_bridge(p1, p2, p3, 0, 0, 0, 0, _IMM(callid));
}
#endif

void outtxt(const char* str, stduint len) {
	syscall(syscall_t::WRIT, 1, (stduint)str, len);// stdout
	// syscall(syscall_t::OUTC, (stduint)str, len, nil);// bindout
}
void sysouts(const char* str)// 0
{
	auto len = StrLength(str);
	if (!len) return;
	syscall(syscall_t::WRIT, 1, (stduint)str, len);
	// [Other Method]
	//    syswrite(STDOUT, (void*)"Hello CCCCC!\n\r", 15);
}

int sysinnc()// 1
{
	return syscall(syscall_t::INNC, 1, nil, nil);
}

_ESYM_C
stduint syssecond()// 3
{
	return syscall(syscall_t::TIME, 0, nil, nil);
}

void sysrest(stduint unit, stduint num)// 4
{
	syscall(syscall_t::REST, unit, num, nil);
}

void syscomm(int send_recv, stduint obj, struct CommMsg* msg)
{
	syscall(syscall_t::COMM, send_recv ? 0b01 : 0b10, obj, _IMM(msg));
}
//{unchk}
stduint msgsend(stduint to_whom, const void* msgaddr, stduint bytlen, stduint type)
{
	struct CommMsg msg;
	msg.data.address = _IMM(msgaddr);
	msg.data.length = bytlen;
	msg.type = type;
	return syscall(syscall_t::COMM, 0b01, to_whom, _IMM(&msg));
}
//{unchk}
stduint msgrecv(stduint fo_whom, void* msgaddr, stduint bytlen, stduint* type, stduint* src)
{
	struct CommMsg msg = { nil };
	msg.data.address = _IMM(msgaddr);
	msg.data.length = bytlen;
	if (type) msg.type = *type;
	if (src) msg.src = *src;
	stduint ret = syscall(syscall_t::COMM, 0b10, fo_whom, _IMM(&msg));
	if (type) *type = msg.type;
	if (src) *src = msg.src;
	return ret;
}


//{} with retval
void sysdelay(unsigned dword) {
	#if 0
	unsigned last_tick = callid[0x1C / 4];
	unsigned last_sec = callid[0x18 / 4];
	while (callid[0x1C / 4] - last_tick + (callid[0x18 / 4] - last_sec) * 1000000 <= dword) {
		if (dword >= 1000000 && callid[0x18 / 4] > last_sec) {
			last_sec++;
			dword -= 1000000;
		}
	}
	#endif
}


int sys_createfil(rostr fullpath)
{
	stduint r = syscall(syscall_t::OPEN, _IMM(fullpath), O_CREAT | O_RDWR, nil);// open -> desc
	return *(int*)&r;
}
/*
int sys_mkdir(rostr path) {
    return syscall(syscall_t::OPEN, _IMM(path), 0b1001, nil);
}
*/

int sysopen(rostr fullpath) {
	stduint r = syscall(syscall_t::OPEN, _IMM(fullpath), 0b10, nil);// open -> desc
	return *(int*)&r;
}


int sys_removefil(rostr fullpath)
{
	return syscall(syscall_t::DELF, _IMM(fullpath), nil, nil);
}

stduint get_core_id(stduint* ptr_hid) {
	return syscall(syscall_t::GET_CORE_ID, _IMM(ptr_hid), nil, nil);
}

extern "C" int dup2(int oldfd, int newfd) {
	return (int)syscall(syscall_t::DUP2, (stduint)oldfd, (stduint)newfd, nil);
}

extern "C" int pipe(int pipefd[2]) {
	return (int)syscall(syscall_t::PIPE, (stduint)pipefd, nil, nil);
}

// ----
#define PROC_ORIGIN_STACK 128

int spawnve(const char* path, char* const argv[], char* const envp[])
{
	return syscall(syscall_t::EXEC, _IMM(path), _IMM(argv), _IMM(envp));
}

int spawnl(const char* path, const char* arg, ...)
{
	char* argv[_TEMP 16]; // Increased safe size
	int argc = 0;
	para_list args;
	
	argv[argc++] = (char*)arg;
	para_ento(args, arg);
	while (argc < numsof(argv) - 1) {
		char* next_arg = para_next(args, char*);
		argv[argc++] = next_arg;
		if (next_arg == nullptr || next_arg == 0) break;
	}
	para_endo(args);
	argv[argc] = nullptr; 
	return spawnve(path, argv, nullptr);
}


int execl(const char* path, const char* arg, ...)
{
	char* argv[_TEMP 16];
	int argc = 0;
	para_list args;
	
	argv[argc++] = (char*)arg;
	para_ento(args, arg);
	while (argc < numsof(argv) - 1) {
		char* next_arg = para_next(args, char*);
		argv[argc++] = next_arg;
		if (next_arg == nullptr || next_arg == 0) break;
	}
	para_endo(args);
	argv[argc] = nullptr; 
	return execv(path, argv);
}

extern "C" void __sigrestorer();

extern "C" int sigaction(int sig, const struct _POSIX_sigaction* act, struct _POSIX_sigaction* oact) {
	struct _POSIX_sigaction temp_act;
	if (act) {
		temp_act = *act;
		if (temp_act.sa_restorer == nullptr) {
			temp_act.sa_restorer = __sigrestorer;
		}
		act = &temp_act;
	}
	return (int)syscall(syscall_t::SIGA, sig, _IMM(act), _IMM(oact));
}

extern "C" int kill(pid_t pid, int sig) {
	return (int)syscall(syscall_t::KILL, pid, sig, 0);
}

extern "C" int sigemptyset(_POSIX_sigset_t* set) {
	if (!set) return -1;
	_sigset_raw(set) = 0;
	return 0;
}

extern "C" int sigfillset(_POSIX_sigset_t* set) {
	if (!set) return -1;
	_sigset_raw(set) = ~0ULL;
	return 0;
}

extern "C" int sigaddset(_POSIX_sigset_t* set, int signo) {
	if (!set || signo <= 0 || signo >= _NSIG) return -1;
	_sigset_raw(set) |= (1ULL << (signo - 1));
	return 0;
}

extern "C" int sigdelset(_POSIX_sigset_t* set, int signo) {
	if (!set || signo <= 0 || signo >= _NSIG) return -1;
	_sigset_raw(set) &= ~(1ULL << (signo - 1));
	return 0;
}

extern "C" int sigismember(const _POSIX_sigset_t* set, int signo) {
	if (!set || signo <= 0 || signo >= _NSIG) return 0;
	return (_sigset_raw(set) & (1ULL << (signo - 1))) != 0;
}

extern "C" __sighandler_t signal(int sig, __sighandler_t handler) {
	struct _POSIX_sigaction act, oact;
	act.sa_handler = handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_restorer = nullptr;
	if (sigaction(sig, &act, &oact) < 0) {
		return SIG_ERR;
	}
	return oact.sa_handler;
}

extern "C" char** environ;
char** environ = nullptr;

extern "C" void _init_environ(int argc, char** argv, char** envp) {
	environ = envp;
}

// extern "C" _WEAK void _preprocess() {}



extern "C" char* getenv(const char* name) {
	if (!name || !environ) {
		return nullptr;
	}
	stduint len = StrLength(name);
	for (int i = 0; environ[i] != nullptr; i++) {
		if (StrCompareN(environ[i], name, len) == 0 && environ[i][len] == '=') {
			return environ[i] + len + 1;
		}
	}
	return nullptr;
}

extern "C" int setenv(const char* name, const char* value, int overwrite) {
	if (!name || StrLength(name) == 0 || StrIndexChar(name, '=') != nullptr) {
		return -1;
	}
	char* current = getenv(name);
	if (current) {
		if (!overwrite) {
			return 0;
		}
		// Overwrite the existing value
		stduint name_len = StrLength(name);
		stduint val_len = StrLength(value);
		char* new_str = (char*)malloc(name_len + 1 + val_len + 1);
		if (!new_str) {
			return -1;
		}
		StrCopy(new_str, name);
		StrAppend(new_str, "=");
		StrAppend(new_str, value);
		
		for (int i = 0; environ[i] != nullptr; i++) {
			if (StrCompareN(environ[i], name, name_len) == 0 && environ[i][name_len] == '=') {
				environ[i] = new_str;
				return 0;
			}
		}
	} else {
		stduint count = 0;
		if (environ) {
			while (environ[count] != nullptr) {
				count++;
			}
		}
		// Allocate a new array to hold the existing variables plus the new one and the NULL terminator
		char** new_env = (char**)malloc((count + 2) * sizeof(char*));
		if (!new_env) {
			return -1;
		}
		for (stduint i = 0; i < count; i++) {
			new_env[i] = environ[i];
		}
		
		stduint name_len = StrLength(name);
		stduint val_len = StrLength(value);
		char* new_str = (char*)malloc(name_len + 1 + val_len + 1);
		if (!new_str) {
			free(new_env);
			return -1;
		}
		StrCopy(new_str, name);
		StrAppend(new_str, "=");
		StrAppend(new_str, value);
		
		new_env[count] = new_str;
		new_env[count + 1] = nullptr;
		environ = new_env;
	}
	return 0;
}

extern "C" char* strchr(const char* s, int c) {
	return (char*)StrIndexChar(s, c);
}

extern "C" int atoi(const char* nptr) {
	return (int)atoins(nptr);
}



#include <stdarg.h>

extern "C" int printf(const char* fmt, ...) {
	para_list args;
	para_ento(args, fmt);
	int ret = outsfmtlst(fmt, args);
	para_endo(args);
	return ret;
}

// TODO:
extern "C" void __stack_chk_fail_local() { loop; }
extern "C" void atexit() {  }
extern "C" {
	int errno = 0;
	void* _Unwind_Resume = 0;
	void* __gcc_personality_v0 = 0;
}
