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

#define outpi// Out to Port's Pin
#define outpb OUT_b
#define outpw OUT_w
#define innpi// In from Port's Pin
#define innpb IN_b
#define innpw IN_w

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
