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

// Return quotient (n / d). If d == 0, returns UINT64_MAX (caller should avoid div by 0).
extern "C"
uint64_t __udivdi3(uint64_t n, uint64_t d) {
    if (d == 0) return UINT64_MAX; // undefined in C; choose sentinel
    uint64_t q = 0;
    uint64_t r = 0;
    // Iterate bits from most-significant to least-significant
    for (int i = 63; i >= 0; --i) {
        // shift r left by 1, bring in the next highest bit of n
        r = (r << 1) | ((n >> i) & 1ULL);
        if (r >= d) {
            r -= d;
            q |= (1ULL << i);
        }
    }
    return q;
}

// Return remainder (n % d). If d == 0, returns UINT64_MAX (caller should avoid div by 0).
extern "C"
uint64_t __umoddi3(uint64_t n, uint64_t d) {
    if (d == 0) return UINT64_MAX; // undefined in C; choose sentinel
    uint64_t r = 0;
    for (int i = 63; i >= 0; --i) {
        r = (r << 1) | ((n >> i) & 1ULL);
        if (r >= d) {
            r -= d;
        }
    }
    return r;
}
