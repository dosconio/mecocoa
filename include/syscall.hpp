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
	INNC = 0x01, // innstr (blocked)->ASCII>0 | x86 rv
	EXIT = 0x02, // exit   (code)             | x86 rv
	TIME = 0x03, // getsec (0sec/1ms)->second | x86
	REST = 0x04, // halt   (unit, time)       | x86
	COMM = 0x05, // syncom (mod, obj, &msg)   | x86 rv
	OPEN = 0x06, // open   (path,flags)>=0    | x86
	CLOS = 0x07, // close  (fd)->0            | x86
	READ = 0x08, // read   (fd, adr, len)->len| x86
	WRIT = 0x09, // write  (fd, adr, len)->len| x86
	DELF = 0x0A, // remove (pathname)->?      | x86
	//=0x0B{} proper&enumer
	//=0x0C{} 
	WAIT = 0x0D, // wait   (&status)->pid     | x86
	FORK = 0x0E, // fork   ()->pid            | x86
	TMSG = 0x0F, // trymsg ()->(msg_unsovled) | x86 rv
	EXEC = 0x10, // spawnl (path,args,len)->0 | x86
	EXET = 0x11, // execl  (path,args,len)->0 | x86
	MALC = 0x12, // malloc (pages)->addr>0    | 

	GET_CORE_ID, // | rv


	TEST = 0xFF, // (T,E,S)->0 | x86
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
_ESYM_C stduint syscall(syscall_t callid, stduint p1 = 0, stduint p2 = 0, stduint p3 = 0);// MCCA 4 PARA SYSC

#endif
