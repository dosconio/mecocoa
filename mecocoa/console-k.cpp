// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - Keyboard
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST

#include <cpp/unisym>
use crate uni;
#include <c/consio.h>
#include <c/driver/mouse.h>
#include <c/driver/keyboard.h>
#include "../include/console.hpp"

#if _MCCA == 0x8632
#include "../include/atx-x86-flap32.hpp"
#elif _MCCA == 0x8664
#include "../include/atx-x64.hpp"
#include "../prehost/atx-x64-uefi64/atx-x64-uefi64.loader/loader-graph.h"
#endif

#if (_MCCA & 0xFF00) == 0x8600

extern BareConsole Bcons[TTY_NUMBER];
extern byte current_screen_TTY;

keyboard_state_t kbd_state = { 0 };

static void setLED() {
	KbdSetLED((_IMM(kbd_state.lock_caps) << 2) | (_IMM(kbd_state.lock_number) << 1) | _IMM(kbd_state.lock_scroll));
}


KeyboardBridge kbdbridge;
int KeyboardBridge::out(const char* str, stduint len) {
	// skip F4~F12,Q~Z,Menu,MAIN(except CapsLock,Ctrls,Shifts,Alts,Wins)
	// skip PgUp, PgDn.  BUT PrtSc, ScrollLock, Pause, Insert, Home, Delete, End
	// skip Arrows, PadKeys
	static bool last_E0 = false;
	for0(i, len) {
		switch (byte ch = str[i]) {
		// TTYs
		case 0x01:
			// bcons[0]->doshow(0); // TTY0 ESC
			break;
		case 0x3B:
			// bcons[1]->doshow(0); // TTY1 F1
			break;
		case 0x3C:
			// bcons[2]->doshow(0); // TTY2 F2
			break;
		case 0x3D:
			// bcons[3]->doshow(0); // TTY3 F3
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
			Bcons[current_screen_TTY].input_queue.OutChar(ch);
			break;
		}
		last_E0 = ((byte)str[i] == 0xE0);
	}
	return 0;
}

#endif
