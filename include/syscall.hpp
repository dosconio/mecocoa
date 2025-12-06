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
	//=0x0D{} 
	FORK = 0x0E, // unix.fork        // () -> pid
	TMSG = 0x0F, // try message

	TEST = 0xFF,
};


#endif
