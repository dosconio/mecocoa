// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST

#include <c/consio.h>
#include <c/driver/mouse.h>
#include <c/driver/keyboard.h>
#include <cpp/Device/_Video.hpp>
use crate uni;
#ifdef _ARC_x86 // x86:
#include "../include/atx-x86-flap32.hpp"
#elif _MCCA == 0x8664
#include "../include/atx-x64.hpp"
#endif
#include "../include/console.hpp"

Cursor* Cursor::global_cursor = nullptr;

#if (_MCCA & 0xFF00) == 0x8600

// ---- ---- TTY ---- ----

// each barecon: ----
static bool last_E0s[TTY_NUMBER] = { false, false, false, false };
// each con:
typedef char innQueueBuf[64];
static innQueueBuf _BUF_innQueues[TTY_NUMBER];
stduint tty_crt_blocked_appid[TTY_NUMBER];// 0 if no app
// total:
byte current_screen_TTY = 0;
// consider CLI
BareConsole Bcons[TTY_NUMBER];// TTY 0~3 and their buffer
// consider GUI
byte BUF_CONS0[sizeof(VideoConsole)]; VideoConsole* vcon0;
VideoConsole* vcons[TTY_NUMBER];
byte _BUF_cursor[byteof(Cursor)];
// global
bool ento_gui = false;
OstreamTrait* con0_out;
#ifndef _UEFI
GloScreenARGB8888 local_vci;
#else
GloScreenARGB8888 vga_ARGB8888;
GloScreenABGR8888 vga_ABGR8888;
#endif
byte _BUF_QueueMouse[byteof(QueueLimited)];

unsigned IndexTTY(pureptr_t addr) {
	for0a(i, Bcons) {
		if (&Bcons[i] == addr) return i;
	}
	for0a(i, vcons) {
		if (vcons[i] && vcons[i] == addr) return i;
	}
	return TTY_NUMBER;// fail
}


#ifndef _UEFI
void blink() {
	static bool b = false;
	local_vci.DrawRectangle(Rectangle(Point(0, 0), Size2(8, 16), b ? Color::Black : Color::White));
	b = !b;
}
void blink2() {
	static bool b = false;
	local_vci.DrawRectangle(Rectangle(Point(8, 0), Size2(8, 16), b ? Color::Black : Color::White));
	b = !b;
}
#endif

#endif


//// ---- ---- DYNAMIC CORE ---- ---- ////
#ifdef _ARC_x86 // x86:

