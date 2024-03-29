// ASCII C TAB4 LF
// Attribute: Bits(32)
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Flat-32 Console Text I/O Driver
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// Implement: mecocoa/drivers/conio/conio32.c

#ifndef _INC_CONIO32
#define _INC_CONIO32

#include <../c/alice.h>
#include <../c/x86/port.h>


void curset(word posi);
word curget(void);
void scrrol(word lines);
void outtxt(const char *str, dword len);
#define outs(a) outtxt(a, ~(dword)0)
void outc(const char chr);

void outi8hex(const byte inp);
void outi16hex(const word inp);
void outi32hex(const dword inp);

#define _CONCOL_DarkIoWhite "\xFF\x70"


#endif
