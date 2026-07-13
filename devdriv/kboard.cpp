// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: General Kboard and Keyboard-PS2
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../include/mecocoa.hpp"
#include <c/driver/keyboard.h>

extern "C" stduint sys_kill(stduint pid, int sig, stduint tid);

_ESYM_C void R_KBD_INIT();

#if (_MCCA & 0xFF00) == 0x8600
keyboard_state_t kbd_state = {};

extern const char key_map[256], key_map_shift[256];


#endif

#if (_MCCA & 0xFF00) == 0x8600
#if 1
__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_KBD{
	.init = R_KBD_INIT,
	.name = "KBD-PS2",
};
#endif

void R_KBD_INIT() {
	#if _MCCA == 0x8664
	IC[IRQ_Keyboard].setModeRupt(mglb(Handint_KBD_Entry), SegCo64);
	#else
	IC[IRQ_Keyboard].setRange(mglb(Handint_KBD_Entry), SegCo32);
	#endif
	register_interrupt_handler(IRQ_Keyboard, Handint_KBD);
	Keyboard_Init();
	auto* i8042 = Devsman::RegisterSerioController("i8042");
	if (i8042) {
		Devsman::AddIoPortResource(i8042, 0, PORT_KEYBOARD_DAT, 1);
		Devsman::AddIoPortResource(i8042, 1, PORT_KEYBOARD_CMD, 1);
		if (auto* ps2kbd = Devsman::RegisterSerioDevice(i8042, "ps2kbd")) {
			Devsman::AddIrqResource(ps2kbd, IRQ_Keyboard);
		}
	}
}

extern KeyboardBridge kbdbridge;
void Handint_KBD() {
	kbdbridge.OutChar(innpb(PORT_KEYBOARD_DAT));
	IC.SendEOI(IRQ_Keyboard); // Acknowledge interrupt
}

static void setLED_ps2() {
	KbdSetLED((_IMM(kbd_state.lock_caps) << 2) | (_IMM(kbd_state.lock_number) << 1) | _IMM(kbd_state.lock_scroll));
}

