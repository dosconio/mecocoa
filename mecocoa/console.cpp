// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <cpp/Witch/Form.hpp>
#include <cpp/Witch/Control/Control-Label.hpp>
#include <cpp/Witch/Control/Control-TextBox.hpp>



#if (_MCCA & 0xFF00) == 0x8600

Cursor* Cursor::global_cursor = nullptr;
SheetTrait* Cursor::moving_sheet = nullptr;
bool Cursor::mouse_btnl_dn = false;
bool Cursor::mouse_btnm_dn = false;
bool Cursor::mouse_btnr_dn = false;

#endif


#if 1 // ---- ---- TTY ---- ----

// vtty0: global_ground
// vtty1: form1...
Dchain ttys = { nullptr };// offs->ConT*, type->B/V
static void VTTY_Free(pureptr_t inp) {
	Letvar(nod, Dnode*, inp);
	// skip offs
	auto& block = *(vtty_type_t*)nod->type;
	if (block.innput_queue.slice.address) {
		delete (byte*)block.innput_queue.slice.address;
	}
	if (block.output_queue.slice.address) {
		_TODO
	}
	memf((void*)nod->type);
}
Dnode* VTTY_Append(Console_t* con) {
	if (!con) {
		plogerro("%s: You may lose impl Console_t for con", __FUNCIDEN__);
		return nullptr;
	}
	auto p = vttys.Append(con);
	if (!p) return nullptr;
	const stduint SIZE_INN_BUF = 64;
	auto p_innbuf = new byte[SIZE_INN_BUF];
	p->type = _IMM(new vtty_type_t({ 0, QueueLimited({_IMM(p_innbuf), SIZE_INN_BUF}), QueueLimited({0, 0}) }));
	return p;
}
Dchain vttys = { VTTY_Free };// offs->ConT*, type->vtty_type_t

#endif

Vector<stduint> blocked_vtty_pid;
// total: may need change after Remove
unsigned current_screen_TTY = 0;

#if (_MCCA & 0xFF00) == 0x8600

// consider CLI
BareConsole Bcons[TTY_NUMBER];// TTY 0~3 and their buffer
// consider GUI
byte _BUF_cursor[byteof(Cursor)];
// global
bool ento_gui = false;
bool enable_dubuffer = false;
OstreamTrait* con0_out = 0;
#ifndef _UEFI
GloScreenARGB8888 local_vci;
#else
GloScreenARGB8888 vga_ARGB8888;
GloScreenABGR8888 vga_ABGR8888;
#endif
VideoControlInterface* real_pvci = nullptr;


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
#if 1

char* cons_buffer;
extern keyboard_state_t kbd_state;

