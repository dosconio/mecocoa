// ASCII CPP-ISO11 TAB4 LF
// ModuTitle: [Device] LCD with LTDC (RGB panel + hardware layers)
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#if defined(_MCU_STM32H7x)
#include <c/prochip/CortexM7.h>
#include <cpp/MCU/ST/STM32H7>
#include <cpp/Device/LTDC>
#include <cpp/Device/NVIC>
#include <c/data.h>
#include "../touch/touch-gtxxx.hpp"
#include "../../depends/desktop.hpp"
#include "../../prehost/stm32h743X-ac7m/_openedv/unicfg.hpp"

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
	constexpr stduint kLcdLineLengthBias = 7;// RM0433 LTDC_LxCFBLR: CFBLL is visible line bytes + 7.
	static_assert(sizeof(uni::Color) == 4, "ARGB8888 requires four-byte Color pixels");
	static_assert((kOverlayBase & 31) == 0 && (kOverlayPitch & 31) == 0, "Overlay alignment changed");
	static_assert(kOverlayPitch == kOverlayWidth * 4 && kOverlayPitch * kOverlayHeight == kOverlayBytes,
		"Overlay dimensions exceed the front buffer");
	static_assert(kOverlayBase + kOverlayBytes * 2 == kOverlayReserveEnd && kOverlayReserveEnd <= kNonCacheEnd,
		"Keep the second overlay buffer and DMA2D reservation separate");

	class LcdLayerSurface : public uni::VideoControlInterface {
		stduint address_ = 0, bytes_ = 0, pitch_ = 0;
		uni::Size2 size_;
		uni::PixelFormat format_ = uni::PixelFormat::RGB565;
		mutable uni::Point cursor_;
	public:
		bool Configure(stduint address, stduint bytes, stduint pitch, uni::Size2 size, uni::PixelFormat format) {
			const stduint pixel_bytes = format == uni::PixelFormat::ARGB8888 ? 4 : format == uni::PixelFormat::RGB565 ? 2 : 0;
			if (!pixel_bytes || !address || !pitch || !size.x || !size.y || address % pixel_bytes || pitch % pixel_bytes ||
				size.x > pitch / pixel_bytes || size.y > bytes / pitch || bytes > ~stduint(0) - address) return false;
			address_ = address; bytes_ = bytes; pitch_ = pitch; size_ = size; format_ = format;
			return true;
		}
		void SetCursor(const uni::Point& position) const override { cursor_ = position; }
		uni::Point GetCursor() const override { return cursor_; }
		void DrawPoint(const uni::Point& position, uni::Color color) const override {
			if (!bytes_ || position.x >= size_.x || position.y >= size_.y) return;
			const stduint row = address_ + position.y * pitch_;
			if (format_ == uni::PixelFormat::ARGB8888) ((volatile uint32*)row)[position.x] = uint32(color);
			else ((volatile uint16*)row)[position.x] = color.ToRGB565();
		}
		uni::Color GetColor(uni::Point position) const override {
			if (!bytes_ || position.x >= size_.x || position.y >= size_.y) return uni::Color(0);
			const stduint row = address_ + position.y * pitch_;
			if (format_ == uni::PixelFormat::ARGB8888) return uni::Color(((volatile uint32*)row)[position.x]);
			const uint16 pixel = ((volatile uint16*)row)[position.x];
			const uint32 r = (pixel >> 11) & 31, g = (pixel >> 5) & 63, b = pixel & 31;
			return uni::Color(0xFF000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | (b << 3) | (b >> 2));
		}
		void DrawRectangle(const uni::Rectangle& rect) const override {
			if (!rect.width || !rect.height) return;
			const stdsint left = stdsint(rect.x), top = stdsint(rect.y);
			const stduint skip_x = left < 0 ? stduint(0) - rect.x : 0;
			const stduint skip_y = top < 0 ? stduint(0) - rect.y : 0;
			if (skip_x >= rect.width || skip_y >= rect.height) return;
			const stduint x0 = left < 0 ? 0 : rect.x, y0 = top < 0 ? 0 : rect.y;
			if (x0 >= size_.x || y0 >= size_.y) return;
			const stduint width = minof(rect.width - skip_x, size_.x - x0);
			const stduint height = minof(rect.height - skip_y, size_.y - y0);
			for0(y, height) for0(x, width) {
				if (rect.filled || x + skip_x == 0 || x + skip_x == rect.width - 1 || y + skip_y == 0 || y + skip_y == rect.height - 1)
					DrawPoint(uni::Point(x0 + x, y0 + y), rect.color);
			}
		}
		// Block sources are full-surface images, matching VideoControlInterfaceMARGB8888 callers.
		void DrawPoints(const uni::Rectangle& rect, const uni::Color* source) const override {
			if (!source || rect.x >= size_.x || rect.y >= size_.y) return;
			const stduint width = minof(rect.width, size_.x - rect.x), height = minof(rect.height, size_.y - rect.y);
			for0(y, height) for0(x, width)
				DrawPoint(uni::Point(rect.x + x, rect.y + y), source[(rect.y + y) * size_.x + rect.x + x]);
		}
		void DrawFont(const uni::Point& position, const uni::DisplayFont& font, const uni::String& text) const override {
			const char* source = text.reference();
			if (!source) return;
			if (stdsint(position.y) >= stdsint(size_.y) || (stdsint(position.y) < 0 && stduint(0) - position.y >= 16)) return;
			stduint x = position.x;
			while (*source) {
				if (stdsint(x) >= stdsint(size_.x)) break;
				const byte character = byte(*source++);
				const byte glyph = character >= 32 && character <= 126 ? character - 32 : '?' - 32;
				const auto* bits = (const uint16*)&_BITFONT_ASCII_16x8[glyph];
				for0(col, 8) for0(row, 16) if (bits[col] & (1u << row))
					DrawPoint(uni::Point(x + col, position.y + (row ^ 7)), font.forecolor);
				x += 8;
			}
		}
		void Clear(uni::Color color) const { DrawRectangle(uni::Rectangle(uni::Point(0, 0), size_, color)); }
	};

	// This slice transfers one Sheet verbatim; multi-Sheet alpha composition is handled separately.
	class LcdSheetLayerManager : public LayerManager2 {
	public:
		LcdSheetLayerManager(uni::VideoControlInterface* surface, const uni::Rectangle& area) : LayerManager2(surface, area) {}
		void Update(uni::SheetTrait* who, const uni::Rectangle& rect) override {
			if (!pvci || subf != subl) {
				plogerro("[LTDC] single-Sheet layer cannot compose multiple Sheets");
				return;
			}
			auto* sheet = subf ? static_cast<uni::SheetTrait*>(subf->offs) : nullptr;
			const stduint origin_x = who ? who->sheet_area.x : 0, origin_y = who ? who->sheet_area.y : 0;
			const stdsint left = stdsint(origin_x + rect.x), top = stdsint(origin_y + rect.y);
			const stduint skip_x = left < 0 ? stduint(0) - stduint(left) : 0;
			const stduint skip_y = top < 0 ? stduint(0) - stduint(top) : 0;
			if (skip_x >= rect.width || skip_y >= rect.height) return;
			const stduint x0 = left < 0 ? 0 : stduint(left), y0 = top < 0 ? 0 : stduint(top);
			if (x0 >= window.width || y0 >= window.height) return;
			const stduint width = minof(rect.width - skip_x, window.width - x0);
			const stduint height = minof(rect.height - skip_y, window.height - y0);
			for0(y, height) for0(x, width) {
				const uni::Point position(x0 + x, y0 + y);
				uni::Color color(0);
				if (sheet && sheet->sheet_area.ifContain(position)) {
					const auto local = position - sheet->sheet_area.getVertex();
					color = sheet->sheet_buffer ? sheet->sheet_buffer[local.y * sheet->sheet_area.width + local.x] : sheet->getPoint(local);
				}
				pvci->DrawPoint(position, color);
			}
			__DSB();
			is_dirty = false;
			dirty_area = {};
		}
	};
}

