//{} #include "c/stdinc.h"
#ifdef _INC_CPP
extern "C" {
#endif
	int main();
	void syscall();
	//
	void sysouts(const char* str);
	void sysdelay(unsigned dword);
	void sysquit(int code);
#ifdef _INC_CPP
}
#endif
