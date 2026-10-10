// static, pie

#include <stdio.h>

#if defined(_OPT_ARM32)
#include "aaaaa.h"

// printf-free decimal output: keeps the probe usable while the libc formatter is suspect
static void outnum(stduint v) {
    char b[12];
    int i = 12;
    b[--i] = 0;
    if (!v) b[--i] = '0';
    while (v) { b[--i] = (char)('0' + (v % 10)); v /= 10; }
    sysouts(&b[i]);
}
#endif

int main(int argc, char* argv[]) {
#if defined(_OPT_ARM32)
    // Blocking syscall probe: the user side keeps plain calls, no retry loop.
    sysouts("[BLK] A\n");
    stduint t0 = syssecond();
    sysrest(1, 1200);// the thread is switched out here and continues in place after the timer
    stduint t1 = syssecond();
    sysouts("[BLK] t0="); outnum(t0);
    sysouts(" t1="); outnum(t1);
    sysouts((t1 > t0) ? " slept\n" : " odd\n");
    int fd = sysopen("/dev/tty");// FileSys round trip inside one call
    sysouts((fd >= 0) ? "[BLK] C ok\n" : "[BLK] C fail\n");
#endif
    printf("[STATIC-PIE] Hello from Mecocoa Static PIE!\n");
    for (int i = 1; i < argc; i++) {
        printf("%s ", argv[i]);
    }
    printf("\n");
    return 0;
}
