// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Device] Classic Video
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#if (_MCCA & 0xFF00) == 0x8600

_ESYM_C void R_CLASSIC_VIDEO_INIT();

class ClassicVideoDevice : public VideoDevice {
public:
	FramebufferInfo fb_info;
	uni::ScreenBridge renderer;
	ClassicVideoDevice(const FramebufferInfo& info) : fb_info(info), renderer(fb_info) {}

	virtual const FramebufferInfo& GetFramebuffer() const override { return fb_info; }
	virtual bool setMode(const VideoMode& mode) override { return false; }
	virtual void Flush(const Rectangle& rect) override {}

	virtual void SetCursor(const Point& disp) const override { renderer.SetCursor(disp); }
	virtual Point GetCursor() const override { return renderer.GetCursor(); }
	virtual void DrawPoint(const Point& disp, Color color) const override { renderer.DrawPoint(disp, color); }
	virtual void DrawRectangle(const Rectangle& rect) const override { renderer.DrawRectangle(rect); }
	virtual void DrawFont(const Point& disp, const DisplayFont& font, const String& str) const override { renderer.DrawFont(disp, font, str); }
	virtual Color GetColor(Point p) const override { return renderer.GetColor(p); }
	virtual void DrawPoints(const Rectangle& rect, const Color* base) const override { renderer.DrawPoints(rect, base); }
};

VideoDevice* InitClassicVideo(const FramebufferInfo& info) {
	switch (info.format) {
	case uni::PixelFormat::ARGB8888:
	case uni::PixelFormat::ABGR8888:
		break;
	default: return nullptr;
	}
	return new ClassicVideoDevice(info);
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

void R_CLASSIC_VIDEO_INIT() {
	Devsman::RegisterDriverStarter("classic-video-driver", ClassicVideo_Start);
}

#endif