static bool ifContainBlockedTTY(ProcessBlock* ppb) {
	for0(i, blocked_vtty_pid.Count()) {
		if (Taskman::Locate(blocked_vtty_pid[i])->focus_tty == ppb->focus_tty) {
			return true;
		}
	}
	return false;
}// To OPTIMIZE
void _Comment(R1) serv_cons_loop()
{
	#ifdef _ARC_x86 // x86:
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
	#endif

	volatile stduint sig_type = 0, sig_src, ret;
	volatile stduint to_args[4];

	int ch;

	while (true) {
		for0 (i, blocked_vtty_pid.Count()) if (stduint pid = blocked_vtty_pid[i]) {
			auto ppb = Taskman::Locate(pid);
			if (!ppb) {
				plogerro("%s", __FUNCIDEN__);
				continue;
			}
			if (ppb->focus_tty && -1 != (ch = VTTY_INNQ(ppb->focus_tty)->inn())) {
				stdsint val = ch; // The character is already translated through sysmsg_kbd
				blocked_vtty_pid.Remove(i);
				syssend(pid, &val, byteof(val));
			}
		}

		// Render the bottom ribbon
		#ifdef _ARC_x86 // x86:
		#if !_GUI_ENABLE
		if (!ento_gui && current_screen_TTY == 0) {
			Ribbon[0].attr = kbd_state.mod.l_ctrl ? 0x70 : 0x07;
			Ribbon[1].attr = kbd_state.mod.l_shift ? 0x70 : 0x07;
			Ribbon[2].attr = kbd_state.mod.l_alt ? 0x70 : 0x07;
			Ribbon[77].attr = kbd_state.mod.r_alt ? 0x70 : 0x07;
			Ribbon[78].attr = kbd_state.mod.r_shift ? 0x70 : 0x07;
			Ribbon[79].attr = kbd_state.mod.r_ctrl ? 0x70 : 0x07;
		}
		#endif
		#endif

		// Process potential message
		if (syscall(syscall_t::TMSG)) {
			sysrecv(ANYPROC, (void*)to_args, byteof(to_args), (usize*)&sig_type, (usize*)&sig_src);
			ProcessBlock* pb = Taskman::Locate(to_args[3]);
			switch (ConsoleMsg(sig_type)) {
			case ConsoleMsg::TEST: break;

			#ifdef _ARC_x86 // x86:
			case ConsoleMsg::READ:
				to_args[0] &= _IMM1S(dev_domain_bits) - 1;
				//{TODO}
				break;
			case ConsoleMsg::WRIT:
				ret = StrCopyP(cons_buffer, kernel_paging,
					(char*)to_args[1], pb->paging, to_args[2]);
				// if (get_drv_pid(to_args[0]) == 4)
				//{TODO} 0x1000 and to_args[2]
				if (((_IMM1S(dev_domain_bits) - 1) & to_args[0]) == 0) {
					
					// LocateTTY(0xFF & to_args[0])->out(cons_buffer, ret);
				}
				syssend(sig_src, (void*)&ret, sizeof(ret), 0);
				break;
			#endif

			case ConsoleMsg::INNC:
			{
				// ReadChar(ASCII): normal \n \r ...
				ProcessBlock* pb = Taskman::Locate(to_args[3]);
				if (!ifContainBlockedTTY(pb)) {
					blocked_vtty_pid.Append(pb->getID());
				}
				else {
					ret = ~_IMM0;
					syssend(sig_src, (void*)&ret, sizeof(ret), 0);
				}
				break;
			}

			


			default:
				plogerro("Unknown syscall type %u", sig_type);
				break;
			}
		}
		syscall(syscall_t::REST);
	}
}
#endif

//// ---- ---- Bottom Impl ---- ---- ////
#ifdef _ARC_x86 // x86:

void uni::BareConsole::doshow(void* _) {}

#endif

//// ---- ---- STATIC CORE ---- ---- ////
#if ((_MCCA & 0xFF00) == 0x8600)
LayerManager global_layman;
#if defined(_UEFI) && _MCCA == 0x8664
extern UefiData uefi_data;
#endif

::uni::Witch::Form form0, form1, form2;

uni::witch::control::Label* plabel_1;
uni::witch::control::TextBox* ptext_1;
VideoConsole* pcon_1;

void enable_2buffer() {
	// Double Buffer
	//{} put off
	#if _GUI_DOUBLE_BUFFER
	auto vcon0_size = global_layman.window.getArea() * sizeof(Color);
	global_layman.sheet_buffer = (Color*)mem.allocate(vcon0_size);
	if (!global_layman.sheet_buffer) {
		plogerro("Failed to allocate back buffer for Layman");
	} else {
		global_layman.sheet_area = global_layman.window;
		for0(i, global_layman.window.getArea()) {
			global_layman.sheet_buffer[i] = Color::Black;
		}
		// Redirect global_layman's VCI to point to our back-buffer
		extern VideoControlInterface* real_pvci;
		real_pvci = global_layman.pvci;
		// Allocate a static VCI for the back-buffer
		static byte _BUF_VCI[sizeof(VideoControlInterfaceMARGB8888)];
		VideoControlInterfaceMARGB8888* back_vci = new (_BUF_VCI) VideoControlInterfaceMARGB8888(global_layman.sheet_buffer, global_layman.window.getSize());
		global_layman.pvci = back_vci;
	}
	enable_dubuffer = true;
	#endif
}

