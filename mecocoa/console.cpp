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
#include <c/driver/mouse.h>
#include <c/driver/keyboard.h>

use crate uni;
#ifdef _ARC_x86 // x86:
#include "../include/atx-x86-flap32.hpp"
#include "../include/console.hpp"

// ---- ---- TTY ---- ----

// each barecon: ----
static bool last_E0s[TTY_NUMBER] = { false, false, false, false };
// each con:
typedef char innQueueBuf[64];
static innQueueBuf _BUF_innQueues[TTY_NUMBER];
stduint tty_crt_blocked_appid[4];// 0 if no app
// total:
static byte current_screen_TTY = 0;
// consider CLI
byte BUF_BCONS0[byteof(BareConsole)]; BareConsole* BCONS0;// TTY0
byte BUF_BCONS1[byteof(BareConsole)]; static BareConsole* BCONS1;// TTY1
byte BUF_BCONS2[byteof(BareConsole)]; static BareConsole* BCONS2;// TTY2
byte BUF_BCONS3[byteof(BareConsole)]; static BareConsole* BCONS3;// TTY3
BareConsole* bcons[TTY_NUMBER];
// consider GUI
byte BUF_VCI[sizeof(GloScreen)];
byte BUF_CONS0[sizeof(VideoConsole)]; VideoConsole* vcon0;
VideoConsole* vcons[TTY_NUMBER];
// consider CLI and GUI
OstreamTrait* con0_out;
OstreamTrait* tty0_out;
OstreamTrait* tty1_out;
OstreamTrait* tty2_out;
OstreamTrait* tty3_out;
unsigned IndexTTY(pureptr_t addr) {
	for0a(i, bcons) {
		if (bcons[i] && bcons[i] == addr) return i;
	}
	for0a(i, vcons) {
		if (vcons[i] && vcons[i] == addr) return i;
	}
	return TTY_NUMBER;// fail
}
inline static OstreamTrait* LocateTTY(stduint tty_id) {
	OstreamTrait* ttyout = ento_gui ? dynamic_cast<OstreamTrait*>(vcons[tty_id]) : dynamic_cast<OstreamTrait*>(bcons[tty_id]);
	return ttyout;
}
// ---- ---- ---- ----

extern QueueLimited* queue_mouse;


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
				bcons[0]->doshow(); // TTY0 ESC
				break;
			case 0x3B:
				bcons[1]->doshow(); // TTY1 F1
				break;
			case 0x3C:
				bcons[2]->doshow(); // TTY2 F2
				break;
			case 0x3D:
				bcons[3]->doshow(); // TTY3 F3
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
				bcons[current_screen_TTY]->input_queue.OutChar(ch);
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

byte _BUF_QueueMouse[byteof(QueueLimited)];
//// ---- ---- STATIC CORE ---- ---- ////

void cons_init()
{
	// Manually Initialize
	for0a(i, last_E0s) {
		last_E0s[i] = false;
	}
	void* pointer_mouse = Memory::physical_allocate(0x1000);
	Slice mouse_slice{ _IMM(pointer_mouse), byteof(0x1000) };
	queue_mouse = new (_BUF_QueueMouse) QueueLimited(mouse_slice);

	video_info = (uni::ModeInfoBlock*)Memory::physical_allocate(0x1000);
	BCONS0 = new (BUF_BCONS0) BareConsole(bda->screen_columns, 24, 0xB8000, 0 * 50); BCONS0->setShowY(0, 24);
	BCONS1 = new (BUF_BCONS1) BareConsole(bda->screen_columns, 50, 0xB8000, 1 * 50); BCONS1->setShowY(0, 25);
	BCONS2 = new (BUF_BCONS2) BareConsole(bda->screen_columns, 50, 0xB8000, 2 * 50); BCONS2->setShowY(0, 25);
	BCONS3 = new (BUF_BCONS3) BareConsole(bda->screen_columns, 50, 0xB8000, 3 * 50); BCONS3->setShowY(0, 25);
	
	bcons[0] = BCONS0; new (&BCONS0->input_queue) QueueLimited((Slice) { _IMM(_BUF_innQueues[0]), byteof(_BUF_innQueues[0]) });
	bcons[1] = BCONS1; new (&BCONS1->input_queue) QueueLimited((Slice) { _IMM(_BUF_innQueues[1]), byteof(_BUF_innQueues[1]) });
	bcons[2] = BCONS2; new (&BCONS2->input_queue) QueueLimited((Slice) { _IMM(_BUF_innQueues[2]), byteof(_BUF_innQueues[2]) });
	bcons[3] = BCONS3; new (&BCONS3->input_queue) QueueLimited((Slice) { _IMM(_BUF_innQueues[3]), byteof(_BUF_innQueues[3]) });
	//
	for0a(i, vcons) {
		vcons[i] = NULL;
	}
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
	vcons[0] = vcon0;
	//
	con0_out = vcon0;
	vcon0->forecolor = Color::Black;
	vcon0->Clear();
	Console.OutFormat("\xFF\x70[Mecocoa]\xFF\x27 Real16 Switched Test OK!\xFF\x07\n\r");
	
	// TTY1 window form (TODO)
	// TTY2 window form (TODO)
	// TTY3 window form (TODO)

	

}