struct LcdLayerSlot : GraphicLayer {
	uni::LTDC_LAYER_t* hw = nullptr;
	LcdLayerSurface pixels;
	uni::UniquePtr<LayerManager2> owned_manager;
	bool configured = false;
	uni::Rectangle submitted_window, scanned_window;
	bool placement_dirty = false, reload_pending = false;
};

namespace {
	bool lcd_vblank_enabled = false;
	volatile bool lcd_reload_complete = false;

	void LcdReloadComplete() { lcd_reload_complete = true; }

	void WriteLcdPlacement(LcdLayerSlot& slot, const uni::Rectangle& window) {
		// RM0433 LTDC: window endpoints include BPCR; CFBP is the allocated row stride.
		const stduint bpcr = uni::LTDC[uni::LTDCReg::BPCR];
		const stduint hbp = (bpcr >> 16) & 0xFFFu, vbp = bpcr & 0xFFFu;
		(*slot.hw)[uni::LTDCLayerReg::WHPCR] = ((window.x + window.width + hbp) << 16) | (window.x + hbp + 1);
		(*slot.hw)[uni::LTDCLayerReg::WVPCR] = ((window.y + window.height + vbp) << 16) | (window.y + vbp + 1);
		(*slot.hw)[uni::LTDCLayerReg::CFBAR] = slot.address;
		(*slot.hw)[uni::LTDCLayerReg::CFBLR] = (slot.pitch << 16) | (window.width * 4 + kLcdLineLengthBias);
		(*slot.hw)[uni::LTDCLayerReg::CFBLNR] = window.height;
		(*slot.hw)[uni::LTDCLayerReg::CR].setof(0, true);
	}

