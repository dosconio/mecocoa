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

#if _MCCA==0x8632
defVconIface(GloScreen, uint8);
#elif _MCCA==0x8664
defVconIface(GloScreenARGB8888, uint32);
defVconIface(GloScreenABGR8888, uint32);
#endif

#endif
