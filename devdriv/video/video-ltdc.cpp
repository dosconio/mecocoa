// ASCII CPP-ISO11 TAB4 LF
// ModuTitle: [Device] LCD with LTDC (RGB panel + hardware layers)
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#if defined(_MCU_STM32H7x)
#include <cpp/MCU/ST/STM32H7>
#include <cpp/Device/LTDC>

void ltdc_gpio_config();
void ltdc_clock_config();

struct LcdLayerSlot {
	uni::LTDC_LAYER_t* hw = nullptr;// the LTDC layer is the drawing surface itself
	uni::LayerManager* manager = nullptr;// composes into this layer once the chain is built
	bool used = false;
	byte id = 0;// 1-based LTDC layer index
	pureptr_t addr = 0;
	uni::Size2 cap = {};
	uni::Rectangle window = {};
};

struct RgbPanelConfig {
	stduint hsw = 0, hbp = 0, hact = 0, hfp = 0;
	stduint vsw = 0, vbp = 0, vact = 0, vfp = 0;
	uni::Color background = uni::Color::Black;
	bool valid = false;
};

class RgbLcdScreen : public uni::VideoDevice {
protected:
	uni::FramebufferInfo fb_info = {};
	RgbPanelConfig panel = {};
	LcdLayerSlot slots[2];// this part has exactly two LTDC layers
public:
	void SetPanelTiming(stduint hsw, stduint hbp, stduint hact, stduint hfp, stduint vsw, stduint vbp, stduint vact, stduint vfp) {
		panel.hsw = hsw; panel.hbp = hbp; panel.hact = hact; panel.hfp = hfp;
		panel.vsw = vsw; panel.vbp = vbp; panel.vact = vact; panel.vfp = vfp;
		panel.valid = hact && vact;
	}
	bool Initialize(pureptr_t fb_addr) {
		if (!panel.valid || !fb_addr) return false;
		ltdc_gpio_config();
		ltdc_clock_config();
		auto& h = uni::LTDC.refHorizontal();
		h.sync_len = panel.hsw; h.back_porch = panel.hbp; h.active_len = panel.hact; h.front_porch = panel.hfp;
		auto& v = uni::LTDC.refVertical();
		v.sync_len = panel.vsw; v.back_porch = panel.vbp; v.active_len = panel.vact; v.front_porch = panel.vfp;
		if (!uni::LTDC.setMode(panel.background)) return false;
		uni::LTDC_LAYER_t::LayerPara para{};
		uni::LTDC_LAYER_t::layer_param_refer(&para);
		para.roleaddr = fb_addr;
		para.image_size = uni::Size2(panel.hact, panel.vact);
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
		fb_info.physical_range = uni::Slice{ (stduint)fb_addr, (stduint)panel.hact * panel.vact * 2 };
		fb_info.screen_size = para.image_size;
		fb_info.pitch = panel.hact * 2;
		fb_info.bpp = 16;
		fb_info.format = uni::PixelFormat::RGB565;
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

void RgbLcd_ConfigPanel(stduint hsw, stduint hbp, stduint hact, stduint hfp, stduint vsw, stduint vbp, stduint vact, stduint vfp) {
	if (!rgb_lcd) rgb_lcd = new RgbLcdScreen;
	rgb_lcd->SetPanelTiming(hsw, hbp, hact, hfp, vsw, vbp, vact, vfp);
}

uni::VideoDevice* RgbLcd_ScreenInit(pureptr_t fb_addr) {
	if (!rgb_lcd) return nullptr;
	if (!rgb_lcd->Initialize(fb_addr)) return nullptr;
	return rgb_lcd;
}

stduint RgbLcd_LayerCount() { return rgb_lcd ? rgb_lcd->LayerCount() : 0; }

#endif
