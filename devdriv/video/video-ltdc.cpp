// ASCII CPP-ISO11 TAB4 LF
// ModuTitle: [Device] LCD with LTDC (RGB panel + hardware layers)
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#if defined(_MCU_STM32H7x)
#include <c/prochip/CortexM7.h>
#include <cpp/MCU/ST/STM32H7>
#include <cpp/Device/LTDC>
#include "../touch/touch-gtxxx.hpp"
#include "../../depends/desktop.hpp"
#include "../../prehost/stm32h743X-ac7m/_openedv/unicfg.hpp"

void ltdc_gpio_config();
void ltdc_clock_config();
extern byte _BUF_cursor[byteof(Cursor)];

_ESYM_C void R_LTDC_INIT();

__attribute__((section(".init.rmod")))
extern const RMOD_LIST RMOD_LIST_LTDC{
	R_LTDC_INIT,
	"LTDC",
	{}
};

namespace {
	constexpr stduint kOverlayBase = 0xC00BC000;
	constexpr stduint kOverlayBytes = 2048;
	constexpr stduint kOverlayReserveEnd = 0xC00BD000;
	constexpr stduint kFramebufferBase = 0xC0000000;
	constexpr stduint kNonCacheEnd = 0xC0100000;
	constexpr stduint kOverlayWidth = 16, kOverlayHeight = 32, kOverlayPitch = 64;
	static_assert(sizeof(uni::Color) == 4, "ARGB8888 requires four-byte Color pixels");
	static_assert((kOverlayBase & 31) == 0 && (kOverlayPitch & 31) == 0, "Overlay alignment changed");
	static_assert(kOverlayPitch == kOverlayWidth * 4 && kOverlayPitch * kOverlayHeight == kOverlayBytes,
		"Overlay dimensions exceed the front buffer");
	static_assert(kOverlayBase + kOverlayBytes * 2 == kOverlayReserveEnd && kOverlayReserveEnd <= kNonCacheEnd,
		"Keep the second overlay buffer and DMA2D reservation separate");

	struct LcdArgbSurface {
		stduint base = kOverlayBase, bytes = kOverlayBytes, pitch = kOverlayPitch;
		uni::Size2 capacity = { kOverlayWidth, kOverlayHeight }, visible = {};
		uni::PixelFormat format = uni::PixelFormat::ARGB8888;

		bool IsValid() const {
			return format == uni::PixelFormat::ARGB8888 && base >= kOverlayBase && base < kOverlayReserveEnd &&
				!(base & 31) && bytes <= kOverlayReserveEnd - base && bytes <= kNonCacheEnd - base &&
				pitch && !(pitch & 3) && capacity.x && capacity.y && capacity.x <= pitch / 4 &&
				capacity.y <= bytes / pitch && visible.x && visible.y &&
				visible.x <= capacity.x && visible.y <= capacity.y;
		}
		void Clear() const {
			auto pixels = (volatile uint32*)base;
			for0(i, bytes / 4) pixels[i] = 0;
		}
		// Source starts at the submitted block's first pixel; its pitch is in Color elements.
		bool Submit(const uni::Rectangle& rect, const uni::Color* source, stduint source_pitch, stduint source_count) const {
			if (!IsValid() || !source || !rect.width || !rect.height || source_pitch < rect.width ||
				source_count > ~stduint(0) / sizeof(uni::Color) || rect.width > source_count ||
				rect.height - 1 > (source_count - rect.width) / source_pitch) return false;
			if (rect.x >= visible.x || rect.y >= visible.y) return true;
			const stduint width = minof(rect.width, visible.x - rect.x);
			const stduint height = minof(rect.height, visible.y - rect.y);
			for0(y, height) {
				auto row = (volatile uint32*)(base + (rect.y + y) * pitch) + rect.x;
				for0(x, width) row[x] = uint32(source[y * source_pitch + x]);
			}
			return true;
		}
		uint32 Read(stduint x, stduint y) const {
			return *((volatile uint32*)(base + y * pitch) + x);
		}
	};
}

struct LcdLayerSlot {
	uni::LTDC_LAYER_t* hw = nullptr;// the LTDC layer is the drawing surface itself
	uni::LayerManager* manager = nullptr;// composes into this layer once the chain is built
	bool used = false;
	byte id = 0;// 1-based LTDC layer index
	pureptr_t addr = 0;
	uni::Size2 cap = {};
	uni::Rectangle window = {};
};