	// The Graphic thread holds global_layman; the ISR only publishes completion.
	void ServiceLcdPlacement(LcdLayerSlot& slot) {
		const bool restore_interrupts = IC.TryMaskInterrupt();
		if (slot.reload_pending && lcd_reload_complete) {
			slot.scanned_window = slot.submitted_window;
			slot.reload_pending = false;
			lcd_reload_complete = false;
		}
		// Never modify shadow registers while an earlier reload can still latch them.
		if (!slot.reload_pending && slot.placement_dirty && !(stduint(uni::LTDC[uni::LTDCReg::SRCR]) & 3u)) {
			slot.submitted_window = slot.window;
			slot.placement_dirty = false;
			WriteLcdPlacement(slot, slot.submitted_window);
			uni::LTDC[uni::LTDCReg::ICR] = 1u << 3;// RM0433: CRRIF clears an old reload completion.
			lcd_reload_complete = false;
			slot.reload_pending = true;
			uni::LTDC[uni::LTDCReg::IER].setof(3);// RRIE is disabled by the shared IRQ handler after completion.
			__DSB();
			uni::LTDC.Reload(uni::LTDCReload::VerticalBlank);
		}
		if (restore_interrupts) IC.enInterrupt(true);
	}

	bool PlaceLcdLayer(GraphicLayer& generic, const uni::Point& position) {
		auto& slot = static_cast<LcdLayerSlot&>(generic);
		if (!slot.screen || !slot.hw || !slot.manager || !slot.address || !slot.pitch ||
			slot.manager->window.width > slot.capacity.x || slot.manager->window.height > slot.capacity.y) return false;
		const auto screen = slot.screen->GetFramebuffer().screen_size;
		if (position.x >= screen.x || position.y >= screen.y) return false;
		const stduint width = minof(slot.manager->window.width, screen.x - position.x);
		const stduint height = minof(slot.manager->window.height, screen.y - position.y);
		if (!width || !height) return false;
		const uni::Rectangle window(position, uni::Size2(width, height));
		if (!slot.configured) {
			uni::LTDC_LAYER_t::LayerPara para{};
			uni::LTDC_LAYER_t::layer_param_refer(&para);
			para.roleaddr = (pureptr_t)slot.address;
			para.image_size = slot.capacity;
			para.window = window;
			para.pixel_format = slot.format;
			para.backcolor = uni::Color(0xFF000000u);
			para.factor1_mode = para.factor2_mode = true;
			if (!slot.hw->setMode(para)) return false;
			slot.configured = true;
		}
		// window is the latest logical position, independent of the last scanned frame.
		slot.window = window;
		slot.enabled = true;
		if (lcd_vblank_enabled) {
			slot.placement_dirty = true;
			ServiceLcdPlacement(slot);
		}
		else {
			WriteLcdPlacement(slot, window);
			__DSB();
			uni::LTDC.Reload(uni::LTDCReload::Immediate);
			slot.submitted_window = slot.scanned_window = window;
		}
		return true;
	}

