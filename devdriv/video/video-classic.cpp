// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Device] Classic Video
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

static FramebufferInfo s_fb_info;


// GloScreen
#if 0// GloScreenRGB888

uint8& GloScreenRGB888::Locate(const Point& disp) const {
	return *((uint8*)s_fb_info.physical_range.address +
		(disp.x * s_fb_info.bpp >> 3) +
		disp.y * s_fb_info.pitch);// without boundary check
}
void GloScreenRGB888::SetCursor(const Point& disp) const { _TODO; }// MAYBE unused
Point GloScreenRGB888::GetCursor() const { _TODO return {0, 0}; }// MAYBE unused
void GloScreenRGB888::DrawPoint(const Point& disp, Color color) const {
	uint8* p = &Locate(disp);
	*p++ = color.b;
	*p++ = color.g;
	*p++ = color.r;
}
void GloScreenRGB888::DrawRectangle(const Rectangle& rect) const {// RGB 24bpp only
	uint8* p = &Locate(rect.getVertex());
	uint8 comm[3 * 4] = {
		rect.color.b, rect.color.g, rect.color.r,
		rect.color.b, rect.color.g, rect.color.r,
		rect.color.b, rect.color.g, rect.color.r,
		rect.color.b, rect.color.g, rect.color.r,
	};
	uint32 (*com)[3] = (uint32(*)[3])comm;
	for0(y, rect.height) {
		union {
			uint8* pp8;
			uint32* pp32;
		};
		pp8 = p;
		for0r(x, rect.width) {
			if (!(_IMM(pp8) & 0b11) && x >= 12) {
				pp32[0] = treat<uint32>(&comm[4 * 0]);
				pp32[1] = treat<uint32>(&comm[4 * 1]);
				pp32[2] = treat<uint32>(&comm[4 * 2]);
				x -= 3 - 1;
				pp8 += 12;
				continue;
			}
			*pp8++ = rect.color.b;
			*pp8++ = rect.color.g;
			*pp8++ = rect.color.r;
		}
		p += s_fb_info.pitch;
	}
}
void GloScreenRGB888::DrawFont(const Point& disp, const DisplayFont& font, const String& str) const {
	_TODO;
}
Color GloScreenRGB888::GetColor(Point p) const {
	return cast<Color>(Locate(p));
}

#elif (_MCCA & 0xFF00) == 0x8600


inline static stduint VCI_LimitX() { return s_fb_info.screen_size.x; }
uint32& GloScreenARGB8888::Locate(const Point& disp) const {
	return *(uint32*)((byte*)s_fb_info.physical_range.address + disp.y * s_fb_info.pitch + disp.x * 4);
}
uint32& GloScreenABGR8888::Locate(const Point& disp) const {
	return *(uint32*)((byte*)s_fb_info.physical_range.address + disp.y * s_fb_info.pitch + disp.x * 4);
}


void GloScreenARGB8888::SetCursor(const Point& disp) const { _TODO; }// MAYBE unused
Point GloScreenARGB8888::GetCursor() const { _TODO return {0, 0}; }// MAYBE unused
void GloScreenARGB8888::DrawPoint(const Point& disp, Color color) const {
	Locate(disp) = cast<uint32>(color);
}
void GloScreenARGB8888::DrawRectangle(const Rectangle& rect) const {
	uint32* p = &Locate(rect.getVertex());
	for0(y, rect.height) {
		for0(x, rect.width) p[x] = rect.color.val;// cast<uint32>(rect.color);
		p = (uint32*)((byte*)p + s_fb_info.pitch);
	}
}
void GloScreenARGB8888::DrawFont(const Point& disp, const DisplayFont& font, const String& str) const {
	_TODO;
}
Color GloScreenARGB8888::GetColor(Point p) const {
	return cast<Color>(Locate(p));
}

#if defined(_MCCA) && ((_MCCA & 0xFF00) == 0x8600)
__attribute__((target("general-regs-only")))
#endif
void GloScreenARGB8888::DrawPoints(const Rectangle& rect, const Color* base) const {
	uint32* p = &Locate(rect.getVertex());
	const Color* pbase = base + rect.y * VCI_LimitX() + rect.x;
	for0(y, rect.height) {
		for0(x, rect.width) p[x] = pbase[x].val;
		p = (uint32*)((byte*)p + s_fb_info.pitch);
		pbase += VCI_LimitX();
	}
}



