// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"


#if (_MCCA & 0xFF00) == 0x8600
Cursor* Cursor::global_cursor = nullptr;
SheetTrait* Cursor::moving_sheet = nullptr;
bool Cursor::mouse_btnl_dn = false;
bool Cursor::mouse_btnm_dn = false;
bool Cursor::mouse_btnr_dn = false;
// consider CLI
BareConsole Bcons[TTY_NUMBER];// TTY 0~3 and their buffer
// consider GUI
byte _BUF_cursor[byteof(Cursor)];
bool Consman::ento_gui = false;
bool Consman::enable_dubuffer = false;
VideoControlInterface* Consman::real_pvci = nullptr;

#ifndef _UEFI
GloScreenARGB8888 local_vci;
#else
GloScreenARGB8888 vga_ARGB8888;
GloScreenABGR8888 vga_ABGR8888;
#endif

OstreamTrait* con0_out = 0;
#endif


//// ---- ---- STATIC CORE ---- ---- ////
#if ((_MCCA & 0xFF00) == 0x8600)
LayerManager2 global_layman;
#if defined(_UEFI) && _MCCA == 0x8664
extern UefiData uefi_data;
#endif

#ifdef _ARC_x86 // x86:
static void InitializeBottomBar() {
	// BCON:
	struct element { byte ch; byte attr; };
	Letvar(Ribbon, element*, (_VIDEO_ADDR_BUFFER + 80 * 2 * 24));
	Ribbon[0].ch = '^';
	Ribbon[1].ch = '-';
	Ribbon[2].ch = '+';
	Ribbon[77].ch = '+';
	Ribbon[78].ch = '-';
	Ribbon[79].ch = '^';
}
#endif

::uni::Witch::Form* form2 _TEMP;

bool Consman::Initialize() {
	con0_out = 0;
	Bcons[0].Reset(bda->screen_columns, 24, _VIDEO_ADDR_BUFFER, 0 * 50); Bcons[0].setShowY(0, 24);
	for1(i, TTY_NUMBER - 1) {
		Bcons[i].Reset(bda->screen_columns, 50, _VIDEO_ADDR_BUFFER, i * 50); Bcons[i].setShowY(0, 25);
	}
	#ifdef _ARC_x86 // x86:
	InitializeBottomBar();
	#endif

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
	Consman::ento_gui = vmod_default;
	if (!vmod_default) {
		con0_out = &Bcons[0];
		Bcons[0].Scroll(24);
		for0a(i, Bcons) ttys.Append(dynamic_cast<Console_t*>(&Bcons[i]));
		for0a(i, Bcons) VTTY_Append((&Bcons[i]));
		plogwarn("There is no default 800xN-8888 Video Mode");
		return true;
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
	auto vcon0 = new VideoConsole2(&global_layman.getVCI(), screen0_win, Color::Black, Color::White);
	// auto vcon0_buf = (Color*)mem.allocate(vcon0_size);
	vcon0->setBuffers(nullptr,
		new BufferChar[vcon0->getCols() * vcon0->getRows()],
		new Color[vcon0->getLineBufferSize()]
	);
	vcon0->InitializeSheet(global_layman, screen0_win.getVertex(), screen0_win.getSize());
	VTTY_Append(vcon0);

	// cursor
	Cursor::global_cursor = new (_BUF_cursor)Cursor{ &global_layman.getVCI() };
	const Point cursor_pos = { 300,200 };
	Cursor::global_cursor->setSheet(global_layman, cursor_pos);

	if (_TEMP 1) {
		Rectangle rect{ Point(250, 160), Size2(480, 320) };
		auto [pcon, pf, tty_no] = CreateVconsole(rect, "Console-Gen2");
		form2 = pf;
	}// should follow 'init'

	global_layman.Append(vcon0);

	#if _GUI_DOUBLE_BUFFER
	Consman::enable_2buffer();
	#endif

	vcon0->Clear();
	con0_out = vcon0;
	current_screen_TTY = _TEMP 1;

	// default tty are all bcon
	return true;
}
#endif
