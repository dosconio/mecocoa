// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#undef _DEBUG
#define _DEBUG

#include <c/consio.h>
#include <c/driver/keyboard.h>

use crate uni;
#ifdef _ARC_x86 // x86:
#include "../include/atx-x86-flap32.hpp"
#include "../include/console.hpp"


byte MccaTTYCon::current_screen_TTY = 0;

// consider CLI and GUI
OstreamTrait* tty0_out;
OstreamTrait* tty1_out;
OstreamTrait* tty2_out;
OstreamTrait* tty3_out;

BareConsole* BCONS0;// TTY0
static BareConsole* BCONS1;// TTY1
static BareConsole* BCONS2;// TTY2
static BareConsole* BCONS3;// TTY3
byte BUF_BCONS0[byteof(MccaTTYCon)];
byte BUF_BCONS1[byteof(MccaTTYCon)];
byte BUF_BCONS2[byteof(MccaTTYCon)];
byte BUF_BCONS3[byteof(MccaTTYCon)];
MccaTTYCon* ttycons[4];
//
byte BUF_VCI[sizeof(GloScreen)];
byte BUF_CONS0[sizeof(VideoConsole)];
VideoConsole* vcon0; OstreamTrait* con0_out;


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

#define TEMP_AREA 0x78000

// [QEMU] Video Address
// 0xFD000000  default
// 0xFC000000  -device VGA,vgamem_mb=32
// 0xF8000000  -device VGA,vgamem_mb=64 ...

ModeInfoBlock* video_info;

uint8& GloScreen::Locate(const Point& disp) const {
	return *((uint8*)(video_info->PhysBasePtr) +
		(disp.x * video_info->BitsPerPixel >> 3) +
		disp.y * video_info->BytesPerScanLine);// without boundary check
}
void GloScreen::SetCursor(const Point& disp) const { _TODO; }// MAYBE unused
Point GloScreen::GetCursor() const { _TODO return {0, 0}; }// MAYBE unused
void GloScreen::DrawPoint(const Point& disp, Color color) const {
	uint8* p = &Locate(disp);
	*p++ = color.b;
	*p++ = color.g;
	*p++ = color.r;
}
void GloScreen::DrawRectangle(const Rectangle& rect) const {// RGB 24bpp only
	uint8* p = &Locate(rect.getVertex());
	uint8 comm[3 * 4] = {
		rect.color.b, rect.color.g, rect.color.r,
		rect.color.b, rect.color.g, rect.color.r,
		rect.color.b, rect.color.g, rect.color.r,
		rect.color.b, rect.color.g, rect.color.r,
	};
	uint32 (*com)[3] = (uint32(*)[3])comm;
	for0(y, rect.height) {
		union {
			uint8* pp8;
			uint32* pp32;
		};
		pp8 = p;
		for0r(x, rect.width) {
			if (!(_IMM(pp8) & 0b11) && x >= 12) {
				pp32[0] = treat<uint32>(&comm[4 * 0]);
				pp32[1] = treat<uint32>(&comm[4 * 1]);
				pp32[2] = treat<uint32>(&comm[4 * 2]);
				x -= 3 - 1;
				pp8 += 12;
				continue;
			}
			*pp8++ = rect.color.b;
			*pp8++ = rect.color.g;
			*pp8++ = rect.color.r;
		}
		p += video_info->BytesPerScanLine;
	}
}
void GloScreen::DrawFont(const Point& disp, const DisplayFont& font) const {
	_TODO;
}
Color GloScreen::GetColor(Point p) const {
	return cast<Color>(Locate(p));
}

class Graphic {
public:
	static bool setMode(VideoMode vmode);
};
_ESYM_C word VideoModeVal;
bool Graphic::setMode(VideoMode vmode) {
	VideoModeVal = _IMM(vmode);
	__asm("call SwitchReal16");
	MemCopyN(video_info, (pureptr_t)TEMP_AREA, offsetof(ModeInfoBlock, ReservedTail));
	return !VideoModeVal;
}