void GloScreenABGR8888::SetCursor(const Point& disp) const { _TODO; }// MAYBE unused
Point GloScreenABGR8888::GetCursor() const { _TODO return { 0, 0 }; }// MAYBE unused
void GloScreenABGR8888::DrawPoint(const Point& disp, Color color) const {
	uint32 val = 
		(_IMM(color.r) << 0) |
		(_IMM(color.g) << 8) |
		(_IMM(color.b) << 16) |
		(_IMM(color.a) << 24);
	Locate(disp) = val;
}
void GloScreenABGR8888::DrawRectangle(const Rectangle& rect) const {
	uint32 val;
	val = (_IMM(rect.color.r) << 0)
		| (_IMM(rect.color.g) << 8)
		| (_IMM(rect.color.b) << 16)
		| (_IMM(rect.color.a) << 24);
	uint32* p = &Locate(rect.getVertex());
	for0(y, rect.height) {
		for0(x, rect.width) p[x] = val;
		p = (uint32*)((byte*)p + s_fb_info.pitch);
	}
}
void GloScreenABGR8888::DrawFont(const Point& disp, const DisplayFont& font, const String& str) const {
	_TODO;
}
Color GloScreenABGR8888::GetColor(Point p) const {
	Color color;
	uint32 val = Locate(p);
	color.r = (val >> 0) & 0xFF;
	color.g = (val >> 8) & 0xFF;
	color.b = (val >> 16) & 0xFF;
	color.a = (val >> 24) & 0xFF;
	return color;
}
void GloScreenABGR8888::DrawPoints(const Rectangle& rect, const Color* base) const {
	
}



class ClassicVideoDevice : public VideoDevice {
public:
	uni::VideoControlInterface* renderer;
	ClassicVideoDevice(uni::VideoControlInterface* r = nullptr) : renderer(r) {}

	virtual const FramebufferInfo& GetFramebuffer() const override { return s_fb_info; }
	virtual bool SetMode(const VideoMode& mode) override { return false; }
	virtual void Flush(const Rectangle& rect) override {}

	virtual void SetCursor(const Point& disp) const override { if (renderer) renderer->SetCursor(disp); }
	virtual Point GetCursor() const override { return renderer ? renderer->GetCursor() : Point(0, 0); }
	virtual void DrawPoint(const Point& disp, Color color) const override { if (renderer) renderer->DrawPoint(disp, color); }
	virtual void DrawRectangle(const Rectangle& rect) const override { if (renderer) renderer->DrawRectangle(rect); }
	virtual void DrawFont(const Point& disp, const DisplayFont& font, const String& str) const override { if (renderer) renderer->DrawFont(disp, font, str); }
	virtual Color GetColor(Point p) const override { return renderer ? renderer->GetColor(p) : Color(); }
	virtual void DrawPoints(const Rectangle& rect, const Color* base) const override { if (renderer) renderer->DrawPoints(rect, base); }
};

static GloScreenARGB8888 vga_ARGB8888_internal;
static GloScreenABGR8888 vga_ABGR8888_internal;
static ClassicVideoDevice s_classic_video;

VideoDevice* InitClassicVideo(const FramebufferInfo& info) {
	s_fb_info = info;
	switch (info.format) {
	case uni::PixelFormat::ARGB8888: s_classic_video.renderer = &vga_ARGB8888_internal; break;
	case uni::PixelFormat::ABGR8888: s_classic_video.renderer = &vga_ABGR8888_internal; break;
	default: return nullptr;
	}
	return &s_classic_video;
}

bool ClassicVideo_Start(DeviceNode* node) {
	if (!node || !node->fields.binding.driver_data) return false;
	FramebufferInfo* info = (FramebufferInfo*)node->fields.binding.driver_data;
	
	VideoDevice* screen = InitClassicVideo(*info);
	if (screen) {
		node->fields.binding.driver_data = screen;
		return true;
	}
	return false;
}

static struct _ClassicVideo_AutoReg {
	_ClassicVideo_AutoReg() {
		Devsman::RegisterDriverStarter("classic-video-driver", ClassicVideo_Start);
	}
} _auto_reg;

#endif
