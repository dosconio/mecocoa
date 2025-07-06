#include "c/stdinc.h"
#ifdef _INC_CPP
extern "C" {
#endif
	int main();
	stduint syscall(stduint p0, stduint p1, stduint p2, stduint p3);
	//void syscall(stduint p0, stduint p1 = 0, stduint p2 = 0, stduint p3 = 0);
	//
	void sysouts(const char* str);
	void sysdelay(unsigned dword);
	void sysquit(int code);

	stduint systest(unsigned t, unsigned e, unsigned s);
	
#ifdef _INC_CPP
}
#endif
