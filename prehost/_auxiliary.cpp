#define _STYLE_RUST
#include <c/ustring.h>

namespace std { void __throw_bad_function_call(void); }

_ESYM_C void __cxa_pure_virtual(void) {}
void std::__throw_bad_function_call(void) {
	plogerro("%s", __FUNCIDEN__); loop;
}

_ESYM_C void* memcpy(void* dest, const void* sors, size_t n) { return MemCopyN(dest, sors, n); }
_ESYM_C void* memset(void* str, int c, size_t n) { return MemSet(str, c, n); }
_ESYM_C void* memchr(const void* s, int c, size_t n) { return (void*)MemIndexByte((const char*)s, c, n); }
_ESYM_C char* memmove(char* dest, const char* sors, size_t width) {
	return MemAbsolute(dest, sors, width);
}
_ESYM_C void abort(void) {
	plogerro("%s", __FUNCIDEN__); loop;
}

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

_ESYM_C unsigned int __ctzsi2(unsigned int x)
{
	if (x == 0) return 32;
	unsigned int n = 0;
	while ((x & 1) == 0) {
		n++;
		x >>= 1;
	}
	return n;
}
_ESYM_C unsigned int __ctzdi2(unsigned long x)
{
	if (x == 0) return 64;
	unsigned int n = 0;
	while ((x & 1) == 0) {
		n++;
		x >>= 1;
	}
	return n;
}