static stduint tty_parse(stduint tty_id, byte keycode, keyboard_state_t state, OstreamTrait* ttyout) { // // scan code set 1
	BareConsole* ttycon = &Bcons[tty_id];
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
extern keyboard_state_t kbd_state;
inline static OstreamTrait* LocateTTY(stduint tty_id) {
	OstreamTrait* ttyout = ento_gui ? dynamic_cast<OstreamTrait*>(vcons[tty_id]) : dynamic_cast<OstreamTrait*>(&Bcons[tty_id]);
	return ttyout;
}
void _Comment(R1) serv_cons_loop()
{
	cons_buffer = (char*)Memory::physical_allocate(0x1000);
	// mouse_buf[3] = 0;
	// BCON:
	struct element { byte ch; byte attr; };
	Letvar(Ribbon, element*, (_VIDEO_ADDR_BUFFER + 80 * 2 * 24));
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
		for0a(i, tty_crt_blocked_appid) if (-1 != (ch = Bcons[i].input_queue.inn())) if (stduint pid = tty_crt_blocked_appid[i]) {
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

void uni::BareConsole::doshow(void* _) {}

#endif

//// ---- ---- STATIC CORE ---- ---- ////
LayerManager global_layman;
#if ((_MCCA & 0xFF00) == 0x8600)
#if defined(_UEFI) && _MCCA == 0x8664
extern UefiData uefi_data;
#endif
void cons_init() {
	Bcons[0].Reset(bda->screen_columns, 24, _VIDEO_ADDR_BUFFER, 0 * 50); Bcons[0].setShowY(0, 24);
	con0_out = &Bcons[0];
	for1(i, TTY_NUMBER - 1) {
		Bcons[i].Reset(bda->screen_columns, 50, _VIDEO_ADDR_BUFFER, i * 50); Bcons[i].setShowY(0, 25);
	}
	for0(i, TTY_NUMBER) {
		new (&Bcons[i].input_queue) QueueLimited((Slice) { _IMM(_BUF_innQueues[i]), byteof(_BUF_innQueues[i]) });
	}

	// try first 800xN ARGB8888 Mode
	uint16 vmod_default = nil;
	#if !defined(_UEFI)
	call_ladder(R16FN_LSVM);// list video modes
	for (auto vie = (VideoInfoEntry*)0x78000; _IMM(vie) < 0x80000; vie++) {
		if (!vie->mode) break;
		// ploginfo("mode %[16H], %ux%u, ARGB:%[16H]", vie->mode, vie->width, vie->height, vie->bitmode);
		if (vie->bitmode == 0x8888 && vie->width == 800) {
			vmod_default = vie->mode;
			break;
		}
	}
	#else
	vmod_default = 0xFFFF;
	#endif
	ento_gui = vmod_default;
	if (!vmod_default) {
		Bcons[0].Scroll(24);
		plogwarn("There is no default 800xN-8888 Video Mode");
		return;
	}

	// config layman
	#if !defined(_UEFI)
	auto addr = (ModeInfoBlock*)_IMM(call_ladder(R16FN_VMOD, vmod_default));
	Rectangle screen0_win{ Point(0,0), Size2(addr->XResolution, addr->YResolution), Color::Black };
	global_layman.Reset(&local_vci, screen0_win);
	global_layman.video_mode = vmod_default;
	global_layman.video_memory = addr->PhysBasePtr;
	global_layman.pixel_fmt = PixelFormat::ARGB8888;
	#else
	Rectangle screen0_win{ Point(0,0), Size2(uefi_data.frame_buffer_config.horizontal_resolution, uefi_data.frame_buffer_config.vertical_resolution), Color::Black };
	VideoControlInterface* screen;
	switch (uefi_data.frame_buffer_config.pixel_format) {
	case PixelFormat::ARGB8888: screen = &vga_ARGB8888;
		new (screen) GloScreenARGB8888();
		break;
	case PixelFormat::ABGR8888: screen = &vga_ABGR8888;
		new (screen) GloScreenABGR8888();
		break;
	default:
		loop HALT();
	}
	global_layman.Reset(screen, screen0_win);
	global_layman.video_mode = vmod_default;
	global_layman.video_memory = (stduint)uefi_data.frame_buffer_config.frame_buffer;
	global_layman.pixel_fmt = uefi_data.frame_buffer_config.pixel_format;
	#endif
	const stduint vcon0_size = global_layman.window.getArea() * sizeof(Color);
	//{TODO} Mapping
	#if _MCCA == 0x8632
	kernel_paging.MapWeak(
		global_layman.video_memory,
		global_layman.video_memory,
		vcon0_size,
		true, _Comment(R0) false
	);// VGA
	#endif

	// cursor
	Cursor::global_cursor = new (_BUF_cursor)Cursor{ &global_layman.getVCI() };
	const Point cursor_pos = { 300,200 };
	Cursor::global_cursor->setSheet(global_layman, cursor_pos);

	// vcon0
	vcon0 = new (BUF_CONS0) VideoConsole(&global_layman.getVCI(), screen0_win);
	vcon0->backcolor = Color::White;
	vcon0->forecolor = Color::Black;
	auto vcon0_buf = (Color*)mem.allocate(vcon0_size);
	global_layman.Append(vcon0);
	vcon0->InitializeSheet(global_layman, screen0_win.getVertex(), screen0_win.getSize(), vcon0_buf);
	vcon0->setModeBuffer(vcon0_buf);
	vcon0->Clear();
	con0_out = vcons[0] = vcon0;
}
/* 4 BCON TTY

void uni::BareConsole::doshow(void *_) {
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

*/

#endif


