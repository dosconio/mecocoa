// ASCII CPP TAB4 CRLF
// Docutitle: (Device) Bochs VBE Graphics Driver
// Attribute: Arn-Covenant Any-Architect Bit-32mode Non-Dependence
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#include <cpp/Device/Video/Bochs-GrafAda.hpp>

#if (_MCCA & 0xFF00) == 0x8600

static void EnsureFramebufferMapped(const uni::Slice& physical_range) {
	if (!physical_range.address || !physical_range.length) return;
	const stduint page_base = stduint(physical_range.address) & ~0xFFFu;
	const stduint page_end = stduint((physical_range.address + physical_range.length + 0xFFFu) & ~0xFFFu);
	if (page_end <= page_base) return;
	kernel_paging.Map(
		page_base,
		page_base,
		page_end - page_base,
		PAGESIZE_4KB,
		PGPROP_present | PGPROP_writable |
		PGPROP_cache_disable | PGPROP_write_through
	);
	// BAR0 is device framebuffer memory, not ordinary RAM. Refresh every
	// translation after changing its cache type so pixel stores reach QEMU's
	// VRAM dirty tracking promptly.
	for (stduint page = page_base; page < page_end; page += 0x1000) {
		RefreshVirtualAddress(page);
	}
}

class BochsVideoDevice : public VideoDevice {
public:
	FramebufferInfo fb_info;
	uni::ScreenBridge renderer;
	uni::BochsGrafAda hw;
	BochsVideoDevice() : renderer(fb_info) {}

	virtual const FramebufferInfo& GetFramebuffer() const override { return fb_info; }
	virtual bool setMode(const VideoMode& mode) override {
		if (hw.SetResolution(mode.resolution.x, mode.resolution.y, 32)) {
			fb_info.screen_size = mode.resolution;
			fb_info.bpp = 32;
			fb_info.pitch = mode.resolution.x * 4;
			fb_info.format = uni::PixelFormat::ARGB8888;
			fb_info.physical_range = uni::Slice{ fb_info.physical_range.address, (stduint)(fb_info.pitch * fb_info.screen_size.y) };
			EnsureFramebufferMapped(fb_info.physical_range);
			return true;
		}
		return false;
	}
	virtual void Flush(const Rectangle& rect) override {}

	virtual void SetCursor(const Point& disp) const override {}
	virtual Point GetCursor() const override { return {0, 0}; }
	virtual void DrawPoint(const Point& disp, Color color) const override { renderer.DrawPoint(disp, color); }
	virtual void DrawRectangle(const Rectangle& rect) const override { renderer.DrawRectangle(rect); }
	virtual void DrawFont(const Point& disp, const DisplayFont& font, const String& str) const override {}
	virtual Color GetColor(Point p) const override { return renderer.GetColor(p); }
	virtual void DrawPoints(const Rectangle& rect, const Color* base) const override { renderer.DrawPoints(rect, base); }
};

static stdsint BochsVideo_Ctrl(DeviceNode* node, stduint cmd, void* args, stduint flags) {
	(void)flags;
	if (!node || !args) return -1;
	auto* dev = static_cast<BochsVideoDevice*>(node->fields.binding.driver_data);
	if (!dev) return -1;
	if (cmd == (stduint)DeviceCtrlCommand::GetBackingObject) {
		*static_cast<VideoDevice**>(args) = dev;
		return 0;
	}
	switch (VideoCtrlCommand(cmd)) {
	case VideoCtrlCommand::GetFramebufferInfo:
		*static_cast<FramebufferInfo*>(args) = dev->GetFramebuffer();
		return 0;
	case VideoCtrlCommand::SetVideoMode:
		return dev->setMode(*static_cast<VideoMode*>(args)) ? 0 : -1;
	default:
		return -1;
	}
}

static const DeviceNodeOps bochs_video_ops{
	.read = nullptr,
	.send = nullptr,
	.ctrl = BochsVideo_Ctrl,
};

bool BochsVideo_Start(DeviceNode* node) {
	if (!node) return false;
	
	// QEMU/Bochs VGA vendor ID = 0x1234, device ID = 0x1111
	if (node->fields.vendor_id != 0x1234 || node->fields.device_id != 0x1111) {
		return false;
	}
	if (!_GUI_ENABLE) {
		ploginfo("[BochsVBE] Skip start because _GUI_ENABLE=0");
		return false;
	}

	const auto* bar0 = Devsman::FindResource(node, DeviceResourceType::PciBarMmio, 0);
	if (!bar0) {
		plogwarn("[BochsVBE] Failed to find BAR0 (framebuffer)");
		return false;
	}

	auto* dev = new BochsVideoDevice();

	// Read initial resolution from Bochs registers
	uint16_t xres = dev->hw.ReadRegister(uni::BochsGrafAda::INDEX_XRES);
	uint16_t yres = dev->hw.ReadRegister(uni::BochsGrafAda::INDEX_YRES);
	uint16_t bpp = dev->hw.ReadRegister(uni::BochsGrafAda::INDEX_BPP);
	
	// Default to 1024x768x32 if invalid
	if (xres == 0 || yres == 0 || bpp != 32) {
		xres = 1024;
		yres = 768;
		dev->hw.SetResolution(xres, yres, 32);
	}

	dev->fb_info.screen_size = Size2(xres, yres);
	dev->fb_info.bpp = 32;
	dev->fb_info.pitch = xres * 4;
	dev->fb_info.format = uni::PixelFormat::ARGB8888;
	dev->fb_info.physical_range = uni::Slice{ bar0->start, (stduint)(dev->fb_info.pitch * yres) };
	EnsureFramebufferMapped(dev->fb_info.physical_range);

	node->fields.binding.driver_data = dev;
	Devsman::SetOps(node, &bochs_video_ops);
	Consman::AdoptVideoDevice(dev);
	
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
