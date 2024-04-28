#include <cpp/cinc>
#include <c/x86/x86.h>
#include "../include/console.h"
void sysouts(const char *str);
void sysquit(void);
#include <cpp/cinc>

int main()
{
	sysouts("(HelloC)");
	sysquit();
	re_entry:
	sysouts("[HelloC]");
	sysquit();
	goto re_entry;
}

