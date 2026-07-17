// ASCII CPP TAB4 CRLF
// Docutitle: (Device) VMware SVGA-II Graphics Driver
// Attribute: Arn-Covenant Any-Architect Bit-32mode Non-Dependence
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#include <cpp/Device/Video/VMware-SVGA.hpp>

#if (_MCCA & 0xFF00) == 0x8600

class VmwareVideoDevice : public VideoDevice {
public:
	FramebufferInfo fb_info;
	uni::ScreenBridge renderer;
	uni::VmwareSvgaDevice hw;
	VmwareVideoDevice() : renderer(fb_info) {}

	virtual const FramebufferInfo& GetFramebuffer() const override { return fb_info; }
	virtual bool setMode(const VideoMode& mode) override {
		if (mode.format != uni::PixelFormat::ARGB8888) return false;
		if (hw.setMode(mode.resolution.x, mode.resolution.y, 32)) {
			uint32_t pitch = 0, bpp = 0;
			Size2 resolution;
			if (!hw.ReadMode(resolution, bpp, pitch)) return false;
			fb_info.screen_size = resolution;
			fb_info.bpp = bpp;
			fb_info.pitch = pitch;
			fb_info.format = uni::PixelFormat::ARGB8888;
			const stduint fb_size = hw.GetFramebufferSize() ? hw.GetFramebufferSize() : (stduint)(pitch * resolution.y);
			fb_info.physical_range = uni::Slice{ fb_info.physical_range.address, fb_size };
			hw.Flush(Rectangle(Point(0, 0), resolution, Color::Black));
			return true;
		}
		return false;
	}
	virtual void Flush(const Rectangle& rect) override { hw.Flush(rect); }

	virtual void SetCursor(const Point& disp) const override {}
	virtual Point GetCursor() const override { return {0, 0}; }
	virtual void DrawPoint(const Point& disp, Color color) const override { renderer.DrawPoint(disp, color); }
	virtual void DrawRectangle(const Rectangle& rect) const override { renderer.DrawRectangle(rect); }
	virtual void DrawFont(const Point& disp, const DisplayFont& font, const String& str) const override {}
	virtual Color GetColor(Point p) const override { return renderer.GetColor(p); }
	virtual void DrawPoints(const Rectangle& rect, const Color* base) const override { renderer.DrawPoints(rect, base); }
};

static bool IsVmwareSvgaDevice(const DeviceNode* node) {
	return node &&
		node->fields.vendor_id == 0x15AD &&
		node->fields.device_id == 0x0405;
}

bool VmwareVideo_Start(DeviceNode* node) {
	if (!IsVmwareSvgaDevice(node)) return false;
	if (!_GUI_ENABLE) {
		ploginfo("[VMwareSVGA] Skip start because _GUI_ENABLE=0");
		return false;
	}

	const auto* bar_io = Devsman::FindResource(node, DeviceResourceType::PciBarIo, 0);
	const auto* bar_fb = Devsman::FindResource(node, DeviceResourceType::PciBarMmio, 1);
	const auto* bar_fifo = Devsman::FindResource(node, DeviceResourceType::PciBarMmio, 2);
	if (!bar_io || !bar_fb || !bar_fifo) {
		plogwarn("[VMwareSVGA] Missing BAR resources");
		return false;
	}

	auto* dev = new VmwareVideoDevice();
	dev->hw.SetIoBase((uint16_t)bar_io->start);
	dev->hw.SetFramebufferBase(bar_fb->start);
	dev->hw.SetFifoBase(bar_fifo->start);
	if (!dev->hw.Initialize()) {
		plogwarn("[VMwareSVGA] Failed to negotiate SVGA version");
		delete dev;
		return false;
	}
	if (!dev->hw.InitializeFifo()) {
		plogwarn("[VMwareSVGA] Failed to initialize FIFO");
		delete dev;
		return false;
	}

	Size2 resolution;
	uint32_t bpp = 0, pitch = 0;
	if (!dev->hw.ReadMode(resolution, bpp, pitch) || bpp != 32) {
		if (!dev->hw.setMode(1024, 768, 32) || !dev->hw.ReadMode(resolution, bpp, pitch)) {
			plogwarn("[VMwareSVGA] Failed to read or set a valid mode");
			delete dev;
			return false;
		}
	}

	const stduint fb_offset = dev->hw.GetFramebufferOffset();
	const stduint fb_size = dev->hw.GetFramebufferSize() ? dev->hw.GetFramebufferSize() : (stduint)(pitch * resolution.y);
	dev->fb_info.screen_size = resolution;
	dev->fb_info.bpp = bpp;
	dev->fb_info.pitch = pitch;
	dev->fb_info.format = uni::PixelFormat::ARGB8888;
	dev->fb_info.physical_range = uni::Slice{ bar_fb->start + fb_offset, fb_size };

	node->fields.binding.driver_data = dev;
	Consman::AdoptVideoDevice(dev);

	ploginfo("[VMwareSVGA] Mounted io=%[x] fb=%[x] fifo=%[x] res=%ux%u caps=%[x]",
		bar_io->start, dev->fb_info.physical_range.address, bar_fifo->start,
		resolution.x, resolution.y, dev->hw.GetCapabilities());
	return true;
}

_ESYM_C void R_VMWARE_VIDEO_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_VMWARE_VIDEO{
	.init = R_VMWARE_VIDEO_INIT,
	.name = "VmwareVideo",
};

void R_VMWARE_VIDEO_INIT() {
	Devsman::RegisterDriverStarter("video-vmware", VmwareVideo_Start);
	Devsman::StartKnownDrivers();
}

#endif // (_MCCA & 0xFF00) == 0x8600