//// ---- ---- DYNAMIC CORE ---- ---- ////

static stduint tty_parse(stduint tty_id, byte keycode, keyboard_state_t state, OstreamTrait* ttyout) { // // scan code set 1
	BareConsole* ttycon = bcons[tty_id];
	// OstreamTrait* ttyout = LocateTTY(tty_id);
	if (last_E0s[tty_id]) {
		if (!ento_gui && keycode == 0x49 && ttycon->crtline > 0) { // PgUp
			ttycon->auto_incbegaddr = 0;
			ttycon->setStartLine(--ttycon->crtline + ttycon->topline);
		}//{TODO} Adapt Vcon
		else if (!ento_gui && keycode == 0x51 && ttycon->crtline < ttycon->area_total.y - ttycon->area_show.height) { // PgDn
			ttycon->auto_incbegaddr = 0;
			ttycon->setStartLine(++ttycon->crtline + ttycon->topline);
		}//{TODO} Adapt Vcon
		else if (keycode == 0x35) // Pad /
		{
			return '/';//ttyout->OutChar('/');
		}
		else if (keycode == 0x1C) // Pad ENTER
		{
			//ttyout->OutChar('\n');
			//ttyout->OutChar('\r');
			return '\n';// process as \n\r
		}
	}
	else if (Rangein(keycode, 0x47, 0x54)) {
		byte c = (state.lock_number) ? _tab_keycode2ascii[keycode].ascii_shift : _tab_keycode2ascii[keycode].ascii_usual;
		if (c > 1) return c;// ttyout->OutChar(c);
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

			return c;//ttyout->OutChar(c);
			//if (c == '\n') ttyout->OutChar('\r');
			//else if (c == '\b') {
			//	ttyout->OutChar(' ');
			//	ttyout->OutChar('\b');
			//}
			//{TODO} Menus, TAB, Arrows, PrtSc, Pause, Ins, Home, Del, End
		}
	}
	last_E0s[tty_id] = keycode == 0xE0;
	return 0x80;
}

