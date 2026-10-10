// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: 
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include "c/driver/UART.h"

#if (_MCCA & 0xFF00) == 0x8600
#ifdef _UEFI
extern UefiData uefi_data;

#include <cpp/Witch/TextChrome.hpp>
#include "../../depends/desktop.hpp"

auto ProbeLegacyUart(stduint base) -> bool;

// ScreenLog: console fallback shown when no COM1 answers, driven by the kernel's own VideoConsole2
#ifdef _UEFI_AUFDBG
enum { ScreenLogRows = 40 };
static FramebufferInfo screen_log_fb;
static uni::ScreenBridge screen_log_bridge(screen_log_fb);
static uni::BitmapFontEngine screen_log_font(1);
static Rectangle screen_log_win{ Point(0, 0), Size2(0, 0), Color::Black };
uni::VideoConsole2 screen_log_console(&screen_log_bridge, screen_log_win, Color::Green, Color::Black);
static uni::BufferChar* screen_log_text = nullptr;
static Color* screen_log_line = nullptr;
static Color* screen_log_pixels = nullptr;
static bool screen_log_on_layer = false;
static ::uni::Witch::Form* screen_log_form = nullptr;

// Swallows the client-area events of the log window, so a click never reaches the console.
struct ScreenLogBlankSheet : public SheetTrait {
	virtual void doshow(void*) override {}
	virtual void onrupt(uni::SheetEvent, Point, ...) override {}
};
static ScreenLogBlankSheet screen_log_blank;

void ScreenLogErr() {
	screen_log_console.update_method = 0;
}
Rectangle ScreenLogGetWindow() {
	return screen_log_win;
}

// the console rasterizes into RAM; push the used rows to the framebuffer in one sequential pass
static void ScreenLogPush() {
	if (screen_log_on_layer || !screen_log_pixels) return;
	stduint used = (stduint)screen_log_console.getCursor().y + 1;
	if (used > screen_log_console.getRows()) used = screen_log_console.getRows();
	if (!used) return;
	Rectangle rect{ Point(0, 0), Size2(screen_log_win.width, used * 16), Color::Black };
	screen_log_bridge.DrawPoints(rect, screen_log_pixels);
}

class ScreenLogSink : public OstreamTrait {
public:
	virtual int out(const char* str, stduint len) override {
		const int res = screen_log_console.out(str, len);
		ScreenLogPush();
		return res;
	}
};
static ScreenLogSink screen_log_sink;

_ESYM_C void ScreenLogAttach() {
	if (ProbeLegacyUart(PORT_COM1_DATA)) return;	// a real COM1 keeps the UART console
	if (con0_out == &screen_log_sink) return;
	auto& cfg = uefi_data.frame_buffer_config;
	screen_log_fb.physical_range = Slice{ (stduint)cfg.frame_buffer, cfg.vertical_resolution * cfg.pixels_per_scan_line * 4 };
	screen_log_fb.screen_size = Size2(cfg.horizontal_resolution, cfg.vertical_resolution);
	screen_log_fb.pitch = cfg.pixels_per_scan_line * 4;
	screen_log_fb.bpp = 32;
	screen_log_fb.format = cfg.pixel_format;
	stduint rows = screen_log_fb.screen_size.y / 16;
	if (rows > ScreenLogRows) rows = ScreenLogRows;
	screen_log_win.width = screen_log_fb.screen_size.x;	// full width: DrawPoints steps the source by the screen width
	screen_log_win.height = rows * 16;
	// a=1 still fills each cell but blends away, so the desktop shows through the layer
	Color log_bg = Color::Black;
	log_bg.a = 1;
	screen_log_win.color = log_bg;
	screen_log_console.backcolor = log_bg;
	screen_log_console.window = screen_log_win;
	screen_log_console.setFontEngine(&screen_log_font);
	if (!screen_log_text) screen_log_text = new uni::BufferChar[screen_log_console.getCols() * screen_log_console.getRows()];
	if (!screen_log_line) screen_log_line = new Color[screen_log_console.getLineBufferSize()];
	if (!screen_log_pixels) screen_log_pixels = new Color[screen_log_win.getArea()];
	if (!screen_log_text || !screen_log_line || !screen_log_pixels) return;
	screen_log_console.setBuffers(screen_log_pixels, screen_log_text, screen_log_line);
	screen_log_console.Clear();
	screen_log_bridge.DrawRectangle(Rectangle{ Point(0, 0), screen_log_win.getSize(), Color::Black });
	con0_out = &screen_log_sink;
}

