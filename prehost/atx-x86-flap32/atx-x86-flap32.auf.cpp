#include <stdint.h>

// may no use
extern "C"
uint64_t __udivmoddi4(uint64_t num, uint64_t den, uint64_t * rem_p) {
	uint64_t quot = 0, qbit = 1;
	if (den == 0) return 0;
	// Left-align denominator
	while ((int64_t)den >= 0) {
		den <<= 1;
		qbit <<= 1;
	}
	while (qbit) {
		if (den <= num) {
			num -= den;
			quot += qbit;
		}
		den >>= 1;
		qbit >>= 1;
	}
	if(rem_p) rem_p[0] = num;
	return quot;
}
