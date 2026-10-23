// ASCII CPP TAB4 CRLF
// Docutitle: (Device) VMware SVGA-II Graphics Driver
// Attribute: Arn-Covenant Any-Architect Bit-32mode Non-Dependence
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#include <cpp/Device/Video/VMware-SVGA.hpp>

#if (_MCCA & 0xFF00) == 0x8600

static bool EnsureFramebufferMapped(const uni::Slice& physical_range) {
	if (!physical_range.address || !physical_range.length) return false;
	#if CONFIG_ENABLE_MMU
	return Memory::MapFramebufferPages(physical_range.address,
		physical_range.length);
	#else
	return true;
	#endif
}

class VmwareVideoDevice : public VideoDevice {
public:
	FramebufferInfo fb_info;
	uni::ScreenBridge renderer;
	uni::VmwareSvgaDevice hw;
	VmwareVideoDevice() : renderer(fb_info) {}

	virtual const FramebufferInfo& GetFramebuffer() const override { return fb_info; }
	virtual bool setMode(const VideoMode& mode) override {
		if (mode.format != uni::PixelFormat::ARGB8888) return false;
		const uint32 width = mode.resolution.x;
		const uint32 height = mode.resolution.y;
		const uint32 max_width = hw.ReadRegister(uni::VmwareSvgaDevice::REG_MAX_WIDTH);
		const uint32 max_height = hw.ReadRegister(uni::VmwareSvgaDevice::REG_MAX_HEIGHT);
		const uint64 required = uint64(width) * height * sizeof(uint32);
		const uint64 vram_size = hw.GetVramSize();
		if (!width || !height || (max_width && width > max_width) ||
			(max_height && height > max_height) || !required ||
			(vram_size && required > vram_size)) {
			plogwarn("[VMwareSVGA] Reject mode %ux%u max=%ux%u vram=%u",
				width, height, max_width, max_height, uint32(vram_size));
			return false;
		}

		Size2 previous_resolution;
		uint32_t previous_bpp = 0, previous_pitch = 0;
		if (!hw.ReadMode(previous_resolution, previous_bpp, previous_pitch) ||
			!hw.setMode(width, height, 32)) return false;

		uint32_t pitch = 0, bpp = 0;
		Size2 resolution;
		const bool valid_mode = hw.ReadMode(resolution, bpp, pitch) &&
			resolution.x == width && resolution.y == height && bpp == 32 &&
			uint64(pitch) >= uint64(width) * sizeof(uint32) &&
			(!vram_size || uint64(pitch) * height <= vram_size);
		if (!valid_mode) {
			(void)hw.setMode(previous_resolution.x, previous_resolution.y,
				previous_bpp);
			return false;
		}

		const stduint fb_size = hw.GetFramebufferSize() ?
			hw.GetFramebufferSize() : stduint(uint64(pitch) * height);
		const uni::Slice new_range{ fb_info.physical_range.address, fb_size };
		if (!EnsureFramebufferMapped(new_range)) {
			(void)hw.setMode(previous_resolution.x, previous_resolution.y,
				previous_bpp);
			return false;
		}
		fb_info.screen_size = resolution;
		fb_info.bpp = bpp;
		fb_info.pitch = pitch;
		fb_info.format = uni::PixelFormat::ARGB8888;
		fb_info.physical_range = new_range;
		hw.Flush(Rectangle(Point(0, 0), resolution, Color::Black));
		return true;
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
	dev->fb_info.physical_range = uni::Slice{ (stduint)bar_fb->start + fb_offset, fb_size };
	if (!EnsureFramebufferMapped(dev->fb_info.physical_range)) {
		plogwarn("[VMwareSVGA] Failed to map framebuffer");
		delete dev;
		return false;
	}

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
