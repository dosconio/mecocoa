//#include <c/stdinc.h>
#include "../../accmlib/inc/aaaaa.h"
#include "c/consio.h"

using namespace uni;

int main(int argc, char** argv)
{
	char buf[10] = { 0 };
	while (1) {
		unsigned id = systest('T', 'E', 'S');
		outsfmt("C(%d)\n\r", 1);
		while (1) {
			sysrest();
		}
		while (1) sysrest();
	}
}

