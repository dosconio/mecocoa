// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _DEBUG

#include <c/consio.h>
#include <c/driver/keyboard.h>

use crate uni;
#ifdef _ARC_x86 // x86:
#include "../include/atx-x86-flap32.hpp"
#include <cpp/Device/_Video.hpp>


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

static void setLED() {
	KbdSetLED((_IMM(kbd_state.lock_caps) << 2) | (_IMM(kbd_state.lock_number) << 1) | _IMM(kbd_state.lock_scroll));
}

struct KeyboardBridge : public OstreamTrait // // scan code set 1
{
	virtual int out(const char* str, stduint len) override {
		// skip F4~F12,Q~Z,Menu,MAIN(except CapsLock,Ctrls,Shifts,Alts,Wins)
		// skip PgUp, PgDn.  BUT PrtSc, ScrollLock, Pause, Insert, Home, Delete, End
		// skip Arrows, PadKeys
		static bool last_E0 = false;
		for0(i, len) {
			switch (byte ch = str[i]) {
			// TTYs
			case 0x01:
				MccaTTYCon::current_switch(0);// TTY0 ESC
				break;
			case 0x3B:
				MccaTTYCon::current_switch(1); // TTY1 F1
				break;
			case 0x3C:
				MccaTTYCon::current_switch(2); // TTY2 F2
				break;
			case 0x3D:
				MccaTTYCon::current_switch(3); // TTY3 F3
				break;
				// Ctrls(^), Shifts(-), Alts(+), Wins(~)
			case 0x1D: case 0x1D + 0x80:// L-Ctrl
				(last_E0 ? kbd_state.r_ctrl : kbd_state.l_ctrl) = !(ch & 0x80); break;
			case 0x2A: case 0x2A + 0x80:// L-Shift
				kbd_state.l_shift = !(ch & 0x80); break;
			case 0x36: case 0x36 + 0x80:// R-Shift
				kbd_state.r_shift = !(ch & 0x80); break;
			case 0x38: case 0x38 + 0x80:// L-Alt
				(last_E0 ? kbd_state.r_alt : kbd_state.l_alt) = !(ch & 0x80); break;
				//
				//{TODO} Wins
				// Locks
			case 0x3A:// CapsLock
				kbd_state.lock_caps = !kbd_state.lock_caps; setLED(); break;
			case 0x45:// NumLock
				kbd_state.lock_number = !kbd_state.lock_number; setLED(); break;
			case 0x46:// ScrollLock
				kbd_state.lock_scroll = !kbd_state.lock_scroll; setLED(); break;
			default:
				ttycons[MccaTTYCon::current_screen_TTY]->put_inn(ch);
				break;
			}
			last_E0 = ((byte)str[i] == 0xE0);
		}
		return 0;
	}
};
KeyboardBridge kbdbridge;






class GloScreen : public VideoControlInterface {
public:
	inline uint8& Locate(const Point& disp) const {
		// uint32 base = *(volatile uint32*)(0x78000 + 0x28);
		return *((uint8*)(0xFD000000) + disp.x * 3 + disp.y * 3 * 800 _Comment(width));// without boundary check
		// 0xFD000000  default
		// 0xFC000000  -device VGA,vgamem_mb=32
		// 0xF8000000  -device VGA,vgamem_mb=64
	}
public:
	virtual ~GloScreen() = default;
	GloScreen() {}
	virtual void SetCursor(const Point& disp) const override;
	virtual Point GetCursor() const override;

	// without boundary check
	virtual void DrawPoint(const Point& disp, Color color) const override;
	virtual void DrawRectangle(const Rectangle& rect) const override;

