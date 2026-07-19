// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include "c/driver/UART.h"


#if (_MCCA & 0xFF00) == 0x8600
Cursor* Cursor::global_cursor = nullptr;
SheetTrait* Cursor::moving_sheet = nullptr;
bool Cursor::mouse_btnl_dn = false;
bool Cursor::mouse_btnm_dn = false;
bool Cursor::mouse_btnr_dn = false;
#endif
// consider CLI: No Remove
unsigned Consman::current_screen_TTY = 0;
#if (_MCCA & 0xFF00) == 0x8600
BareConsole Bcons[TTY_NUMBER];// TTY 0~3 and their buffer
ProcessBlock* Bcons_pcot[TTY_NUMBER] = {};
// consider GUI
byte _BUF_cursor[byteof(Cursor)];
bool Consman::ento_gui = false;
bool Consman::enable_dubuffer = false;
SheetTrait* Consman::last_click_sheet = nullptr;// mark the focused window
VideoDevice* Consman::current_video_device = nullptr;
VideoControlInterface* Consman::real_pvci = nullptr;


OstreamTrait* con0_out = 0;
#endif

extern "C" void R_CLASSIC_VIDEO_INIT();
FramebufferInfo sys_framebuffer;

uni::VideoConsole2* global_vcon0 = nullptr;


//// ---- ---- STATIC CORE ---- ---- ////
#if ((_MCCA & 0xFF00) == 0x8600)
SpinlockBlock<LayerManager2> global_layman;
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


#if _MCCA == 0x8632
static uni::BitmapFontEngine fallback_engine(1);
#else
static uni::BitmapFontEngine loader_font_engine(1);
#endif

extern x86_COM com1;
bool Consman::Initialize() {
	// con0_out = 0;
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
		// con0_out = &Bcons[0];
		Bcons[0].Scroll(24);
		for0a(i, Bcons) ttys.Append(dynamic_cast<Console_t*>(&Bcons[i]));
		for0a(i, Bcons) VTTY_Append((&Bcons[i]));
		plogwarn("There is no default 800xN-8888 Video Mode");
		return true;
	}
	else VTTY_Append(&com1);

	// config layman
	#if !defined(_UEFI)
	auto addr = (ModeInfoBlock*)_IMM(call_ladder(R16FN_VMOD, vmod_default));
	sys_framebuffer.physical_range = Slice{ addr->PhysBasePtr, (stduint)addr->YResolution * addr->BytesPerScanLine };
	sys_framebuffer.screen_size = Size2(addr->XResolution, addr->YResolution);
	sys_framebuffer.pitch = addr->BytesPerScanLine;
	sys_framebuffer.bpp = addr->BitsPerPixel;
	sys_framebuffer.format = PixelFormat::ARGB8888; // Default
	#else
	sys_framebuffer.physical_range = Slice{ (stduint)uefi_data.frame_buffer_config.frame_buffer, uefi_data.frame_buffer_config.vertical_resolution * uefi_data.frame_buffer_config.pixels_per_scan_line * 4 };
	sys_framebuffer.screen_size = Size2(uefi_data.frame_buffer_config.horizontal_resolution, uefi_data.frame_buffer_config.vertical_resolution);
	sys_framebuffer.pitch = uefi_data.frame_buffer_config.pixels_per_scan_line * 4;
	sys_framebuffer.bpp = 32;
	sys_framebuffer.format = uefi_data.frame_buffer_config.pixel_format;
	#endif

	Rectangle screen0_win{ Point(0,0), sys_framebuffer.screen_size, Color::Black };
	R_CLASSIC_VIDEO_INIT();
	// Register 'classic-video' and let its starter convert sys_framebuffer into a VideoDevice.
	DeviceNode* fb_node = Devsman::RegisterPlatformDevice("video-classic", "classic-video-driver", &sys_framebuffer);
	VideoDevice* screen = fb_node && fb_node->fields.binding.driver_data ?
		static_cast<VideoDevice*>(fb_node->fields.binding.driver_data) : nullptr;
	if (!screen) {
		loop HALT();
	}
	Consman::AdoptVideoDevice(screen);
	global_layman.Lock()->video_mode = vmod_default;
	


	stduint vcon0_size = 0, video_memory = 0;
	{
		auto layman = global_layman.Lock();
		vcon0_size = layman->window.getArea() * sizeof(Color);
		video_memory = layman->video_memory;
	}
	//
	#if _MCCA == 0x8632
	kernel_paging.Map(
		video_memory,
		video_memory,
		vcon0_size,
		PAGESIZE_4KB, PGPROP_present | PGPROP_writable
	);// VGA
	#endif

	// main screen
	auto vcon0 = new VideoConsole2(&global_layman.Lock()->getVCI(), screen0_win, Color::Black, Color::White);
	global_vcon0 = vcon0;

	#if _MCCA == 0x8632
	vcon0->setFontEngine(&fallback_engine);
	#else
	vcon0->setFontEngine(&loader_font_engine);
	#endif

	vcon0->setBuffers(nullptr,
		new BufferChar[vcon0->getCols() * vcon0->getRows()],
		new Color[vcon0->getLineBufferSize()]
	);
	vcon0->InitializeSheet(*global_layman.Lock(), screen0_win.getVertex(), screen0_win.getSize());
	VTTY_Append(vcon0);

	// cursor
	Cursor::global_cursor = new (_BUF_cursor)Cursor{ &global_layman.Lock()->getVCI() };
	const Point cursor_pos = { 300,200 };
	Cursor::global_cursor->setSheet(*global_layman.Lock(), cursor_pos);

	global_layman.Lock()->Append(vcon0);

	#if _GUI_DOUBLE_BUFFER
	Consman::enable_2buffer();
	#endif

	vcon0->Clear();
	// con0_out = vcon0;

	// default tty are all bcon
	return true;
}

void Consman_InitializeFreeType() {
#if _MCCA == 0x8632
	ploginfo("Consman_InitializeFreeType: [Start]");
	extern bool InitializeFont();
	extern uni::FontEngine* global_ft_engine;
	bool success = InitializeFont();
	ploginfo("Consman_InitializeFreeType: InitializeFont returned %d", success);
	ploginfo("Consman_InitializeFreeType: global_ft_engine = %p, global_vcon0 = %p", global_ft_engine, global_vcon0);
	if (global_ft_engine && global_vcon0) {
		ploginfo("Consman_InitializeFreeType: Setting font engine...");
		global_vcon0->setFontEngine(global_ft_engine);
		ploginfo("Consman_InitializeFreeType: Clearing screen...");
		global_vcon0->Clear();
		ploginfo("Consman_InitializeFreeType: [Successful End]");
	} else {
		ploginfo("Consman_InitializeFreeType: Deferring check failed (missing engine or vcon0)");
	}
#endif
}
#endif