	void DisableLcdLayer(GraphicLayer& generic) {
		auto& slot = static_cast<LcdLayerSlot&>(generic);
		const bool restore_interrupts = IC.TryMaskInterrupt();
		// Detaching is synchronous so a software replacement cannot leave a second cursor visible.
		uni::LTDC[uni::LTDCReg::IER].rstof(3);
		if (slot.hw) {
			(*slot.hw)[uni::LTDCLayerReg::CR].setof(0, false);
			uni::LTDC[uni::LTDCReg::SRCR] = 1u;// IMR, without copying a pending VBR request.
			__DSB();
		}
		uni::LTDC[uni::LTDCReg::ICR] = 1u << 3;
		lcd_reload_complete = false;
		slot.reload_pending = slot.placement_dirty = false;
		slot.scanned_window = {};
		slot.enabled = false;
		if (restore_interrupts) IC.enInterrupt(true);
	}
}

class RgbLcdScreen : public uni::VideoDevice {
	uni::FramebufferInfo fb_info = {};
	LcdLayerSlot slots[2];
	uni::Dchain layers{ nullptr };// bottom to top, owned by this screen
	uni::TouchTrait* touch_device = nullptr;
public:
	void AttachTouchDevice(uni::TouchTrait* device) { touch_device = device; }
	uni::TouchTrait* GetTouchDevice() const { return touch_device; }
	uni::Dchain& GetLayers() { return layers; }
	bool Initialize(pureptr_t fb_addr) {
		if (layers.Root()) return fb_info.physical_range.address == _IMM(fb_addr);
		if (!BoardConfig::Ltdc::Panel::Width || !BoardConfig::Ltdc::Panel::Height || !fb_addr) return false;
		if (!ConfigDeviceGPIN(DeviceType::LTDC)) return false;
		if (!ConfigDeviceClock(DeviceType::LTDC)) return false;
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
		// Override the shared F4/H7 setup with the H7 line-length encoding.
		uni::LTDC[1][uni::LTDCLayerReg::CFBLR] = ((stduint(h.active_len) * 2) << 16) | (h.active_len * 2 + kLcdLineLengthBias);
		uni::LTDC.Reload(uni::LTDCReload::Immediate);
		fb_info.physical_range = uni::Slice{ (stduint)fb_addr, (stduint)h.active_len * v.active_len * 2 };
		fb_info.screen_size = para.image_size;
		fb_info.pitch = h.active_len * 2;
		fb_info.bpp = 16;
		fb_info.format = uni::PixelFormat::RGB565;
		if (fb_info.physical_range.address < kFramebufferBase || fb_info.physical_range.address >= kOverlayBase ||
			fb_info.physical_range.length > kOverlayBase - fb_info.physical_range.address) return false;
		auto& main = slots[0];
		main.screen = this; main.id = 0; main.hw = &uni::LTDC[1]; main.enabled = true; main.configured = true;
		main.address = fb_info.physical_range.address; main.bytes = fb_info.physical_range.length; main.pitch = fb_info.pitch;
		main.capacity = fb_info.screen_size; main.window = para.window; main.format = fb_info.format;
		if (!main.pixels.Configure(main.address, main.bytes, main.pitch, main.capacity, main.format)) return false;
		main.surface = &main.pixels;
		main.manager = global_layman.unsafe_ptr();
		auto& sprite = slots[1];
		sprite.screen = this; sprite.id = 1; sprite.hw = &uni::LTDC[2]; sprite.input_passthrough = true;
		sprite.address = kOverlayBase; sprite.bytes = kOverlayBytes; sprite.pitch = kOverlayPitch;
		sprite.capacity = uni::Size2(kOverlayWidth, kOverlayHeight); sprite.format = uni::PixelFormat::ARGB8888;
		sprite.surface = &sprite.pixels; sprite.Place = PlaceLcdLayer; sprite.Disable = DisableLcdLayer;
		DisableLcdLayer(sprite);
		if (!sprite.pixels.Configure(sprite.address, sprite.bytes, sprite.pitch, sprite.capacity, sprite.format)) return false;
		sprite.pixels.Clear(uni::Color(0));
		sprite.owned_manager.reset(new (std::nothrow_t{}) LcdSheetLayerManager(sprite.surface,
			uni::Rectangle(uni::Point(0, 0), sprite.capacity, uni::Color(0))));
		if (!sprite.owned_manager) return false;
		sprite.manager = sprite.owned_manager.get();
		if (!layers.Root()) {
			if (!layers.Append(static_cast<GraphicLayer*>(&main))) return false;
			if (!layers.Append(static_cast<GraphicLayer*>(&sprite))) {
				layers.Remove(layers.Root());
				return false;
			}
		}
		return true;
	}
	bool EnableCursorLayer(Cursor& cursor) {
		auto& slot = slots[1];
		if (!cursor.sheet_buffer || !slot.manager || cursor.sheet_area.width > slot.capacity.x || cursor.sheet_area.height > slot.capacity.y) return false;
		slot.manager->window = uni::Rectangle(uni::Point(0, 0), cursor.sheet_area.getSize(), uni::Color(0));
		slot.manager->sheet_area = slot.manager->window;
		if (!cursor.MoveToLayer(&slot)) return false;
		return true;
	}
	void EnableVBlank() {
		const bool restore_interrupts = IC.TryMaskInterrupt();
		uni::LTDC[uni::LTDCReg::IER].rstof(3);
		uni::LTDC[uni::LTDCReg::ICR] = 1u << 3;
		lcd_reload_complete = false;
		uni::LTDC.ReloadEventCallback = LcdReloadComplete;
		uni::NVIC.setPriority(uni::IRQ_LTDC, 8);
		uni::NVIC.setAble(uni::IRQ_LTDC, true);
		lcd_vblank_enabled = true;
		if (restore_interrupts) IC.enInterrupt(true);
	}
	bool HasPresentationWork() const {
		return lcd_reload_complete || (slots[1].placement_dirty && !slots[1].reload_pending);
	}
	void PumpPresentation() { ServiceLcdPlacement(slots[1]); }
	stduint LayerCount() const {
		stduint count = 0;
		for (auto* node = layers.Root(); node; node = node->next) if (static_cast<GraphicLayer*>(node->offs)->enabled) ++count;
		return count;
	}
	GraphicLayer* GetLayer(uint32 id) {
		for (auto* node = layers.Root(); node; node = node->next) {
			auto* layer = static_cast<GraphicLayer*>(node->offs);
			if (layer->id == id) return layer;
		}
		return nullptr;
	}
	const uni::FramebufferInfo& GetFramebuffer() const override { return fb_info; }
	void SetCursor(const uni::Point& disp) const override { slots[0].pixels.SetCursor(disp); }
	uni::Point GetCursor() const override { return slots[0].pixels.GetCursor(); }
	void DrawPoint(const uni::Point& disp, uni::Color color) const override { slots[0].pixels.DrawPoint(disp, color); }
	void DrawRectangle(const uni::Rectangle& rect) const override { slots[0].pixels.DrawRectangle(rect); }
	void DrawFont(const uni::Point& disp, const uni::DisplayFont& font, const uni::String& str) const override { slots[0].pixels.DrawFont(disp, font, str); }
	void DrawPoints(const uni::Rectangle& rect, const uni::Color* source) const override { slots[0].pixels.DrawPoints(rect, source); }
	uni::Color GetColor(uni::Point position) const override { return slots[0].pixels.GetColor(position); }
};

