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

extern "C"
int64_t __divdi3(int64_t a, int64_t b) {
	uint64_t ua = a < 0 ? -a : a;
	uint64_t ub = b < 0 ? -b : b;
	uint64_t uq = __udivdi3(ua, ub);
	int64_t q = (int64_t)uq;
	if ((a < 0) ^ (b < 0)) {
		q = -q;
	}
	return q;
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

_ESYM_C void qsort(void* base, size_t num, size_t size, int (*compar)(const void*, const void*)) {
	if (!base || num < 2 || size == 0 || !compar) return;
	char* bytes = (char*)base;
	for (size_t gap = num / 2; gap > 0; gap /= 2) {
		for (size_t i = gap; i < num; i++) {
			for (size_t j = i; j >= gap; j -= gap) {
				char* a = bytes + j * size;
				char* b = bytes + (j - gap) * size;
				if (compar(a, b) < 0) {
					for (size_t k = 0; k < size; k++) {
						char tmp = a[k];
						a[k] = b[k];
						b[k] = tmp;
					}
				} else {
					break;
				}
			}
		}
	}
}

_ESYM_C long strtol(const char* str, char** endptr, int base) {
	if (!str) return 0;
	const char* s = str;
	while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\v' || *s == '\f') {
		s++;
	}
	bool negative = false;
	if (*s == '-') {
		negative = true;
		s++;
	} else if (*s == '+') {
		s++;
	}
	if (base == 0) {
		if (*s == '0') {
			s++;
			if (*s == 'x' || *s == 'X') {
				base = 16;
				s++;
			} else {
				base = 8;
			}
		} else {
			base = 10;
		}
	} else if (base == 16) {
		if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) {
			s += 2;
		}
	}
	unsigned long result = 0;
	bool any_digits = false;
	while (*s) {
		int digit = -1;
		char c = *s;
		if (c >= '0' && c <= '9') {
			digit = c - '0';
		} else if (c >= 'a' && c <= 'z') {
			digit = c - 'a' + 10;
		} else if (c >= 'A' && c <= 'Z') {
			digit = c - 'A' + 10;
		}
		if (digit < 0 || digit >= base) {
			break;
		}
		result = result * base + digit;
		any_digits = true;
		s++;
	}
	if (endptr) {
		*endptr = (char*)(any_digits ? s : str);
	}
	return negative ? -(long)result : (long)result;
}

_ESYM_C long __isoc23_strtol(const char* str, char** endptr, int base) {
	return strtol(str, endptr, base);
}

_ESYM_C int strcmp(const char* s1, const char* s2) {
	return StrCompare(s1, s2);
}

_ESYM_C size_t strlen(const char* s) {
	return StrLength(s);
}

_ESYM_C char* strrchr(const char* s, int c) {
	return (char*)StrIndexCharRight(s, c);
}

_ESYM_C char* strncpy(char* dest, const char* src, size_t n) {
	return StrCopyN(dest, src, n);
}

_ESYM_C char* strcat(char* dest, const char* src) {
	return StrAppend(dest, src);
}

_ESYM_C int memcmp(const void* s1, const void* s2, size_t n) {
	return MemCompare((const char*)s1, (const char*)s2, n);
}

_ESYM_C char* strcpy(char* dest, const char* src) {
	return StrCopy(dest, src);
}

_ESYM_C int strncmp(const char* s1, const char* s2, size_t n) {
	return StrCompareN(s1, s2, n);
}

_ESYM_C char* strstr(const char* haystack, const char* needle) {
	return (char*)StrIndexString(haystack, needle);
}
