#include "c/stdinc.h"
#ifdef _INC_CPP
extern "C" {
#endif
void sysinit(void);
void sysouts(const char *str);
void sysdelay(dword);
void sysquit(int);
#ifdef _INC_CPP
}
#endif
