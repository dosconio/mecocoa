#include "c/stdinc.h"
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
	
#ifdef _INC_CPP
}
#endif