void cons_init() {
	con0_out = 0;
	Bcons[0].Reset(bda->screen_columns, 24, _VIDEO_ADDR_BUFFER, 0 * 50); Bcons[0].setShowY(0, 24);
	for1(i, TTY_NUMBER - 1) {
		Bcons[i].Reset(bda->screen_columns, 50, _VIDEO_ADDR_BUFFER, i * 50); Bcons[i].setShowY(0, 25);
	}

	// try first 800xN ARGB8888 Mode
	uint16 vmod_default = nil;
	#if !defined(_UEFI)
	call_ladder(R16FN_LSVM);// list video modes
	for (auto vie = (VideoInfoEntry*)0x78000; _IMM(vie) < 0x80000; vie++) {
		#if !_GUI_ENABLE
		break;
		#endif
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
		con0_out = &Bcons[0];
		Bcons[0].Scroll(24);
		for0a(i, Bcons) ttys.Append(dynamic_cast<Console_t*>(&Bcons[i]));
		for0a(i, Bcons) VTTY_Append((&Bcons[i]));
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
	case PixelFormat::ARGB8888: screen = &vga_ARGB8888; break;
	case PixelFormat::ABGR8888: screen = &vga_ABGR8888; break;
	default:
		loop HALT();
	}
	global_layman.Reset(screen, screen0_win);
	global_layman.video_mode = vmod_default;
	global_layman.video_memory = (stduint)uefi_data.frame_buffer_config.frame_buffer;
	global_layman.pixel_fmt = uefi_data.frame_buffer_config.pixel_format;
	#endif
	const stduint vcon0_size = global_layman.window.getArea() * sizeof(Color);
	//
	#if _MCCA == 0x8632
	kernel_paging.Map(
		global_layman.video_memory,
		global_layman.video_memory,
		vcon0_size,
		PAGESIZE_4KB, PGPROP_present | PGPROP_writable
	);// VGA
	#endif

	// main screen
	auto vcon0 = new VideoConsole(&global_layman.getVCI(), screen0_win, Color::Black, Color::White);
	auto vcon0_buf = (Color*)mem.allocate(vcon0_size);
	vcon0->InitializeSheet(global_layman, screen0_win.getVertex(), screen0_win.getSize(), vcon0_buf);
	vcon0->setModeBuffer(vcon0_buf);
	VTTY_Append(vcon0);

	// cursor
	Cursor::global_cursor = new (_BUF_cursor)Cursor{ &global_layman.getVCI() };
	const Point cursor_pos = { 300,200 };
	Cursor::global_cursor->setSheet(global_layman, cursor_pos);

	// [demo] window
	if (1) {
		Rectangle rect{ Point(200, 40), Size2(160, 80) };
		// Label
		auto plabel = new uni::witch::control::Label("QwQ~");
		plabel->sheet_area = Rectangle(Point(0, 0), Size2(8 * 5, 16));
		plabel->doshow(0);
		plabel_1 = plabel;

		form0.Title = "Ciallo~>v<";// new (&form0.Title) String((char*)form0_title_text, sizeof(form0_title_text));
		form0.AppendControl(plabel);
		form0.setSheet(global_layman, rect, (Color*)mem.allocate(rect.getArea() * sizeof(Color)));
		global_layman.Append(&form0);
	}
	if (1) {
		Rectangle rect{ Point(400, 40), Size2(160, 80) };
		auto ptext = new uni::witch::control::TextBox();
		ptext->sheet_area = Rectangle(Point(2, 2), Size2(8 * 18, 25));
		ptext->doshow(0);
		ptext_1 = ptext;

		form1.Title = "Test TextBox";
		form1.AppendControl(ptext);
		form1.setSheet(global_layman, rect, (Color*)mem.allocate(rect.getArea() * sizeof(Color)));
		form1.setFocus(ptext);
		global_layman.Append(&form1);
		ptext->Start();
	}
	if (1) {
		Rectangle rect{ Point(250, 160), Size2(480, 320) };
		auto pcon = new VideoConsole(NULL,
			Rectangle(Point(2, 2), Size2(470, 290)),
				Color::White, Color::Black
		);
		auto vcon_buf = (Color*)mem.allocate(pcon->window.getArea() * sizeof(Color));
		pcon->InitializeSheet(global_layman, pcon->window.getVertex(), pcon->window.getSize(), vcon_buf);
		pcon->setModeBuffer(vcon_buf);
		pcon_1 = pcon;
		pcon->Clear();

		form2.Title = "Console";
		form2.AppendControl(pcon);
		form2.setSheet(global_layman, rect, (Color*)mem.allocate(rect.getArea() * sizeof(Color)));
		form2.setFocus(pcon);
		global_layman.Append(&form2);
		pcon->Start();

		VTTY_Append((pcon));
	}

	global_layman.Append(vcon0);

	#if _GUI_DOUBLE_BUFFER
	enable_2buffer();
	#endif

	vcon0->Clear();
	con0_out = vcon0;
	current_screen_TTY = _TEMP 1;

	// default tty are all bcon
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


