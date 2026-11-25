#include "ribbon.hpp"
#include "c/datime.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include "c/arith.h"

static constexpr stduint kCalWidth = 256;
static constexpr stduint kCalHeight = 316;
static constexpr stduint kCalGridLeft = 9;
static constexpr stduint kCalColWidth = 34;

static Button btn_cal_prev("<");
static Button btn_cal_next(">");
static Button btn_cal_today("Now");

void ApplyCalendarLango(Lango lang) {
	if (lang == Lango_Japanese || lang == Lango_Chinese) {
		btn_cal_today.text = "今";
	} else {
		btn_cal_today.text = "Now";
	}
}

static void FormatMonthTitle(word year, word month, char* out, stduint out_sz) {
	static rostr const en_months[12] = {
		"January", "February", "March", "April", "May", "June",
		"July", "August", "September", "October", "November", "December"
	};
	(void)out_sz;
	if (month < 1 || month > 12) month = 1;
	if (current_lang == Lango_Japanese || current_lang == Lango_Chinese) {
		sprintf(out, "%04u年%u月", (unsigned)year, (unsigned)month);
	} else {
		sprintf(out, "%s %04u", en_months[month - 1], (unsigned)year);
	}
}

static rostr GetWeekdayLabel(stduint col) {
	static rostr const en_days[7] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
	static rostr const ja_days[7] = { "日", "月", "火", "水", "木", "金", "土" };
	static rostr const zh_days[7] = { "日", "一", "二", "三", "四", "五", "六" };
	if (col >= 7) return "";
	if (current_lang == Lango_Japanese) return ja_days[col];
	if (current_lang == Lango_Chinese) return zh_days[col];
	return en_days[col];
}

