//#include <c/stdinc.h>
#include "../../../accmlib/inc/aaaaa.h"
#include "c/consio.h"

using namespace uni;

// mnt33 fat16
// mnt34 fat32

int main(int argc, char** argv)
{
	unsigned id = systest('T', 'E', 'S');// TEST
	outsfmt("C(%d)\n\r", id);// OUTC
	// TODO
	return 0;
}