static RgbLcdScreen* rgb_lcd = nullptr;

uni::VideoDevice* RgbLcd_ScreenInit(pureptr_t fb_addr) {
	if (!rgb_lcd) rgb_lcd = new RgbLcdScreen;
	if (!rgb_lcd->Initialize(fb_addr)) return nullptr;
	return rgb_lcd;
}

stduint RgbLcd_LayerCount() { return rgb_lcd ? rgb_lcd->LayerCount() : 0; }

// Called by the Graphic service even after the final touch frame has been released.
void RgbLcd_PumpPresentation() {
	if (!rgb_lcd || !rgb_lcd->HasPresentationWork()) return;
	auto layman = global_layman.Lock();
	rgb_lcd->PumpPresentation();
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
		{
			auto layman = global_layman.Lock();
			if (!rgb_lcd->EnableCursorLayer(*Cursor::global_cursor))
				plogwarn("[LTDC] cursor layer unavailable; using the main layer");
		}
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
			auto* control = TouchGt1xx::GetControlBlock();
			if (Consman::RegisterTouchDevice(control)) {
				while (TouchGt1xx::Process()) {}
				Consman::UnregisterTouchDevice(control);
				while (Consman::IsTouchDeviceRegistered(control)) syscall(syscall_t::REST, 1, 2);
			}
			else plogerro("[TOUCH] cannot register the touch consumer");
			rgb_lcd->AttachTouchDevice(nullptr);
			TouchGt1xx::Shutdown();
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

void LTDC_LAYER_t::DrawFont(const Point& disp, const DisplayFont& font, const String& str) const {
	const char* p = str.reference();
	stduint x = disp.x, y = disp.y;
	Color fg = font.forecolor;
	while (*p) {
		char ch = *p++;
		if (ch < 32 || ch > 126) ch = '?';
		ch -= 0x20;
		const uint16* datptr = (const uint16*)&_BITFONT_ASCII_16x8[(byte)ch];
		for (byte col = 0; col < 8; col++) {
			uint16 dat = datptr[col];
			for (byte row = 0; row < 16; row++) {
				if (dat & _IMM1S(row)) DrawPoint(Point(x + col, y + (row ^ 0b111)), fg);
			}
		}
		x += 8;
	}
}

void R_LTDC_INIT() {
	if (rgb_lcd_initialized) return;
	// Complete display setup before Fileman and the other services can run.
	if (!InitializeLcdDesktop(RgbLcd_ScreenInit((pureptr_t)kFramebufferBase))) {
		plogerro("[LTDC] device initialization failed");
		return;
	}
	if (!Devsman::RegisterPlatformDevice("video-ltdc", "video-ltdc", static_cast<uni::VideoDevice*>(rgb_lcd))) {
		plogerro("[LTDC] cannot register the display device");
		return;
	}
	rgb_lcd->EnableVBlank();
	rgb_lcd_initialized = true;
	// Devsman dispatches this callback after the fixed service IDs have been allocated.
	if (!Systimex::AppendDeferredCallback(1, 0, (_tocall_ft)StartLcdDeviceThread)) {
		plogerro("[LTDC] cannot append the device thread start action");
	}
}

#endif