class RgbLcdScreen : public uni::VideoDevice {
protected:
	uni::FramebufferInfo fb_info = {};
	LcdLayerSlot slots[2];// this part has exactly two LTDC layers
	LcdArgbSurface overlay;
	uni::TouchTrait* touch_device = nullptr;
public:
	void AttachTouchDevice(uni::TouchTrait* device) { touch_device = device; }
	uni::TouchTrait* GetTouchDevice() const { return touch_device; }
	bool Initialize(pureptr_t fb_addr) {
		if (!BoardConfig::Ltdc::Panel::Width || !BoardConfig::Ltdc::Panel::Height || !fb_addr) return false;
		ltdc_gpio_config();
		ltdc_clock_config();
		auto& h = uni::LTDC.refHorizontal();
		h.sync_len = BoardConfig::Ltdc::Panel::HorizontalSyncLength;
		h.back_porch = BoardConfig::Ltdc::Panel::HorizontalBackPorch;
		h.active_len = BoardConfig::Ltdc::Panel::Width;
		h.front_porch = BoardConfig::Ltdc::Panel::HorizontalFrontPorch;
		auto& v = uni::LTDC.refVertical();
		v.sync_len = BoardConfig::Ltdc::Panel::VerticalSyncLength;
		v.back_porch = BoardConfig::Ltdc::Panel::VerticalBackPorch;
		v.active_len = BoardConfig::Ltdc::Panel::Height;
		v.front_porch = BoardConfig::Ltdc::Panel::VerticalFrontPorch;
		if (!uni::LTDC.setMode(uni::Color(BoardConfig::Ltdc::Panel::BackgroundArgb))) return false;
		uni::LTDC_LAYER_t::LayerPara para{};
		uni::LTDC_LAYER_t::layer_param_refer(&para);
		para.roleaddr = fb_addr;
		para.image_size = uni::Size2(h.active_len, v.active_len);
		para.window = uni::Rectangle(uni::Point(0, 0), para.image_size);
		para.pixel_format = uni::PixelFormat::RGB565;
		if (!uni::LTDC[1].setMode(para)) return false;
		uni::LTDC.Reload(uni::LTDCReload::Immediate);
		slots[0].used = true;
		slots[0].id = 1;
		slots[0].addr = fb_addr;
		slots[0].cap = para.image_size;
		slots[0].window = para.window;
		slots[0].hw = &uni::LTDC[1];
		fb_info.physical_range = uni::Slice{ (stduint)fb_addr, (stduint)h.active_len * v.active_len * 2 };
		fb_info.screen_size = para.image_size;
		fb_info.pitch = h.active_len * 2;
		fb_info.bpp = 16;
		fb_info.format = uni::PixelFormat::RGB565;
		return true;
	}
	bool InitializeOverlayDemo(uni::Point position) {
		if (!slots[0].used || slots[1].used) return false;
		auto& layer = uni::LTDC[2];
		layer[uni::LTDCLayerReg::CR].setof(0, false);// LEN: fill the surface before enabling scanout
		uni::LTDC.Reload(uni::LTDCReload::Immediate);
		overlay.visible = uni::Size2(15, 24);// exercise a visible width smaller than the fixed 16-pixel pitch
		if (!overlay.IsValid() || position.x >= slots[0].cap.x || position.y >= slots[0].cap.y ||
			overlay.visible.x > slots[0].cap.x - position.x || overlay.visible.y > slots[0].cap.y - position.y ||
			fb_info.physical_range.address < kFramebufferBase || fb_info.physical_range.address > overlay.base ||
			fb_info.physical_range.length > overlay.base - fb_info.physical_range.address) return false;

		const uint32 before = *((volatile uint32*)(overlay.base - 4));
		const uint32 after = *((volatile uint32*)(overlay.base + overlay.bytes));
		uni::Color pattern[kOverlayWidth * kOverlayHeight];
		for0(y, kOverlayHeight) for0(x, kOverlayWidth) {
			uint32 color = 0x7FFF0000;// uniform half-transparent red over the visible window
			if (x >= overlay.visible.x || y >= overlay.visible.y) color = 0xFF00FF00;// clipped source padding
			pattern[y * kOverlayWidth + x] = uni::Color(color);
		}
		overlay.Clear();
		if (!overlay.Submit(uni::Rectangle(uni::Point(0, 0), overlay.capacity), pattern,
			kOverlayWidth, numsof(pattern))) return false;
		bool memory_ok = true;
		for0(y, kOverlayHeight) for0(x, kOverlayWidth) {
			const uint32 expected = (x < overlay.visible.x && y < overlay.visible.y) ? uint32(pattern[y * kOverlayWidth + x]) : 0;
			if (overlay.Read(x, y) != expected) memory_ok = false;
		}
		const bool guards_ok = before == *((volatile uint32*)(overlay.base - 4)) &&
			after == *((volatile uint32*)(overlay.base + overlay.bytes));
		if (!memory_ok || !guards_ok) {
			plogwarn("[LTDC A] readback=%s guards=%s", memory_ok ? "OK" : "FAIL", guards_ok ? "OK" : "FAIL");
			return false;
		}

		uni::LTDC_LAYER_t::LayerPara para{};
		uni::LTDC_LAYER_t::layer_param_refer(&para);
		para.roleaddr = (pureptr_t)overlay.base;
		para.image_size = overlay.capacity;
		para.window = uni::Rectangle(position, overlay.visible);
		para.pixel_format = overlay.format;
		para.backcolor = uni::Color(0xFF000000);// current H7 backend takes CACR from backcolor.a and clears DCCR alpha
		para.Alpha0 = 0;
		para.factor1_mode = true;// RM0433: pixel alpha times constant alpha
		para.factor2_mode = true;
		__DSB();
		if (!layer.setMode(para)) {
			layer[uni::LTDCLayerReg::CR].setof(0, false);
			uni::LTDC.Reload(uni::LTDCReload::Immediate);
			return false;
		}
		uni::LTDC.Reload(uni::LTDCReload::Immediate);// Stage A is initialization only
		slots[1].used = true;
		slots[1].id = 2;
		slots[1].addr = para.roleaddr;
		slots[1].cap = overlay.capacity;
		slots[1].window = para.window;
		slots[1].hw = &layer;// metadata only: its CPU drawing methods are RGB565-only
		ploginfo("[LTDC A] overlay=(%u,%u) %ux%u ARGB8888 base=%08X pitch=%u layers=%u",
			(unsigned)position.x, (unsigned)position.y, (unsigned)overlay.visible.x, (unsigned)overlay.visible.y,
			(unsigned)overlay.base, (unsigned)overlay.pitch, (unsigned)LayerCount());
		ploginfo("[LTDC A] cr=%08X pfcr=%08X cfbar=%08X cfblr=%08X lines=%u cacr=%08X bfcr=%08X",
			(unsigned)layer[uni::LTDCLayerReg::CR], (unsigned)layer[uni::LTDCLayerReg::PFCR],
			(unsigned)layer[uni::LTDCLayerReg::CFBAR], (unsigned)layer[uni::LTDCLayerReg::CFBLR],
			(unsigned)layer[uni::LTDCLayerReg::CFBLNR], (unsigned)layer[uni::LTDCLayerReg::CACR],
			(unsigned)layer[uni::LTDCLayerReg::BFCR]);
		return true;
	}
	stduint LayerCount() const {
		stduint n = 0;
		for0(i, numsof(slots)) if (slots[i].used) n++;
		return n;
	}
	LcdLayerSlot* getSlot(byte id) {
		for0(i, numsof(slots)) if (slots[i].used && slots[i].id == id) return &slots[i];
		return nullptr;
	}
	virtual const uni::FramebufferInfo& GetFramebuffer() const override { return fb_info; }
	virtual void SetCursor(const uni::Point& disp) const override { if (slots[0].hw) slots[0].hw->SetCursor(disp); }
	virtual uni::Point GetCursor() const override { return slots[0].hw ? slots[0].hw->GetCursor() : uni::Point(0, 0); }
	virtual void DrawPoint(const uni::Point& disp, uni::Color color) const override { if (slots[0].hw) slots[0].hw->DrawPoint(disp, color); }
	virtual void DrawRectangle(const uni::Rectangle& rect) const override { if (slots[0].hw) slots[0].hw->DrawRectangle(rect); }
	virtual void DrawFont(const uni::Point& disp, const uni::DisplayFont& font, const uni::String& str) const override { if (slots[0].hw) slots[0].hw->DrawFont(disp, font, str); }
	virtual uni::Color GetColor(uni::Point p) const override { return slots[0].hw ? slots[0].hw->GetColor(p) : (uni::Color)0; }
};