	//
	virtual void DrawFont(const Point& disp, const DisplayFont& font) const override;
	virtual Color GetColor(Point p) const override;
};
void GloScreen::SetCursor(const Point& disp) const { _TODO; }// MAYBE unused
Point GloScreen::GetCursor() const { _TODO return {0, 0}; }// MAYBE unused
void GloScreen::DrawPoint(const Point& disp, Color color) const {
	uint8* p = &Locate(disp);
	*p++ = color.b;
	*p++ = color.g;
	*p++ = color.r;
}
void GloScreen::DrawRectangle(const Rectangle& rect) const {
	uint8* p = &Locate(rect.getVertex());
	for0(y, rect.height) {
		uint8* pp = p;
		for0(x, rect.width) {
			*pp++ = rect.color.b;
			*pp++ = rect.color.g;
			*pp++ = rect.color.r;
		}
		p += 800 * 3;//{} 800x600
	}
}
void GloScreen::DrawFont(const Point& disp, const DisplayFont& font) const {
	_TODO;
}
Color GloScreen::GetColor(Point p) const {
	return cast<Color>(Locate(p));
}
void MccaTTYCon::cons_init()
{
	BCONS0 = new (BUF_BCONS0) MccaTTYCon(bda->screen_columns, 24, 0); BCONS0->setShowY(0, 24);
	//
	BCONS1 = new (BUF_BCONS1) MccaTTYCon(bda->screen_columns, 50, 1 * 50); BCONS1->setShowY(0, 25);
	BCONS2 = new (BUF_BCONS2) MccaTTYCon(bda->screen_columns, 50, 2 * 50); BCONS2->setShowY(0, 25);
	BCONS3 = new (BUF_BCONS3) MccaTTYCon(bda->screen_columns, 50, 3 * 50); BCONS3->setShowY(0, 25);
	ttycons[0] = (MccaTTYCon*)BCONS0;
	ttycons[1] = (MccaTTYCon*)BCONS1;
	ttycons[2] = (MccaTTYCon*)BCONS2;
	ttycons[3] = (MccaTTYCon*)BCONS3;
	//
	new (&kbdbridge) KeyboardBridge();// C++ Bare Programming
	kbd_out = &kbdbridge;

	// ABOVE are OUTDATED

	//{TODO} Switch Graphic Mode
	__asm("call SwitchReal16");
	//{TODO} VideoConsole - 4 TTYs & Redirect
	//{TODO} Code Adapt for
	//{TODO} PS2/USB Mouse
	// Console.OutFormat("\xFF\x70[Mecocoa]\xFF\x02 Real16 Switched Test OK!\xFF\x07\n\r");

	// byte* p = (byte*)0xA0000;
	// for0(i, 800 * 3)* p++ = 0xFF;
	// p += 800 * 3 * 3;
	// for0(i, 800) { *p++ = 0xFF;  *p++ = 0x00; *p++ = 0x00; }// G
	// p += 800 * 3 * 3;
	// for0(i, 800) { *p++ = 0x00;  *p++ = 0xFF; *p++ = 0x00; }// B
	// p += 800 * 3 * 3;
	// for0(i, 800) { *p++ = 0x00;  *p++ = 0x00; *p++ = 0xFF; }// R

	GloScreen wholescreen0;
	VideoConsole vcon0(wholescreen0, { 800,600 });
	// uint8* ppp = (uint8*)0xFD000000 + 800 * 3 * 500 + 10 * 3;
	// for0(y, 10) {
	// 	uint8* pp = ppp;
	// 	for0(x, 10) {
	// 		*pp++ = 0xFF;
	// 		*pp++ = 0xFF;
	// 		*pp++ = 0xFF;
	// 	}
	// 	ppp += 800 * 3;// 800x600
	// }


	wholescreen0.DrawRectangle(Rectangle(Point(0, 0), Size2(800, 600), 0xDD0022));
	vcon0.OutFormat("Ciallo\n\r %u", 2025);

	// uint32 base = *(volatile uint32*)(0x78000 + 0x28);
	// outsfmt("base: 0x%[32H]\n\r", base);

	// outsfmt("     00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n\r");
	// char* __p = (char*)0x78000;
	// for0(j, 10) {
	// 	outsfmt("%[16H] ", j << 4);
	// 	for0(i, 16) {
	// 		outsfmt("%[8H] ", *__p++);
	// 	}
	// 	outsfmt("\n\r");
	// }

	loop _ASM ("hlt");
}


static void tty_parse(MccaTTYCon& tty, byte keycode, keyboard_state_t state) { // // scan code set 1
	if (tty.last_E0) {
		if (keycode == 0x49 && tty.crtline > 0) { // PgUp
			tty.auto_incbegaddr = 0;
			tty.setStartLine(--tty.crtline + tty.topline);
		}
		else if (keycode == 0x51 && tty.crtline < tty.area_total.y - tty.area_show.height) { // PgDn
			tty.auto_incbegaddr = 0;
			tty.setStartLine(++tty.crtline + tty.topline);
		}
		else if (keycode == 0x35) // Pad /
		{
			tty.OutChar('/');
		}
		else if (keycode == 0x1C) // Pad ENTER
		{
			tty.OutChar('\n');
			tty.OutChar('\r');
		}
	}
	else if (Rangein(keycode, 0x47, 0x54)) {
		byte c = (state.lock_number) ? _tab_keycode2ascii[keycode].ascii_shift : _tab_keycode2ascii[keycode].ascii_usual;
		if (c > 1)
			tty.OutChar(c);
	}
	else if (keycode < 0x80) { // KEYDOWN
		byte c = _tab_keycode2ascii[keycode].ascii_usual;
		if (Ranglin(c, 'a', 26))
			c = (state.lock_caps ^ (state.l_shift || state.r_shift)) ?
			_tab_keycode2ascii[keycode].ascii_shift : _tab_keycode2ascii[keycode].ascii_usual;
		else c = (state.l_shift || state.r_shift) ?
			_tab_keycode2ascii[keycode].ascii_shift : _tab_keycode2ascii[keycode].ascii_usual;

		if (c > 1)
		{
			//{} wait app get the char

			tty.OutChar(c);
			if (c == '\n') tty.OutChar('\r');
			else if (c == '\b') {
				tty.OutChar(' ');
				tty.OutChar('\b');
			}
			//{TODO} Menus, TAB, Arrows, PrtSc, Pause, Ins, Home, Del, End
		}
	}
	tty.last_E0 = keycode == 0xE0;
}

void _Comment(R1) MccaTTYCon::serv_cons_loop()
{
	//*(char*)0xB8000 = 'a';
	//outc('a');
	// while (1);
	// while (true) a('a');
	struct element { byte ch; byte attr; };
	Letvar(Ribbon, element*, (0xB8000 + 80 * 2 * 24));
	Ribbon[0].ch = '^';
	Ribbon[1].ch = '-';
	Ribbon[2].ch = '+';

	Ribbon[77].ch = '+';
	Ribbon[78].ch = '-';
	Ribbon[79].ch = '^';

	while (true) {
		for0(i, 4) {
			MccaTTYCon& tty = *ttycons[i];
			int ch;
			while (-1 != (ch = tty.get_inn())) {
				tty_parse(tty, ch, kbd_state);
			}
		}
		if (current_screen_TTY == 0) {
			// Render the bottom ribbon
			Ribbon[0].attr = kbd_state.l_ctrl ? 0x70 : 0x07;
			Ribbon[1].attr = kbd_state.l_shift ? 0x70 : 0x07;
			Ribbon[2].attr = kbd_state.l_alt ? 0x70 : 0x07;
			Ribbon[77].attr = kbd_state.r_alt ? 0x70 : 0x07;
			Ribbon[78].attr = kbd_state.r_shift ? 0x70 : 0x07;
			Ribbon[79].attr = kbd_state.r_ctrl ? 0x70 : 0x07;
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
