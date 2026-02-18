#ifndef CONSOLE_HPP_
#define CONSOLE_HPP_

// without boundary check
#define defVconIface(cname,unit) class cname : public VideoControlInterface {\
public:\
	unit& Locate(const Point& disp) const;\
	virtual ~cname() = default; cname() {}\
	virtual void SetCursor(const Point& disp) const override;\
	virtual Point GetCursor() const override;\
	virtual void DrawPoint(const Point& disp, Color color) const override;\
	virtual void DrawRectangle(const Rectangle& rect) const override;\
	virtual void DrawFont(const Point& disp, const DisplayFont& font) const override;\
	virtual Color GetColor(Point p) const override;\
};

#include <cpp/Device/_Video.hpp>
#include <c/driver/mouse.h>

#if (_MCCA & 0xFF00) == 0x8600
extern uni::LayerManager global_layman;
#endif

#if (_MCCA & 0xFF00) == 0x8600
struct KeyboardBridge : public OstreamTrait // // scan code set 1
{
	virtual int out(const char* str, stduint len) override;
};
#endif

// Cursor
#if (_MCCA & 0xFF00) == 0x8600
class Cursor: public uni::SheetTrait
{
public:
	virtual void doshow(void* _) override;
	virtual void onrupt(uni::SheetEvent event, Point rel_p, ...) override {}
public:// single instance
	static Cursor* global_cursor;
	static SheetTrait* moving_sheet;
	static bool mouse_btnl_dn;
	static bool mouse_btnm_dn;
	static bool mouse_btnr_dn;
public:
	Cursor(uni::VideoControlInterface* writer) : uni::SheetTrait(), pixel_writer_{ writer } {}
	void setSheet(LayerManager& layman, const Point& vertex);
public:
	uni::VideoControlInterface* pixel_writer_ = nullptr;
};
#endif


#if (_MCCA & 0xFF00) == 0x8600
#define TTY_NUMBER 4

// defVconIface(GloScreenRGB888, uint8);
defVconIface(GloScreenARGB8888, uint32);
defVconIface(GloScreenABGR8888, uint32);

extern bool ento_gui;

void cons_init();

void hand_mouse(MouseMessage mmsg);
void hand_kboard(keyboard_event_t mmsg);


#endif

#endif
