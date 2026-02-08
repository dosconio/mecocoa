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
	OUTC = 0x00, // putchar
	INNC = 0x01, //{TODO} getchar
	EXIT = 0x02, // exit             // (code)
	TIME = 0x03, // getsecond
	REST = 0x04, // halt
	COMM = 0x05, // sync communicate
	OPEN = 0x06, // open   file
	CLOS = 0x07, // close  file      // (fd)-> 0
	READ = 0x08, // read   file      // (fd, addr, len)
	WRIT = 0x09, // write  file      // (fd, addr, len)
	DELF = 0x0A, // remove file
	//=0x0B{} proper&enumer
	//=0x0C{} 
	WAIT = 0x0D, // wait             // (&status) -> pid
	FORK = 0x0E, // unix.fork        // () -> pid
	TMSG = 0x0F, // try message
	EXEC = 0x10, // exec             // (path, argv) -> 0[success]

	TEST = 0xFF,
};

#define IRQ_SYSCALL 0x81// leave 0x80 for unix-like syscall

#ifdef _MCCA

_ESYM_C void call_gate();
_ESYM_C void call_intr();
_ESYM_C void* call_gate_entry();
stduint syscall(syscall_t callid, ...);

#endif

#endif
