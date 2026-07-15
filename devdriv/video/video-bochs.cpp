// ASCII CPP TAB4 CRLF
// Docutitle: (Device) Bochs VBE Graphics Driver
// Attribute: Arn-Covenant Any-Architect Bit-32mode Non-Dependence
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"
#include "../../include/devsman.hpp"
#include <cpp/Device/Video/Bochs-GrafAda.hpp>

#if (_MCCA & 0xFF00) == 0x8600

class BochsVideoDevice : public VideoDevice {
public:
	FramebufferInfo fb_info;
	uni::BochsGrafAda hw;

	virtual const FramebufferInfo& GetFramebuffer() const override { return fb_info; }
	virtual bool SetMode(const VideoMode& mode) override {
		if (hw.SetResolution(mode.resolution.x, mode.resolution.y, 32)) {
			fb_info.screen_size = mode.resolution;
			fb_info.bpp = 32;
			fb_info.pitch = mode.resolution.x * 4;
			fb_info.format = uni::PixelFormat::ARGB8888;
			fb_info.physical_range = uni::Slice{ fb_info.physical_range.address, (stduint)(fb_info.pitch * fb_info.screen_size.y) };
			return true;
		}
		return false;
	}
	virtual void Flush(const Rectangle& rect) override {}

	inline uint32& Locate(const Point& disp) const {
		return *(uint32*)((byte*)fb_info.physical_range.address + disp.y * fb_info.pitch + disp.x * 4);
	}

	virtual void SetCursor(const Point& disp) const override {}
	virtual Point GetCursor() const override { return {0, 0}; }
	virtual void DrawPoint(const Point& disp, Color color) const override {
		Locate(disp) = cast<uint32>(color);
	}
	virtual void DrawRectangle(const Rectangle& rect) const override {
		uint32* p = &Locate(rect.getVertex());
		for0(y, rect.height) {
			for0(x, rect.width) p[x] = rect.color.val;
			p = (uint32*)((byte*)p + fb_info.pitch);
		}
	}
	virtual void DrawFont(const Point& disp, const DisplayFont& font, const String& str) const override {}
	virtual Color GetColor(Point p) const override {
		return cast<Color>(Locate(p));
	}
	virtual void DrawPoints(const Rectangle& rect, const Color* base) const override {
		uint32* p = &Locate(rect.getVertex());
		const Color* pbase = base + rect.y * fb_info.screen_size.x + rect.x;
		for0(y, rect.height) {
			for0(x, rect.width) p[x] = pbase[x].val;
			p = (uint32*)((byte*)p + fb_info.pitch);
			pbase += fb_info.screen_size.x;
		}
	}
};

static BochsVideoDevice s_bochs_video;

bool BochsVideo_Start(DeviceNode* node) {
	if (!node) return false;
	
	// QEMU/Bochs VGA vendor ID = 0x1234, device ID = 0x1111
	if (node->fields.vendor_id != 0x1234 || node->fields.device_id != 0x1111) {
		return false;
	}

	const auto* bar0 = Devsman::FindResource(node, DeviceResourceType::PciBarMmio, 0);
	if (!bar0) {
		plogwarn("[BochsVBE] Failed to find BAR0 (framebuffer)");
		return false;
	}

	// Read initial resolution from Bochs registers
	uint16_t xres = s_bochs_video.hw.ReadRegister(uni::BochsGrafAda::INDEX_XRES);
	uint16_t yres = s_bochs_video.hw.ReadRegister(uni::BochsGrafAda::INDEX_YRES);
	uint16_t bpp = s_bochs_video.hw.ReadRegister(uni::BochsGrafAda::INDEX_BPP);
	
	// Default to 1024x768x32 if invalid
	if (xres == 0 || yres == 0 || bpp != 32) {
		xres = 1024;
		yres = 768;
		s_bochs_video.hw.SetResolution(xres, yres, 32);
	}

	s_bochs_video.fb_info.screen_size = Size2(xres, yres);
	s_bochs_video.fb_info.bpp = 32;
	s_bochs_video.fb_info.pitch = xres * 4;
	s_bochs_video.fb_info.format = uni::PixelFormat::ARGB8888;
	s_bochs_video.fb_info.physical_range = uni::Slice{ bar0->start, (stduint)(s_bochs_video.fb_info.pitch * yres) };

	node->fields.binding.driver_data = &s_bochs_video;
	
	ploginfo("[BochsVBE] Mounted at %[x], Res: %ux%u", bar0->start, xres, yres);
	return true;
}

_ESYM_C void R_BOCHS_VIDEO_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_BOCHS_VIDEO{
	.init = R_BOCHS_VIDEO_INIT,
	.name = "BochsVideo",
};

void R_BOCHS_VIDEO_INIT() {
	Devsman::RegisterDriverStarter("video-bochs", BochsVideo_Start);
	Devsman::StartKnownDrivers();
}

#endif // _MCCA == 0x8600