KeyboardBridge kbdbridge;
extern BareConsole Bcons[TTY_NUMBER];
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
			
			// Global Hotkeys
			if (event.method == keyboard_event_t::method_t::keydown && (event.mod.l_logo || event.mod.r_logo)) {
				if (event.keycode == 0x06) { // Win + C
					extern SpinlockBlock<uni::Queue<SysMessage>> message_queue_conv;
					SysMessage msg;
					msg.type = SysMessage::RUPT_NEW_TERM;
					message_queue_conv.Lock()->Enqueue(msg);
				}
				return 0; // Intercept: do not pass to VTTY or other sheets
			}
			else if (event.method == keyboard_event_t::method_t::keydown && event.keycode == 0x39) { // CapsLock
				kbd_state.lock_caps = !kbd_state.lock_caps; setLED_ps2();
				continue;
			}
			else if (event.method == keyboard_event_t::method_t::keydown && event.keycode == 0x53) { // NumLock
				kbd_state.lock_number = !kbd_state.lock_number; setLED_ps2();
				continue;
			}
			else if (event.method == keyboard_event_t::method_t::keydown && event.keycode == 0x47) { // ScrollLock
				kbd_state.lock_scroll = !kbd_state.lock_scroll; setLED_ps2();
				continue;
			}
			else if (event.method == keyboard_event_t::method_t::keydown) {
				auto ascii_ch = (kbd_state.mod.l_shift || kbd_state.mod.r_shift ? key_map_shift : key_map)[event.keycode];
				if (!ascii_ch && kbd_state.lock_number && event.keycode >= 0x59 && event.keycode <= 0x63) {
					ascii_ch = key_map_shift[event.keycode];
				}

				#if !_GUI_ENABLE
				BareConsole* ttycon = static_cast<BareConsole*>((Console_t*)ttys[Consman::current_screen_TTY]->offs);
				if (!ttycon) {
					plogerro("assert ttycons");
				}
				auto p_vtty = vttys[Consman::current_screen_TTY];
				if (!p_vtty) {
					plogerro("assert p_vtty");
				}
				#endif
				#if !_GUI_ENABLE
				if (event.keycode == 0x4B && ttycon->crtline > 0) { // PgUp -> PageUp VKC
					ttycon->auto_incbegaddr = 0;
					ttycon->setStartLine(--ttycon->crtline + ttycon->topline);
				}
				else if (event.keycode == 0x4E && ttycon->crtline < ttycon->area_total.y - ttycon->area_show.height) { // PgDn -> PageDown VKC
					ttycon->auto_incbegaddr = 0;
					ttycon->setStartLine(++ttycon->crtline + ttycon->topline);
				}
				else if (Ranglin(event.keycode, 0x3A, 4))// F1~4 doshow
				{
					Bcons[event.keycode - 0x3A].doshow(pureptr_t(event.keycode - 0x3A));
				}
				#endif
				if (ascii_ch) {
					#if _GUI_ENABLE
					// Delegate keyboard event to serv_graf_loop to avoid
					// accessing last_click_sheet in interrupt context.
					// Direct onrupt() here causes UAF when GraphicMsg_FDEL deletes the sheet.
					{
						extern SpinlockBlock<uni::Queue<SysMessage>> message_queue_conv;
						SysMessage msg;
						msg.type = SysMessage::RUPT_KBD;
						msg.args.kbd_event = event;
						message_queue_conv.Lock()->Enqueue(msg);
					}
					return 0; // Intercept: event will be forwarded in serv_graf_loop
					#else
					// Check for Ctrl+C to trigger SIGINT (Non-GUI Mode, exclude Shift)
					if ((kbd_state.mod.l_ctrl || kbd_state.mod.r_ctrl) && !kbd_state.mod.l_shift && !kbd_state.mod.r_shift && ascii_ch == 'c') {
						if (p_vtty && p_vtty->type) {
							auto pblock = (vtty_type_t*)p_vtty->type;
							for (stduint idx = 0; idx < pblock->proc_group.Count(); idx++) {
								stduint target_pid = pblock->proc_group[idx];
								if (target_pid >= TaskCount && target_pid != pblock->master_pid) {
									sys_kill(target_pid, SIGINT, 0);
								}
							}
						}
						return 0;
					}
					// Translate Ctrl + letters to control characters (e.g., Ctrl+D -> 0x04)
					if ((kbd_state.mod.l_ctrl || kbd_state.mod.r_ctrl) && !kbd_state.mod.l_shift && !kbd_state.mod.r_shift) {
						if (ascii_ch >= 'a' && ascii_ch <= 'z') {
							ascii_ch = ascii_ch - 'a' + 1;
						}
					}
					VTTY_INNQ(p_vtty)->OutChar(ascii_ch);
					Consman::WakeBlockedWaiters();
					#endif
				}
			}
		}
		// Forward ALL keyboard events (down/up/repeat) to focused window
		#if _GUI_ENABLE
		// Delegate keyboard event to serv_graf_loop to avoid
		// accessing last_click_sheet in interrupt context.
		{
			extern SpinlockBlock<uni::Queue<SysMessage>> message_queue_conv;
			SysMessage msg;
			msg.type = SysMessage::RUPT_KBD;
			msg.args.kbd_event = event;
			message_queue_conv.Lock()->Enqueue(msg);
		}
		#else
		if (asrtand(Consman::last_click_sheet)->refSheetNode().next) {
			Consman::last_click_sheet->onrupt(SheetEvent::onKeybd, Point(0, 0), &event);
			Consman::WakeBlockedWaiters();
		}
		#endif
	}
	// Render the bottom ribbon
	#if !_GUI_ENABLE
	struct element { byte ch; byte attr; };
	Letvar(Ribbon, element*, (_VIDEO_ADDR_BUFFER + 80 * 2 * 24));
	if (!Consman::ento_gui && Consman::current_screen_TTY == 0) {
		Ribbon[0].attr = kbd_state.mod.l_ctrl ? 0x70 : 0x07;
		Ribbon[1].attr = kbd_state.mod.l_shift ? 0x70 : 0x07;
		Ribbon[2].attr = kbd_state.mod.l_alt ? 0x70 : 0x07;
		Ribbon[77].attr = kbd_state.mod.r_alt ? 0x70 : 0x07;
		Ribbon[78].attr = kbd_state.mod.r_shift ? 0x70 : 0x07;
		Ribbon[79].attr = kbd_state.mod.r_ctrl ? 0x70 : 0x07;
	}
	#endif
	return 0;
}

