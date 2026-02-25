// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - Keyboard
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <c/driver/mouse.h>
#include <c/driver/keyboard.h>
#include <cpp/Witch/Control/Control-TextBox.hpp>
#include "../include/console.hpp"


#if (_MCCA & 0xFF00) == 0x8600

extern BareConsole Bcons[TTY_NUMBER];
extern byte current_screen_TTY;

keyboard_state_t kbd_state = {};

static void setLED() {
	KbdSetLED((_IMM(kbd_state.lock_caps) << 2) | (_IMM(kbd_state.lock_number) << 1) | _IMM(kbd_state.lock_scroll));
}



#ifdef _UEFI
// uint32
#elif (_MCCA) == 0x8632
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
			(last_E0 ? kbd_state.mod.r_ctrl : kbd_state.mod.l_ctrl) = !(ch & 0x80); break;
		case 0x2A: case 0x2A + 0x80:// L-Shift
			kbd_state.mod.l_shift = !(ch & 0x80); break;
		case 0x36: case 0x36 + 0x80:// R-Shift
			kbd_state.mod.r_shift = !(ch & 0x80); break;
		case 0x38: case 0x38 + 0x80:// L-Alt
			(last_E0 ? kbd_state.mod.r_alt : kbd_state.mod.l_alt) = !(ch & 0x80); break;
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

#ifdef _UEFI
extern const char key_map[256], key_map_shift[256];//{TEMP}
extern uni::witch::control::TextBox* ptext_1;
void sysmsg_kbd(keyboard_event_t kbd_event) {
	if (!kbd_event.keycode) {
		kbd_state.mod = kbd_event.mod;
	}
	else if (0);
	else if (0) {
		
	}// locks
	else {
		auto ch = (kbd_event.mod.l_shift || kbd_event.mod.r_shift ? key_map_shift : key_map)[kbd_event.keycode];
		if (kbd_event.method == keyboard_event_t::method_t::keydown)
		{
			// only for ENUS-kbd
			if (ch == '\b') {
				auto str = ptext_1->text.reflect();
				if (*str) {// ASCIZ
					ptext_1->text[-1] = 0;
					ptext_1->text.Refresh();
				}
			}
			else ptext_1->text << ch;
			ptext_1->doshow(0);
		}
	}
}
void hand_kboard(keyboard_event_t  kmsg) {
	SysMessage msg;
	msg.type = SysMessage::RUPT_KBD;
	msg.args.kbd_event = kmsg;
	message_queue.Enqueue(msg);
}
#endif

#endif
