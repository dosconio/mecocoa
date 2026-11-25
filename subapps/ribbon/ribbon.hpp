#ifndef MCCA_SUBAPPS_RIBBON_HPP
#define MCCA_SUBAPPS_RIBBON_HPP

#include "aaaaa.h"
#include "c/consio.h"
#include "c/loc.h"
#include "cpp/Witch/Control/Button.hpp"
#include "cpp/Witch/Control/Control-Label.hpp"
#include <time.h>

using namespace uni;
using namespace uni::witch::control;

static constexpr stduint kRibbonClientHeight = 30;
static constexpr stduint kStartButtonWidth = 54;

extern Lango current_lang;
struct tm* ReadRibbonDateTime();

class BufferSheet : public SheetTrait {
public:
	BufferSheet(Color* buf, stduint w, stduint h) : SheetTrait() {
		sheet_area = Rectangle(Point(0, 0), Size2(w, h));
		sheet_buffer = buf;
	}
	virtual void doshow(void*) override {}
	virtual void onrupt(SheetEvent, Point, ...) override {}
};

static inline Color RColor(uint32 argb) {
	return argb;
}

static inline void FillRect(Color* buffer, stduint pitch, stduint width, stduint height,
	stduint x, stduint y, stduint w, stduint h, Color color)
{
	if (!buffer || x >= width || y >= height) return;
	if (x + w > width) w = width - x;
	if (y + h > height) h = height - y;
	for0(row, h) {
		Color* p = buffer + (y + row) * pitch + x;
		for0(col, w) *p++ = color;
	}
}

static inline void BlitButton(Color* buffer, stduint buf_w, Button& btn) {
	btn.doshow(nullptr);
	if (!btn.sheet_buffer) return;
	for0(y, btn.sheet_area.height) {
		for0(x, btn.sheet_area.width) {
			buffer[(btn.sheet_area.y + y) * buf_w + btn.sheet_area.x + x] =
				btn.sheet_buffer[y * btn.sheet_area.width + x];
		}
	}
}

static inline bool HasNonAscii(rostr s) {
	if (!s) return false;
	while (*s) {
		if ((byte)*s >= 0x80) return true;
		s++;
	}
	return false;
}

static inline stduint MeasureUtf8DisplayWidth(rostr s) {
	if (!s) return 0;
	String tmp(s);
	stduint w = 0;
	for (auto cp : tmp.chars()) {
		w += ((uint32)cp >= 0x100) ? 16 : 8;
	}
	return w;
}

static inline void DrawButtonTextIfNonAscii(stdsint form_id, const Button& btn) {
	if (HasNonAscii(btn.text.reference())) {
		Point pos = btn.sheet_area.getVertex() + btn.getTextVertex();
		sys_draw_default_string(form_id, pos, btn.text.reference(), Color::Black);
	}
}

static inline void DrawPopupText(BufferSheet& sheet, stdsint x, stdsint y, rostr str, Color col) {
	if (!str || !*str || HasNonAscii(str)) return;
	DrawString_16(sheet, Point2(x, y), str, col);
}

static inline void FlushPopupTextIfNonAscii(stdsint form_id, stdsint x, stdsint y, rostr str, Color col) {
	if (!str || !*str || !HasNonAscii(str)) return;
	sys_draw_default_string(form_id, Point(x, y), str, col);
}

// Start Menu module (start.cpp)
void ApplyStartMenuLango(Lango lang);
void RunStartMenuPopup(Size2 screen);

// Calendar & Clock module (calendar.cpp)
void ApplyCalendarLango(Lango lang);
void RunCalendarPopup(Size2 screen);

// Volume Tray & Popup module (volume.cpp)
void ApplyVolumeLango(Lango lang);
void InitVolumeState();
bool HasAudioService();
void DrawSpeakerTrayIcon(Color* buffer, stduint pitch, stduint width, stduint height, stduint x, stduint y);
bool RunVolumePopup(Size2 screen, stduint anchor_x);

#endif
