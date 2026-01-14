#ifndef CONSOLE_HPP_
#define CONSOLE_HPP_

// without boundary check
#define defVconIface(cname,unit) class cname : public VideoControlInterface {\
	unit& Locate(const Point& disp) const;\
public:\
	virtual ~cname() = default; cname() {}\
	virtual void SetCursor(const Point& disp) const override;\
	virtual Point GetCursor() const override;\
	virtual void DrawPoint(const Point& disp, Color color) const override;\
	virtual void DrawRectangle(const Rectangle& rect) const override;\
	virtual void DrawFont(const Point& disp, const DisplayFont& font) const override;\
	virtual Color GetColor(Point p) const override;\
};

#include <cpp/Device/_Video.hpp>

// ---- ---- ---- ---- X86 ---- ---- ---- ---- //
#if _MCCA==0x8632
#define TTY_NUMBER 4
defVconIface(GloScreen, uint8);
extern BareConsole* BCONS0;
extern BareConsole* BCONS[TTY_NUMBER];

void cons_init();

// ---- ---- ---- ---- X64 ---- ---- ---- ---- //
#elif _MCCA==0x8664
defVconIface(GloScreenARGB8888, uint32);
defVconIface(GloScreenABGR8888, uint32);

class Cursor {
public:
	Cursor(VideoControlInterface* writer, Color erase_color, Point initial_position);
	void MoveRelative(Size2dif displacement);
private:
	VideoControlInterface* pixel_writer_ = nullptr;
	Color erase_color_;
	Point position_;
};




#endif

#endif