static bool FormatLunarText(word year, byte month, byte day, char* out, stduint out_sz) {
	static rostr const stems[] = { "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸" };
	static rostr const branches[] = { "子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥" };
	static rostr const months[] = { "正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "十一", "十二" };
	static rostr const digits[] = { "一", "二", "三", "四", "五", "六", "七", "八", "九", "十" };
	(void)out_sz;
	struct datime_t date = {};
	LunarDate_t lunar = {};
	date.year = year;
	date.month = (byte)(month - 1);
	date.mday = day;
	if (!GregorianToLunar(&date, &lunar)) return false;

	char day_buf[16] = {};
	if (lunar.day <= 10) sprintf(day_buf, "初%s", digits[lunar.day - 1]);
	else if (lunar.day < 20) sprintf(day_buf, "十%s", digits[lunar.day - 11]);
	else if (lunar.day == 20) sprintf(day_buf, "二十");
	else if (lunar.day < 30) sprintf(day_buf, "廿%s", digits[lunar.day - 21]);
	else sprintf(day_buf, "三十");

	sprintf(out, "%s%s年%s%s月%s",
		stems[(lunar.year - 4) % 10],
		branches[(lunar.year - 4) % 12],
		lunar.leap_month ? "闰" : "",
		months[lunar.month - 1],
		day_buf);
	return true;
}

static void DrawClockHand(LayerManager& lm, stdsint cx, stdsint cy, double rad, double length, Color col, bool thick) {
	double s = 0.0, c = 0.0;
	dblsincos(rad, &s, &c);
	stdsint dx = (stdsint)(s * length + (s >= 0 ? 0.5 : -0.5));
	stdsint dy = (stdsint)(-c * length + (-c >= 0 ? 0.5 : -0.5));
	lm.DrawLine(Point(cx, cy), Size2dif(dx, dy), col);
	if (thick) {
		lm.DrawLine(Point(cx + 1, cy), Size2dif(dx, dy), col);
		lm.DrawLine(Point(cx, cy + 1), Size2dif(dx, dy), col);
	}
}

static void DrawCalendarPopup(Color* buffer, stduint w, stduint h, word view_year, word view_month) {
	FillRect(buffer, w, w, h, 0, 0, w, h, RColor(0xFFC6C6C6));
	FillRect(buffer, w, w, h, 0, 0, w, 1, RColor(0xFFFFFFFF));
	FillRect(buffer, w, w, h, 0, 0, 1, h, RColor(0xFFFFFFFF));
	FillRect(buffer, w, w, h, w - 1, 0, 1, h, RColor(0xFF000000));
	FillRect(buffer, w, w, h, 0, h - 1, w, 1, RColor(0xFF000000));

	VideoControlInterfaceMARGB8888 vcim(buffer, Size2(w, h));
	LayerManager lm(&vcim, Rectangle(Point(0, 0), Size2(w, h)));
	BufferSheet sheet(buffer, w, h);

	struct tm* t = ReadRibbonDateTime();
	int sec = t ? t->tm_sec : 0;
	int min = t ? t->tm_min : 0;
	int hour = t ? t->tm_hour : 0;
	word today_y = t ? (word)(1900 + t->tm_year) : view_year;
	word today_m = t ? (word)(1 + t->tm_mon) : view_month;
	byte today_d = t ? (byte)t->tm_mday : 1;

	// Analog clock dial
	constexpr stdsint cx = 64;
	constexpr stdsint cy = 62;
	constexpr stduint radius = 46;
	lm.Draw(Circle(Point(cx, cy), radius, Color::White, true));
	lm.Draw(Circle(Point(cx, cy), radius, Color::Black, false));

	for (int i = 0; i < 12; i++) {
		double rad = (double)i * (_VAL_PI / 6.0);
		double s = 0.0, c = 0.0;
		dblsincos(rad, &s, &c);
		double r0 = (i % 3 == 0) ? (double)(radius - 7) : (double)(radius - 4);
		double r1 = (double)(radius - 2);
		stdsint x0 = cx + (stdsint)(s * r0 + (s >= 0 ? 0.5 : -0.5));
		stdsint y0 = cy + (stdsint)(-c * r0 + (-c >= 0 ? 0.5 : -0.5));
		stdsint x1 = cx + (stdsint)(s * r1 + (s >= 0 ? 0.5 : -0.5));
		stdsint y1 = cy + (stdsint)(-c * r1 + (-c >= 0 ? 0.5 : -0.5));
		lm.DrawLine(Point(x0, y0), Size2dif(x1 - x0, y1 - y0), Color::Black);
	}

	double hour_rad = ((double)(hour % 12) + (double)min / 60.0) * (_VAL_PI / 6.0);
	double min_rad = ((double)min + (double)sec / 60.0) * (_VAL_PI / 30.0);
	double sec_rad = (double)sec * (_VAL_PI / 30.0);
	DrawClockHand(lm, cx, cy, hour_rad, 24.0, Color::Black, true);
	DrawClockHand(lm, cx, cy, min_rad, 34.0, Color::Black, false);
	DrawClockHand(lm, cx, cy, sec_rad, 39.0, RColor(0xFFCC2222), false);
	lm.Draw(Circle(Point(cx, cy), 3, Color::Black, true));

	// Digital clock & today's date on the right
	if (!t) {
		DrawPopupText(sheet, 134, 44, "0/0/0 0:0:0", Color::Black);
		return;
	}
	char str_buf[48] = {};
	sprintf(str_buf, "%02d:%02d:%02d", hour, min, sec);
	DrawPopupText(sheet, 142, 44, str_buf, Color::Black);
	sprintf(str_buf, "%04u-%02u-%02u", (unsigned)today_y, (unsigned)today_m, (unsigned)today_d);
	DrawPopupText(sheet, 134, 68, str_buf, RColor(0xFF404040));

	// Divider
	FillRect(buffer, w, w, h, 8, 118, w - 16, 1, RColor(0xFF808080));
	FillRect(buffer, w, w, h, 8, 119, w - 16, 1, RColor(0xFFFFFFFF));

	// Navigation buttons and month title
	btn_cal_prev.sheet_area = Rectangle(Point(9, 126), Size2(24, 22));
	btn_cal_today.sheet_area = Rectangle(Point(188, 126), Size2(32, 22));
	btn_cal_next.sheet_area = Rectangle(Point(223, 126), Size2(24, 22));
	BlitButton(buffer, w, btn_cal_prev);
	BlitButton(buffer, w, btn_cal_today);
	BlitButton(buffer, w, btn_cal_next);

	FormatMonthTitle(view_year, view_month, str_buf, sizeof(str_buf));
	stduint title_w = MeasureUtf8DisplayWidth(str_buf);
	stdsint title_x = 36 + (148 > title_w ? (stdsint)(148 - title_w) / 2 : 0);
	DrawPopupText(sheet, title_x, 129, str_buf, Color::Black);

	// Weekday header
	for (stduint col = 0; col < 7; col++) {
		rostr lbl = GetWeekdayLabel(col);
		stduint lw = MeasureUtf8DisplayWidth(lbl);
		stdsint lx = (stdsint)(kCalGridLeft + col * kCalColWidth) + (kCalColWidth > lw ? (stdsint)(kCalColWidth - lw) / 2 : 0);
		Color col_c = Color::Black;
		if (col == 0) col_c = RColor(0xFFAA2222);
		else if (col == 6) col_c = RColor(0xFF2244AA);
		DrawPopupText(sheet, lx, 154, lbl, col_c);
	}

	// Calendar grid box
	constexpr stduint grid_y = 174;
	constexpr stduint grid_w = 7 * kCalColWidth;
	constexpr stduint row_h = 18;
	constexpr stduint grid_h = 6 * row_h + 2;
	FillRect(buffer, w, w, h, kCalGridLeft, grid_y, grid_w, grid_h, Color::White);
	FillRect(buffer, w, w, h, kCalGridLeft, grid_y, grid_w, 1, RColor(0xFF808080));
	FillRect(buffer, w, w, h, kCalGridLeft, grid_y, 1, grid_h, RColor(0xFF808080));
	FillRect(buffer, w, w, h, kCalGridLeft, grid_y + grid_h - 1, grid_w, 1, RColor(0xFFFFFFFF));
	FillRect(buffer, w, w, h, kCalGridLeft + grid_w - 1, grid_y, 1, grid_h, RColor(0xFFFFFFFF));

	unsigned first_wd = weekday(view_year, view_month, 1);
	unsigned days_in_month = moondays(view_year, view_month);
	for (unsigned d = 1; d <= days_in_month; d++) {
		unsigned idx = first_wd + (d - 1);
		unsigned row = idx / 7;
		unsigned col = idx % 7;
		if (row >= 6) break;

		stduint cell_x = kCalGridLeft + 1 + col * kCalColWidth;
		stduint cell_y = grid_y + 1 + row * row_h;
		bool is_today = (view_year == today_y && view_month == today_m && d == today_d);

		Color text_col = Color::Black;
		if (is_today) {
			FillRect(buffer, w, w, h, cell_x + 2, cell_y + 1, kCalColWidth - 4, row_h - 2, RColor(0xFF000080));
			text_col = Color::White;
		} else if (col == 0) {
			text_col = RColor(0xFFAA2222);
		} else if (col == 6) {
			text_col = RColor(0xFF2244AA);
		}

		sprintf(str_buf, "%2u", d);
		stdsint dx = (stdsint)cell_x + ((stdsint)kCalColWidth - 16) / 2;
		DrawPopupText(sheet, dx, (stdsint)cell_y + 1, str_buf, text_col);
	}
}

static void FlushCalendarPopup(stdsint cal_id, word view_year, word view_month) {
	sys_update_form(cal_id, nullptr);
	if (!view_month) return;
	DrawButtonTextIfNonAscii(cal_id, btn_cal_today);

	char str_buf[48] = {};
	FormatMonthTitle(view_year, view_month, str_buf, sizeof(str_buf));
	stduint title_w = MeasureUtf8DisplayWidth(str_buf);
	stdsint title_x = 36 + (148 > title_w ? (stdsint)(148 - title_w) / 2 : 0);
	FlushPopupTextIfNonAscii(cal_id, title_x, 129, str_buf, Color::Black);

	for (stduint col = 0; col < 7; col++) {
		rostr lbl = GetWeekdayLabel(col);
		stduint lw = MeasureUtf8DisplayWidth(lbl);
		stdsint lx = (stdsint)(kCalGridLeft + col * kCalColWidth) + (kCalColWidth > lw ? (stdsint)(kCalColWidth - lw) / 2 : 0);
		Color col_c = Color::Black;
		if (col == 0) col_c = RColor(0xFFAA2222);
		else if (col == 6) col_c = RColor(0xFF2244AA);
		FlushPopupTextIfNonAscii(cal_id, lx, 154, lbl, col_c);
	}

	struct tm* t = ReadRibbonDateTime();
	if (!t) return;
	word lunar_y = t ? (word)(1900 + t->tm_year) : view_year;
	byte lunar_m = t ? (byte)(1 + t->tm_mon) : (byte)view_month;
	byte lunar_d = t ? (byte)t->tm_mday : 1;
	if (view_year != lunar_y || view_month != lunar_m) {
		lunar_y = view_year;
		lunar_m = (byte)view_month;
		lunar_d = 1;
	}
	if (FormatLunarText(lunar_y, lunar_m, lunar_d, str_buf, sizeof(str_buf))) {
		stduint lw = MeasureUtf8DisplayWidth(str_buf);
		stdsint lx = kCalWidth > lw ? (stdsint)(kCalWidth - lw) / 2 : 4;
		FlushPopupTextIfNonAscii(cal_id, lx, 290, str_buf, RColor(0xFF202020));
	}
}

static stdsint CreateCalendarForm(Size2 screen, Color** out_buffer, word view_year, word view_month) {
	stduint form_height = kRibbonClientHeight;
	Rectangle rect{
		Point(screen.x > kCalWidth ? screen.x - kCalWidth : 0,
			screen.y > form_height + kCalHeight ? screen.y - form_height - kCalHeight : 0),
		Size2(kCalWidth, kCalHeight)
	};

	stdsint form_id = sys_create_form(~_IMM0, &rect, GraphicFormStyle_Titleless | GraphicFormStyle_Dock);
	if (form_id < 0) return form_id;

	Color* buffer = (Color*)malloc(kCalWidth * kCalHeight * sizeof(Color));
	if (!buffer) {
		sys_close_form(form_id);
		return -1;
	}

	sys_set_form_buffer(form_id, buffer);
	DrawCalendarPopup(buffer, kCalWidth, kCalHeight, view_year, view_month);
	FlushCalendarPopup(form_id, view_year, view_month);
	sys_set_timer(form_id, 500);

	*out_buffer = buffer;
	return form_id;
}

void RunCalendarPopup(Size2 screen) {
	struct tm* tm_init = ReadRibbonDateTime();
	word view_year = tm_init ? (word)(1900 + tm_init->tm_year) : 0;
	word view_month = tm_init ? (word)(1 + tm_init->tm_mon) : 0;
	int last_sec = tm_init ? tm_init->tm_sec : -1;
	int last_yday = tm_init ? tm_init->tm_yday : -1;

	Color* cal_buf = nullptr;
	stdsint cal_id = CreateCalendarForm(screen, &cal_buf, view_year, view_month);
	if (cal_id < 0) return;

	bool cal_open = true;
	while (cal_open) {
		SheetMessage cmsg;
		if (sys_fetch_msg(cal_id, true, &cmsg)) {
			if (cmsg.event == SheetEvent::onLeave) {
				cal_open = false;
				break;
			}
			if (cmsg.event == SheetEvent::onTimer) {
				struct tm* tm_cur = ReadRibbonDateTime();
				int cur_sec = tm_cur ? tm_cur->tm_sec : -1;
				int cur_yday = tm_cur ? tm_cur->tm_yday : -1;
				if (cur_sec != last_sec || cur_yday != last_yday) {
					bool day_changed = (cur_yday != last_yday);
					if (!tm_cur) {
						view_year = 0;
						view_month = 0;
					} else if (!view_month) {
						view_year = (word)(1900 + tm_cur->tm_year);
						view_month = (word)(1 + tm_cur->tm_mon);
					}
					last_sec = cur_sec;
					last_yday = cur_yday;
					DrawCalendarPopup(cal_buf, kCalWidth, kCalHeight, view_year, view_month);
					if (day_changed) {
						FlushCalendarPopup(cal_id, view_year, view_month);
					} else {
						// Only update the upper clock region to avoid erasing FreeType calendar text below
						Rectangle clock_dirty(Point(0, 0), Size2(kCalWidth, 118));
						sys_update_form(cal_id, &clock_dirty);
					}
				}
			} else if (cmsg.event == SheetEvent::onClick || cmsg.event == SheetEvent::onLeave || cmsg.event == SheetEvent::onMoved) {
				if (!view_month) continue;
				Point crel(cmsg.args[0], cmsg.args[1]);
				stduint cpara1 = cmsg.args[2];

				bool prev_was = btn_cal_prev.pressed;
				if (btn_cal_prev.sheet_area.ifContain(crel)) {
					btn_cal_prev.onrupt(cmsg.event, crel - btn_cal_prev.sheet_area.getVertex(), cpara1);
				} else {
					btn_cal_prev.onrupt(SheetEvent::onLeave, crel, 1);
				}

				bool next_was = btn_cal_next.pressed;
				if (btn_cal_next.sheet_area.ifContain(crel)) {
					btn_cal_next.onrupt(cmsg.event, crel - btn_cal_next.sheet_area.getVertex(), cpara1);
				} else {
					btn_cal_next.onrupt(SheetEvent::onLeave, crel, 1);
				}

				bool today_was = btn_cal_today.pressed;
				if (btn_cal_today.sheet_area.ifContain(crel)) {
					btn_cal_today.onrupt(cmsg.event, crel - btn_cal_today.sheet_area.getVertex(), cpara1);
				} else {
					btn_cal_today.onrupt(SheetEvent::onLeave, crel, 1);
				}

				bool need_redraw = (prev_was != btn_cal_prev.pressed ||
					next_was != btn_cal_next.pressed ||
					today_was != btn_cal_today.pressed);

				if (prev_was && !btn_cal_prev.pressed && cmsg.event == SheetEvent::onClick && btn_cal_prev.sheet_area.ifContain(crel)) {
					if (view_month <= 1) {
						view_month = 12;
						if (view_year > 1) view_year--;
					} else {
						view_month--;
					}
					need_redraw = true;
				} else if (next_was && !btn_cal_next.pressed && cmsg.event == SheetEvent::onClick && btn_cal_next.sheet_area.ifContain(crel)) {
					if (view_month >= 12) {
						view_month = 1;
						view_year++;
					} else {
						view_month++;
					}
					need_redraw = true;
				} else if (today_was && !btn_cal_today.pressed && cmsg.event == SheetEvent::onClick && btn_cal_today.sheet_area.ifContain(crel)) {
					struct tm* tm_cur = ReadRibbonDateTime();
					if (tm_cur) {
						view_year = (word)(1900 + tm_cur->tm_year);
						view_month = (word)(1 + tm_cur->tm_mon);
					}
					need_redraw = true;
				}

				if (need_redraw) {
					DrawCalendarPopup(cal_buf, kCalWidth, kCalHeight, view_year, view_month);
					FlushCalendarPopup(cal_id, view_year, view_month);
				}
			}
		}
	}

	sys_close_form(cal_id);
	free(cal_buf);

	btn_cal_prev.pressed = false;
	btn_cal_next.pressed = false;
	btn_cal_today.pressed = false;
}