_ESYM_C void ScreenLogToLayer() {
	if (con0_out != &screen_log_sink) return;
	if (screen_log_on_layer) return;
	if (!screen_log_pixels || !screen_log_win.width || !screen_log_win.height) return;
	auto* layman = global_layman.unsafe_ptr();
	if (!layman || !layman->sheet_buffer) return;

	// A titleless window carries the layer: the console keeps its own canvas and the
	// compositor reads that canvas directly, so the a=1 background stays transparent.
	if (!screen_log_form) screen_log_form = new ::uni::Witch::Form();
	if (!screen_log_form) return;
	Rectangle log_rect(screen_log_win.getVertex(), screen_log_win.getSize());
	screen_log_form->title_visable = false;
	screen_log_form->is_dock = false;
	screen_log_form->normal_rect = log_rect;
	screen_log_form->setSheet(*layman, log_rect, nullptr);
	screen_log_form->sheet_buffer = screen_log_pixels;
	screen_log_form->usrp_buffer = screen_log_pixels;

	// The console lives in the client area for event routing, but reports its updates to
	// the layman, so one log line never triggers a whole-window recomposite.
	LayerManager& client = screen_log_form->getClientSheet();
	screen_log_blank.sheet_area = Rectangle(Point(0, 0), log_rect.getSize());
	client.Append(&screen_log_blank);
	screen_log_console.InitializeSheet(client, Point(0, 0), log_rect.getSize(), screen_log_pixels);
	client.Append(&screen_log_console);
	screen_log_console.refSheetParent() = layman;
	screen_log_console.window = screen_log_win;

	// Just above the desktop, below every window.
	Nnode* node = &screen_log_form->refSheetNode();
	node->offs = static_cast<SheetTrait*>(screen_log_form);
	Nnode* desk_node = global_desktop ? &global_desktop->refSheetNode() : nullptr;
	if (desk_node && desk_node->offs) {
		Nnode* prev = desk_node->left;
		node->next = desk_node;
		node->left = prev;
		if (prev) prev->next = node;
		else layman->subf = node;
		desk_node->left = node;
	}
	else layman->Append(static_cast<SheetTrait*>(screen_log_form));

	screen_log_on_layer = true;
	layman->AddDirty(screen_log_win);
	layman->is_dirty = true;
}

// Fatal path only: the log window moves to the top-most layer.
void ScreenLogRaiseTop() {
	if (!screen_log_on_layer || !screen_log_form) return;
	auto* layman = global_layman.unsafe_ptr();
	if (!layman) return;
	Nnode* node = &screen_log_form->refSheetNode();
	if (!node->offs || layman->subf == node) return;

	if (node->left) node->left->next = node->next;
	else layman->subf = node->next;
	if (node->next) node->next->left = node->left;
	else layman->subl = node->left;

	node->left = nullptr;
	node->next = layman->subf;
	if (layman->subf) layman->subf->left = node;
	else layman->subl = node;
	layman->subf = node;
	layman->is_dirty = true;
}

// Fatal path only: rasterize the whole log grid over a solid color into its own pixel
// buffer, so the top layer is opaque and no stale canvas shows through behind the text.
void ScreenLogFillBackdrop(Color color) {
	if (!screen_log_pixels) return;
	Color bg = screen_log_console.backcolor;
	Color* dst = screen_log_pixels;
	for0(y, screen_log_win.height) {
		for0(x, screen_log_win.width) {
			Color cell = screen_log_console.getPoint(Point(x, y));
			*dst++ = (cell.val == bg.val) ? color : cell;
		}
	}
}

bool SerialCom1Available();
void ScreenLogPanic() {
	auto layman = global_layman.unsafe_ptr();
	auto screen0_win = ScreenLogGetWindow();

	if (!SerialCom1Available()) {
		ScreenLogErr();
		ScreenLogRaiseTop();
		screen_log_console.OutFormat("\n\rKernel panic!\n\r");
		ScreenLogFillBackdrop(Color(0xFF404040));
		layman->UpdateForce(nullptr, screen0_win);
		if (Consman::real_pvci && layman->sheet_buffer) {
			Consman::real_pvci->DrawPoints(screen0_win, layman->sheet_buffer);
		}
		if (Consman::current_video_device) {
			Consman::current_video_device->Flush(screen0_win);
		}
	}
	else outsfmt("\n\rKernel panic!\n\r");
}

#else
_ESYM_C void ScreenLogAttach() {}
#endif
#endif

#endif

