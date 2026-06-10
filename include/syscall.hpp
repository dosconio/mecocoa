#ifndef SYSCALL_HPP_
#define SYSCALL_HPP_

enum
	#ifdef _INC_CPP
	class
	#endif
	syscall_t
	#ifdef _INC_CPP
	: stduint
	#endif
{
	OUTC = 0x00, // outstr (chr/str, len)->0  |.x86 x64 rv
	INNC = 0x01, // innstr (blocked)->ASCII>0 | x86 x64 rv
	EXIT = 0x02, // exit   (code)             | x86 x64 rv
	TIME = 0x03, // getsec (0sec/1ms)->second | x86 x64
	REST = 0x04, // halt   (unit, time)       | x86 x64
	COMM = 0x05, // syncom (mod, obj, &msg)   | x86 x64 rv
	// [Fileops]
	OPEN = 0x06, // open   (path,flags)>=0    | x86 x64
	CLOS = 0x07, // close  (fd)->0            | x86 x64
	READ = 0x08, // read   (fd, adr, len)->len| x86 x64
	WRIT = 0x09, // write  (fd, adr, len)->len| x86 x64
	DELF = 0x0A, // remove (pathname)->?      | x86 x64
	PORP = 0x0B, // proper (fd, &proper)->0   | x86
	ENUM = 0x0C, // enumer (fd,&kde,cnt)->cnt | x86
	// [Process]
	WAIT = 0x0D, // wait   (pid, &status)->pid| x86 x64    | pid 0 for any
	FORK = 0x0E, // fork   ()->pid            | x86 x64
	TMSG = 0x0F, // trymsg ()->(msg_unsovled) | x86 x64 rv
	EXEC = 0x10, // spawn  (path,argv,envp)->0| x86 x64
	EXET = 0x11, // exec   (path,argv,envp)->0| x86 x64
	PFUN = 0x12, // procfn () |(TODO) | priority ...
	// [Signals]
	SIGA = 0x13, // sigaction (sig,&new,&old) | x86
	KILL = 0x14, // kill      (pid, sig, tid) | x86
	SIGR = 0x15, // sigreturn ()              | -
	// [Fileops Extend]
	SETD = 0x16, // chdir  (path)->status     | x86
	GETD = 0x17, // getcwd (buf, size)->len   | x86
	MMAP = 0x18, // mmap   (size,fl,fd)->addr | x86
	UMAP = 0x19, // munmap (addr,size) ->0    | x86
	// [Threads]
	// TNEW
	// TEXI

	GET_CORE_ID, // getcid () | rv
	MANA, // manage (func, op1, op2)->?|(TODO)      | shutdown reboot...
	DUP2, // dup2   (oldfd, newfd)->newfd | x86
	PIPE, // pipe   (pipefd)->0           | x86
	TNEW, // thread_create (entry, arg, stack_top)->tid  | x86 x64
	TEXI, // thread_exit   (exit_code)                   | x86 x64
	TJOI, // thread_join   (tid, &exit_code)->status     | x86 x64
	TGET, // thread_self   ()->tid                       | x86 x64
	TDET, // thread_detach (tid)->status                 | x86 x64
	TYLD, // thread_yield  ()->0                         | x86 x64
	FUTX, // futex         (addr, op, val)->status       | x86 x64


	TEST = 0xFF, // getpid (T,E,S)->0 | x86
};// . stand for well for multi-thread
// Locks usually end with `_lock;`

enum
	#ifdef _INC_CPP
	class
	#endif
	syscall_linux_t
	#ifdef _INC_CPP
	: stduint
	#endif
{
	_
};

#define IRQ_SYSCALL 0x81// leave 0x80 for unix-like syscall

#ifndef _ACCM
#if (_MCCA & 0xFF00) == 0x8600

_ESYM_C void Handint_SYSCALL_Entry();
_ESYM_C void Handint_INTCALL_Entry();
_ESYM_C stduint Handint_SYSCALL(CallgateFrame* frame);

#elif (_MCCA & 0xFF00) == 0x1000

struct NormalTaskContext;
void syscall(NormalTaskContext* cxt);


#endif
#else
#endif
struct file_proper_t {
	stduint size = 0;					// File size in bytes
	stduint mode = 0;					// File mode (type and permissions)
};

struct dirent_t {
	stduint is_dir = 0;					// Directory flag (0: file, 1: directory)
	char name[64] = {};					// File name
};

_ESYM_C stduint syscall(syscall_t callid, stduint p1 = 0, stduint p2 = 0, stduint p3 = 0);// MCCA 4 PARA SYSC

struct Syscall {
	static void Initialize();
};

#endif
