#include "c/stdinc.h"

#define sysrecv(pid,msg) syscomm(0,pid,msg)
#define syssend(pid,msg) syscomm(1,pid,msg)

#define OUTC 0x00
#define INNC 0x01
#define EXIT 0x02
#define TIME 0x03
#define REST 0x04
#define COMM 0x05
#define OPEN 0x06
#define CLOS 0x07
#define READ 0x08
#define WRIT 0x09
#define DELF 0x0A

enum {
	Task_Kernel,
	Task_Con_Serv,
	Task_Hdd_Serv,
	Task_FileSys,
	Task_AppB,
	Task_AppA,
	Task_AppC,
	//
	TaskCount
};

#ifdef _INC_CPP
extern "C" {
#endif
	int main(int argc, char** argv);
	stduint syscall(stduint p0, stduint p1, stduint p2, stduint p3);
	//void syscall(stduint p0, stduint p1 = 0, stduint p2 = 0, stduint p3 = 0);
	//
	void sysouts(const char* str);// 00
	stduint syssecond();// 03
	void sysrest();// 04

	struct CommMsg {
		stduint address;
		stduint length;
		stduint type;
		stduint src;// use if type is HARDRUPT
	};
	void syscomm(int send_recv, stduint obj, struct CommMsg* msg);// 05

	void sysdelay(unsigned dword);
	void sysquit(int code);

	stduint systest(unsigned t, unsigned e, unsigned s);

	// return negative if failed
	int sys_createfil(rostr fullpath);
	//
	int sysopen(rostr fullpath);
	// return nil for success
	int sysclose(int fd);
	//
	stduint sysread(int fd, void* buf, stduint size);
	//
	stduint syswrite(int fd, void* buf, stduint size);
	// return nil for success
	int sys_removefil(rostr fullpath);

#ifdef _INC_CPP
}
#endif
