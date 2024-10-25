//#define _OPT_RISCV64
#include <c/stdinc.h>
#include "../../userkit/inc/cocoapp.h"
#include "../../userkit/inc/stdio.h"

const int SIZE = 10;
const int P = 3;
const int STEP = 100000;
const int MOD = 10007;

///Expectation
/// 3^10000=5079
/// 3^20000=8202
/// 3^30000=8824
/// 3^40000=5750
/// 3^50000=3824
/// 3^60000=8516
/// 3^70000=2510
/// 3^80000=9379
/// 3^90000=2621
/// 3^100000=2749
/// Test power OK!

int main()
{
	int pow[10] = {};
	int index = 0;
	int i, last;
	puts("[subapp1] Yahoo");
	pow[index] = 1;
	for (i = 1; i <= STEP; ++i) {
		last = pow[index];
		index = (index + 1) % SIZE;
		pow[index] = (last * P) % MOD;
		if ((i % 10000) == 0) {
			printf("%d^%d=%d\n", P, i, pow[index]);
			sleep(500);
		}
	}
	puts("[subapp1] Test power OK!");
	return 0;
}