bool work_console = false;
char* cons_buffer;
// static byte mouse_buf[4];
void _Comment(R1) serv_cons_loop()
{
	cons_buffer = (char*)Memory::physical_allocate(0x1000);
	// mouse_buf[3] = 0;
	// BCON:
	struct element { byte ch; byte attr; };
	Letvar(Ribbon, element*, (0xB8000 + 80 * 2 * 24));
	Ribbon[0].ch = '^';
	Ribbon[1].ch = '-';
	Ribbon[2].ch = '+';
	Ribbon[77].ch = '+';
	Ribbon[78].ch = '-';
	Ribbon[79].ch = '^';

	stduint sig_type = 0, sig_src, ret;
	stduint to_args[4];

	int ch;

	work_console = true;

	//{TEMP} only a TTY0(VCON)
	using BR = ProcessBlock::BlockReason;
	while (true) {
		// if (queue_mouse && !queue_mouse->is_empty()) {
		// 	while ((ch = queue_mouse->inn()) >= 0) {
		// 		mouse_buf[mouse_buf[3]++] = ch;
		// 		mouse_buf[3] %= 3;
		// 		if (!mouse_buf[3]) {
		// 			sizeof(MouseMessage);// 3
		// 			outsfmt(" %[8H]-%[8H]-%[8H] ", mouse_buf[0], mouse_buf[1], mouse_buf[2]);
		// 			MouseMessage& mm = *(MouseMessage*)mouse_buf;
		// 			if (!ento_gui) {
		// 				// Ribbon[3].ch = !mm.X ? ' ' : mm.DirX ? '<' : '>';
		// 				if (mm.X) Ribbon[3].ch = mm.DirX ? '<' : '>';
		// 				// Ribbon[4].ch = !mm.Y ? ' ' : mm.DirY ? '^' : 'v';
		// 				if (mm.Y) Ribbon[4].ch = !mm.DirY ? '^' : 'v';
		// 				Ribbon[5].ch = mm.BtnLeft ? 'L' : 'l';
		// 				Ribbon[6].ch = mm.BtnMiddle ? 'M' : 'm';
		// 				Ribbon[7].ch = mm.BtnRight ? 'R' : 'r';
		// 			}
		// 		}
		// 	}
		// }
		// for0(i, ento_gui ? 1 : 4) while (-1 != (ch = bcons[i]->input_queue.inn())) tty_parse(i, ch, kbd_state, &console_agents[i]);
		for0a(i, tty_crt_blocked_appid) if (-1 != (ch = bcons[i]->input_queue.inn())) if (stduint pid = tty_crt_blocked_appid[i]) {
			if (ch <= 0 || ch >= 0x80) break;
			if (_IMM(TaskGet(pid)->block_reason) && _IMM(BR::BR_exInnc)) {
				TaskGet(pid)->Unblock(BR::BR_exInnc);
				
				tty_crt_blocked_appid[i] = nil;
				stdsint val = tty_parse(i, ch, kbd_state, NULL);
				syssend(pid, &val, byteof(val));
			}
			else plogerro("error! %s %u", __FUNCIDEN__, __LINE__);
		}

		// Render the bottom ribbon
		if (!ento_gui && current_screen_TTY == 0) {
			Ribbon[0].attr = kbd_state.l_ctrl ? 0x70 : 0x07;
			Ribbon[1].attr = kbd_state.l_shift ? 0x70 : 0x07;
			Ribbon[2].attr = kbd_state.l_alt ? 0x70 : 0x07;
			Ribbon[77].attr = kbd_state.r_alt ? 0x70 : 0x07;
			Ribbon[78].attr = kbd_state.r_shift ? 0x70 : 0x07;
			Ribbon[79].attr = kbd_state.r_ctrl ? 0x70 : 0x07;
		}
		// Process potential message
		if (syscall(syscall_t::TMSG)) {
			sysrecv(ANYPROC, to_args, byteof(to_args), &sig_type, &sig_src);
			ProcessBlock* pb = TaskGet(to_args[3]);
			switch (sig_type) {
			case 0: break;
			case 1: // R (dev, addr, len, pid)
				to_args[0] &= _IMM1S(dev_domain_bits) - 1;
				//{TODO}
				break;
			case 2: // W (dev, addr, len, pid)
				ret = StrCopyP(cons_buffer, kernel_paging,
					(char*)to_args[1], pb->paging, to_args[2]);
				// if (get_drv_pid(to_args[0]) == 4)
				//{TODO} 0x1000 and to_args[2]
				if (((_IMM1S(dev_domain_bits) - 1) & to_args[0]) == 0) {
					
					LocateTTY(0xFF & to_args[0])->out(cons_buffer, ret);
				}
				syssend(sig_src, &ret, sizeof(ret), 0);
				break;
			case 3: // (dev,.,., pid) noreturn
				// ReadChar(ASCII): normal \n \r ...
				to_args[0] &= _IMM1S(dev_domain_bits) - 1;
				if (!tty_crt_blocked_appid[to_args[0]]) {
					tty_crt_blocked_appid[to_args[0]] = to_args[3];
					pb->Block(BR::BR_exInnc);
				}
				else {
					ret = ~_IMM0;
					syssend(sig_src, &ret, sizeof(ret), 0);
				}
				break;
			default:
				plogerro("Unknown syscall type %u", sig_type);
				break;
			}
		}
		syscall(syscall_t::REST);
	}
}

//// ---- ---- Bottom Impl ---- ---- ////

void uni::BareConsole::doshow() {
	unsigned id = IndexTTY(this);
	if (id == current_screen_TTY) return;
	if (id > 3) {
		plogerro("TTY Id %u", id);
		return;
	}
	bcons[current_screen_TTY]->last_curposi = curget();
	bcons[id]->setStartLine(topline + crtline);
	current_screen_TTY = id;
	curset(bcons[id]->last_curposi);
	//
	for0(i, 4) bcons[i]->auto_incbegaddr = false;
	bcons[id]->auto_incbegaddr = true;
}

#endif
