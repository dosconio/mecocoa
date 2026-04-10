// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: General Kboard and Keyboard-PS2
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../include/mecocoa.hpp"
#include <c/driver/keyboard.h>

_ESYM_C void R_KBD_INIT();

#if (_MCCA & 0xFF00) == 0x8600
keyboard_state_t kbd_state = {};

extern const char key_map[256], key_map_shift[256];


#endif

#if _MCCA == 0x8632
#if 1
__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_KBD{
	.init = R_KBD_INIT,
	.name = "KBD-PS2",
};
#endif

void R_KBD_INIT() {
	IC[IRQ_Keyboard].setRange(mglb(Handint_KBD_Entry), SegCo32);
	Keyboard_Init();
}

extern KeyboardBridge kbdbridge;
void Handint_KBD() {
	kbdbridge.OutChar(innpb(PORT_KEYBOARD_DAT));
}

static void setLED() {
	KbdSetLED((_IMM(kbd_state.lock_caps) << 2) | (_IMM(kbd_state.lock_number) << 1) | _IMM(kbd_state.lock_scroll));
}

KeyboardBridge kbdbridge;
int KeyboardBridge::out(const char* str, stduint len) {
	static bool last_E0 = false;
	extern const byte key_ps2set1_usb[128];
	extern const uint8_t key_ps2set1_usb_E0[128];

	for0(i, len) {
		byte ch = str[i];
		if (ch == 0xE0) {
			last_E0 = true;
			continue;
		}

		keyboard_event_t event = {};
		event.mod = kbd_state.mod;
		event.method = (ch & 0x80) ? keyboard_event_t::method_t::keyup : keyboard_event_t::method_t::keydown;
		
		byte scancode = ch & 0x7F;
		byte vkc = 0;

		if (last_E0) {
			vkc = key_ps2set1_usb_E0[scancode];
			last_E0 = false;
		} else {
			vkc = key_ps2set1_usb[scancode];
		}

		if (vkc) {
			event.keycode = vkc;
			// Update Modifiers Inline
			if (vkc >= 0xE0 && vkc <= 0xE7) {
				byte bit = 1 << (vkc - 0xE0);
				if (event.method == keyboard_event_t::method_t::keydown) {
					kbd_state.mod_val |= bit;
				} else {
					kbd_state.mod_val &= ~bit;
				}
				event.mod = kbd_state.mod;
			}
			
			// x86 Routing Implementation Native to Bare-Metal
			if (event.method == keyboard_event_t::method_t::keydown && event.keycode == 0x39) { // CapsLock
				kbd_state.lock_caps = !kbd_state.lock_caps; setLED();
			}
			else if (event.method == keyboard_event_t::method_t::keydown && event.keycode == 0x53) { // NumLock
				kbd_state.lock_number = !kbd_state.lock_number; setLED();
			}
			else if (event.method == keyboard_event_t::method_t::keydown && event.keycode == 0x47) { // ScrollLock
				kbd_state.lock_scroll = !kbd_state.lock_scroll; setLED();
			}
			else if (event.method == keyboard_event_t::method_t::keydown) {
				auto ascii_ch = (kbd_state.mod.l_shift || kbd_state.mod.r_shift ? key_map_shift : key_map)[event.keycode];
				if (!ascii_ch && kbd_state.lock_number && event.keycode >= 0x59 && event.keycode <= 0x63) {
					ascii_ch = key_map_shift[event.keycode];
				}

				#if !_GUI_ENABLE
				BareConsole* ttycon = static_cast<BareConsole*>((Console_t*)ttys[current_screen_TTY]->offs);
				if (!ttycon) {
					plogerro("assert ttycons");
				}
				#endif
				auto p_vtty = vttys[current_screen_TTY];
				if (!p_vtty) {
					plogerro("assert p_vtty");
				}
				#if !_GUI_ENABLE
				if (event.keycode == 0x4B && ttycon->crtline > 0) { // PgUp -> PageUp VKC
					ttycon->auto_incbegaddr = 0;
					ttycon->setStartLine(--ttycon->crtline + ttycon->topline);
				}
				else if (event.keycode == 0x4E && ttycon->crtline < ttycon->area_total.y - ttycon->area_show.height) { // PgDn -> PageDown VKC
					ttycon->auto_incbegaddr = 0;
					ttycon->setStartLine(++ttycon->crtline + ttycon->topline);
				}
				else//{} F1~4 doshow
				#endif
				if (ascii_ch) {
					VTTY_INNQ(p_vtty)->OutChar(ascii_ch);
				}
			}
		}
	}
	return 0;
}

#endif

// USB3-KBD
#ifdef _UEFI// x64 only now

#include <cpp/Witch/Control/Control-TextBox.hpp>


static void setLED() {
	// {} KbdSetLED((_IMM(kbd_state.lock_caps) << 2) | (_IMM(kbd_state.lock_number) << 1) | _IMM(kbd_state.lock_scroll));
}

extern uni::witch::control::TextBox* ptext_1;

void sysmsg_kbd(keyboard_event_t kbd_event) {
	if (!kbd_event.keycode) {
		kbd_state.mod = kbd_event.mod;
	}
	else if (kbd_event.method == keyboard_event_t::method_t::keydown && kbd_event.keycode == 0x39) { // CapsLock
		kbd_state.lock_caps = !kbd_state.lock_caps; setLED();
	}
	else if (kbd_event.method == keyboard_event_t::method_t::keydown && kbd_event.keycode == 0x53) { // NumLock
		kbd_state.lock_number = !kbd_state.lock_number; setLED();
	}
	else if (kbd_event.method == keyboard_event_t::method_t::keydown && kbd_event.keycode == 0x47) { // ScrollLock
		kbd_state.lock_scroll = !kbd_state.lock_scroll; setLED();
	}
	else if (kbd_event.method == keyboard_event_t::method_t::keydown) {
		auto ch = (kbd_state.mod.l_shift || kbd_state.mod.r_shift ? key_map_shift : key_map)[kbd_event.keycode];
		if (!ch && kbd_state.lock_number && kbd_event.keycode >= 0x59 && kbd_event.keycode <= 0x63) {
			// NumPad adjustments
			ch = key_map_shift[kbd_event.keycode];
		}
		
		// UEFI GUI specific implementation
		if (ch == '\b') {
			if (ptext_1) {
				auto str = ptext_1->text.reflect();
				if (*str) {
					ptext_1->text[-1] = 0;
					ptext_1->text.Refresh();
				}
			}
		}
		else if (ptext_1 && ch) ptext_1->text << ch;
		if (ptext_1) ptext_1->doshow(0);

		// ----

		// test taskman
		// now the task 1 should at ready queue.
		if (ch == 'S') Taskman::DequeueReady(&treat<ProcessBlock>(Taskman::chain[2]->offs));
		else if (ch == 'R') Taskman::EnqueueReady(&treat<ProcessBlock>(Taskman::chain[2]->offs));
	}
}

void hand_kboard(keyboard_event_t  kmsg) {
	SysMessage msg;
	msg.type = SysMessage::RUPT_KBD;
	msg.args.kbd_event = kmsg;
	message_queue.Enqueue(msg);
}
#endif

