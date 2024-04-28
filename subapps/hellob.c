#include <c/x86/x86.h>
#include "../include/console.h"

void sysouts(const char *str);
void sysquit(void);

int main()
{
	sysouts("(HelloB)");
	sysquit();
	re_entry:
	sysouts("[HelloB]");
	sysquit();
	goto re_entry;
}

