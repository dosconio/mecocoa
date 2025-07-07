// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: [Service] Console - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _DEBUG
#include <new>
#include <c/consio.h>
#include <c/driver/keyboard.h>
#include "../include/atx-x86-flap32.hpp"

use crate uni;
#ifdef _ARC_x86 // x86:

byte MccaTTYCon::current_screen_TTY = 0;

BareConsole* BCONS0;// TTY0
static BareConsole* BCONS1;// TTY1
static BareConsole* BCONS2;// TTY2
static BareConsole* BCONS3;// TTY3
byte BUF_BCONS0[byteof(MccaTTYCon)];
byte BUF_BCONS1[byteof(MccaTTYCon)];
byte BUF_BCONS2[byteof(MccaTTYCon)];
byte BUF_BCONS3[byteof(MccaTTYCon)];

MccaTTYCon* ttycons[4];

void MccaTTYCon::cons_init()
{
	BCONS0 = new (BUF_BCONS0) MccaTTYCon(80, 50, 0 * 50); BCONS0->setShowY(0, 25);
	//
	BCONS1 = new (BUF_BCONS1) MccaTTYCon(80, 50, 1 * 50); BCONS1->setShowY(0, 25);
	BCONS2 = new (BUF_BCONS2) MccaTTYCon(80, 50, 2 * 50); BCONS2->setShowY(0, 25);
	BCONS3 = new (BUF_BCONS3) MccaTTYCon(80, 50, 3 * 50); BCONS3->setShowY(0, 25);
	ttycons[0] = (MccaTTYCon*)BCONS0;
	ttycons[1] = (MccaTTYCon*)BCONS1;
	ttycons[2] = (MccaTTYCon*)BCONS2;
	ttycons[3] = (MccaTTYCon*)BCONS3;
}


void MccaTTYCon::serv_cons_loop()
{

}

void MccaTTYCon::current_switch(byte id) {
	if (id > 3 || id == current_screen_TTY) return;
	ttycons[current_screen_TTY]->last_curposi = curget();
	ttycons[id]->setStartLine(ttycons[id]->topline + ttycons[id]->crtline);
	current_screen_TTY = id;
	curset(ttycons[id]->last_curposi);
	//
	for0(i, 4) ttycons[i]->auto_incbegaddr = false;
	ttycons[id]->auto_incbegaddr = true;

}



#endif