static RgbLcdScreen* rgb_lcd = nullptr;

uni::VideoDevice* RgbLcd_ScreenInit(pureptr_t fb_addr) {
	if (!rgb_lcd) rgb_lcd = new RgbLcdScreen;
	if (!rgb_lcd->Initialize(fb_addr)) return nullptr;
	return rgb_lcd;
}

stduint RgbLcd_LayerCount() { return rgb_lcd ? rgb_lcd->LayerCount() : 0; }

bool RgbLcd_OverlayDemo(stduint x, stduint y) {
	return rgb_lcd && rgb_lcd->InitializeOverlayDemo(uni::Point(x, y));
}

namespace {
	bool rgb_lcd_initialized = false;
	ProcessBlock* rgb_lcd_process = nullptr;

	bool InitializeLcdDesktop(uni::VideoDevice* screen) {
		if (!screen) return false;
		sys_framebuffer = screen->GetFramebuffer();
		const Rectangle screen_window(uni::Point(0, 0), sys_framebuffer.screen_size, uni::Color::Black);
		if (!Consman::AdoptVideoDevice(screen)) return false;
		Cursor::global_cursor = new (_BUF_cursor)Cursor{ &global_layman.Lock()->getVCI() };
		Cursor::global_cursor->setSheet(*global_layman.Lock(), uni::Point(300, 200));
		auto desktop = new Desktop(Desktop::kDefaultBgColor);
		global_desktop = desktop;
		desktop->Initialize(*global_layman.Lock(), screen_window, Desktop::kDefaultBgColor);
		global_layman.Lock()->Append(desktop);
		{
			auto layman = global_layman.Lock();
			layman->AddDirty(screen_window);
			layman->is_dirty = true;
			layman->UpdateForce(nullptr, screen_window);
			if (Consman::real_pvci && layman->sheet_buffer) {
				Consman::real_pvci->DrawPoints(screen_window, layman->sheet_buffer);
			}
		}
		if (!RgbLcd_OverlayDemo(200, 150)) plogwarn("[LTDC A] overlay unavailable, keeping the main layer");
		__DSB();
		Consman::ento_gui = true;
		return true;
	}

