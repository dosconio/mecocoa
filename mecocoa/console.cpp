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

keyboard_state_t kbd_state = { 0 };

struct KeyboardBridge : public OstreamTrait
{
	virtual int out(const char* str, stduint len) override {
		for0(i, len) {
			if (str[i] == 1)// TTY0 ESC
				MccaTTYCon::current_switch(0);
			else if (str[i] == 0x3B) // TTY1 F1
				MccaTTYCon::current_switch(1);
			else if (str[i] == 0x3C) // TTY2 F2
				MccaTTYCon::current_switch(2);
			else if (str[i] == 0x3D) // TTY3 F3
				MccaTTYCon::current_switch(3);
			else ttycons[MccaTTYCon::current_screen_TTY]->put_inn(str[i]);
		}
		return 0;
	}
};
KeyboardBridge kbdbridge;

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
	//
	new (&kbdbridge) KeyboardBridge();// C++ Bare Programming
	kbd_out = &kbdbridge;
}

void a(char ch);



void _Comment(R1) MccaTTYCon::serv_cons_loop()
{
	//*(char*)0xB8000 = 'a';
	//outc('a');
	// while (1);
	// while (true) a('a');
	while (true) for0(i, 4) {
		MccaTTYCon& tty = *ttycons[i];
		int ch;
		while (-1 != (ch = tty.get_inn())) {
			// tty.OutFormat("(%[8H])", (char)ch);
			byte keycode = ch;
			if (tty.last_E0) {
				if (keycode == 0x49 && tty.crtline > 0) { // PgUp
					tty.auto_incbegaddr = 0;
					tty.setStartLine(--tty.crtline + tty.topline);
				}
				else if (keycode == 0x51 && tty.crtline < tty.area_total.y - tty.area_show.height) { // PgDn
					tty.auto_incbegaddr = 0;
					tty.setStartLine(++tty.crtline + tty.topline);
				}
			}
			else if (keycode < 0x80) { // KEYDOWN
				byte c = _tab_keycode2ascii[keycode].ascii_usual;
				if (c > 1)
				{
					tty.OutChar(c);
				}
			}
			tty.last_E0 = keycode == 0xE0;
		}
	}
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