#endif

// USB3-KBD
#if _MCCA == 0x8664// x64 only now

static void setLED() {
	// {} KbdSetLED((_IMM(kbd_state.lock_caps) << 2) | (_IMM(kbd_state.lock_number) << 1) | _IMM(kbd_state.lock_scroll));
}

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
	else if (kbd_event.method == keyboard_event_t::method_t::keydown && (kbd_state.mod.l_logo || kbd_state.mod.r_logo)) {
		if (kbd_event.keycode == 0x06) { // Win + C
			// Global Hotkey: Win + C
			extern SpinlockBlock<uni::Queue<SysMessage>> message_queue_conv;
			SysMessage msg;
			msg.type = SysMessage::RUPT_NEW_TERM;
			message_queue_conv.Lock()->Enqueue(msg);
		}
	}
	else {
		if (kbd_event.method == keyboard_event_t::method_t::keydown) {
			auto ch = (kbd_state.mod.l_shift || kbd_state.mod.r_shift ? key_map_shift : key_map)[kbd_event.keycode];
			if (!ch && kbd_state.lock_number && kbd_event.keycode >= 0x59 && kbd_event.keycode <= 0x63) {
				// NumPad adjustments
				ch = key_map_shift[kbd_event.keycode];
			}
			// Check for Ctrl+C to trigger SIGINT (Non-GUI Mode, exclude Shift)
			#if !_GUI_ENABLE
			if ((kbd_state.mod.l_ctrl || kbd_state.mod.r_ctrl) && !kbd_state.mod.l_shift && !kbd_state.mod.r_shift && ch == 'c') {
				auto p_vtty = vttys[Consman::current_screen_TTY];
				if (p_vtty && p_vtty->type) {
					auto pblock = (vtty_type_t*)p_vtty->type;
					for (stduint idx = 0; idx < pblock->proc_group.Count(); idx++) {
						stduint target_pid = pblock->proc_group[idx];
						if (target_pid >= TaskCount && target_pid != pblock->master_pid) {
							sys_kill(target_pid, SIGINT, 0);
						}
					}
				}
				return;
			}
			#endif
			if (!Consman::ento_gui) {
				// Ctrl + letter -> control character
				if ((kbd_state.mod.l_ctrl || kbd_state.mod.r_ctrl) && !kbd_state.mod.l_shift && !kbd_state.mod.r_shift) {
					if (ch >= 'a' && ch <= 'z') {
						ch = ch - 'a' + 1;
					}
				}
				auto p_vtty = vttys[Consman::current_screen_TTY];
				if (auto* q = VTTY_INNQ(p_vtty)) {
					q->OutChar(ch);
				}
				return;
			}
		}
		if (asrtand(Consman::last_click_sheet)->refSheetNode().next) {
			Consman::last_click_sheet->onrupt(SheetEvent::onKeybd, Point(0, 0), &kbd_event);
		}
	}
}

void hand_kboard(keyboard_event_t  kmsg) {
	// Enqueue to message_queue_conv so serv_graf_loop (Graphic thread) processes
	// the event. This avoids racing with GraphicMsg::FDEL on last_click_sheet.
	extern SpinlockBlock<uni::Queue<SysMessage>> message_queue_conv;
	SysMessage msg;
	msg.type = SysMessage::RUPT_KBD;
	msg.args.kbd_event = kmsg;
	message_queue_conv.Lock()->Enqueue(msg);
}
#endif