	void ParkLcdThread() {
		stduint args[4] = {};
		for (;;) sysrecv(ANYPROC, args, sizeof(args));
	}

	void RgbLcdDeviceLoop() {
		ploginfo("[LTDC] device ready tid=%u", (unsigned)Taskman::CurrentTB()->tid);
		if (auto touch = TouchGt1xx::Initialize(rgb_lcd->GetFramebuffer().screen_size)) {
			rgb_lcd->AttachTouchDevice(touch);
			while (TouchGt1xx::Process()) {}
			rgb_lcd->AttachTouchDevice(nullptr);
		}
		ParkLcdThread();
	}

	void StartLcdDeviceThread(void*, ...) {
		if (!rgb_lcd_initialized || rgb_lcd_process) return;
		auto process = Taskman::Create((void*)&RgbLcdDeviceLoop, RING_M, false);
		if (!process) {
			plogerro("[LTDC] cannot create device thread");
			return;
		}
		process->main_thread->name.reset(StrHeap("serv_ltdc"));
		rgb_lcd_process = process;
		Taskman::Append(process);
		Taskman::AppendThread(process->main_thread);
	}
}

void R_LTDC_INIT() {
	if (rgb_lcd_initialized) return;
	// Complete display setup before Fileman and the other services can run.
	if (!InitializeLcdDesktop(RgbLcd_ScreenInit((pureptr_t)kFramebufferBase))) {
		plogerro("[LTDC] device initialization failed");
		return;
	}
	rgb_lcd_initialized = true;
	// Devsman dispatches this callback after the fixed service IDs have been allocated.
	if (!Systimex::AppendDeferredCallback(1, 0, (_tocall_ft)StartLcdDeviceThread)) {
		plogerro("[LTDC] cannot append the device thread start action");
	}
}

#endif