extern bool ento_gui;
void blink() {
	static bool b = false;
	treat<GloScreen>(BUF_VCI).DrawRectangle(Rectangle(Point(0, 0), Size2(8, 16), b ? Color::Black : Color::White));
	b = !b;
}
void blink2() {
	static bool b = false;
	treat<GloScreen>(BUF_VCI).DrawRectangle(Rectangle(Point(8, 0), Size2(8, 16), b ? Color::Black : Color::White));
	b = !b;
}

void MccaTTYCon::cons_init()
{
	video_info = (uni::ModeInfoBlock *)Memory::physical_allocate(0x1000);
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

	if (!Graphic::setMode(VideoMode::RGB888_800x600))
	{
		con0_out = BCONS0;
		plogerro("Switch to GUI.");
		return;
	}
	ento_gui = true;
	kernel_paging.MapWeak(
		video_info->PhysBasePtr,
		video_info->PhysBasePtr,
		video_info->BytesPerScanLine * video_info->YResolution,
		true, _Comment(R0) false
	);// VGA

	//{TODO} PS2/USB Mouse
	//{TODO} BUFFER : vcon-buff
	
	// TTY0 background one (TODO: BUFFER and {clear;reflush})
	new (BUF_VCI) GloScreen();
	Rectangle screen0_win(
		Point(nil, nil),
		Size2(video_info->XResolution, video_info->YResolution),
		Color::White
	);
	vcon0 = new (BUF_CONS0) VideoConsole(treat<GloScreen>(BUF_VCI), screen0_win);
	con0_out = vcon0;
	vcon0->forecolor = Color::Black;
	vcon0->Clear();
	Console.OutFormat("\xFF\x70[Mecocoa]\xFF\x27 Real16 Switched Test OK!\xFF\x07\n\r");
	
	// TTY1 window form (TODO)
	// TTY2 window form (TODO)
	// TTY3 window form (TODO)
}


static void tty_parse(MccaTTYCon& tty, byte keycode, keyboard_state_t state) { // // scan code set 1
	if (ento_gui) {
		return;
		if (&tty == ttycons[0]) {
			_TODO
		}
	}
	_TODO
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
	// struct element { byte ch; byte attr; };
	// Letvar(Ribbon, element*, (0xB8000 + 80 * 2 * 24));
	// Ribbon[0].ch = '^';
	// Ribbon[1].ch = '-';
	// Ribbon[2].ch = '+';
	// Ribbon[77].ch = '+';
	// Ribbon[78].ch = '-';
	// Ribbon[79].ch = '^';

	while (true) {
		if (!ento_gui) for0(i, 4) {
			MccaTTYCon& tty = *ttycons[i];
			int ch;
			while (-1 != (ch = tty.get_inn())) {
				tty_parse(tty, ch, kbd_state);
			}
		}
		// if (current_screen_TTY == 0) {
		// 	// Render the bottom ribbon
		// 	Ribbon[0].attr = kbd_state.l_ctrl ? 0x70 : 0x07;
		// 	Ribbon[1].attr = kbd_state.l_shift ? 0x70 : 0x07;
		// 	Ribbon[2].attr = kbd_state.l_alt ? 0x70 : 0x07;
		// 	Ribbon[77].attr = kbd_state.r_alt ? 0x70 : 0x07;
		// 	Ribbon[78].attr = kbd_state.r_shift ? 0x70 : 0x07;
		// 	Ribbon[79].attr = kbd_state.r_ctrl ? 0x70 : 0x07;
		// }
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

void memdump(_TODO)
{
	// outsfmt("     00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n\r");
	// char* __p = (char*)0x78000;
	// for0(j, 10) {
	// 	outsfmt("%[16H] ", j << 4);
	// 	for0(i, 16) {
	// 		outsfmt("%[8H] ", *__p++);
	// 	}
	// 	outsfmt("\n\r");
	// }
}



#endif
