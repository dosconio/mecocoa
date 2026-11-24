#include "aaaaa.h"
#include "c/consio.h"
#include "c/datime.h"
#include "c/loc.h"
#include "cpp/Format/ConfigText/TOML.hpp"
#include "cpp/Witch/Control/Button.hpp"
#include "cpp/Witch/Control/Control-Label.hpp"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include "c/arith.h"
#include <c/format/picture/PNG.h>
#include <cpp/trait/StorageTrait.hpp>

static void ScaleImage(const Color* src, stduint src_w, stduint src_h,
	Color* dst, stduint dst_w, stduint dst_h)
{
	if (!src || !dst || !src_w || !src_h || !dst_w || !dst_h) return;
	for0(dy, dst_h) {
		stduint sy = (dy * src_h) / dst_h;
		if (sy >= src_h) sy = src_h - 1;
		for0(dx, dst_w) {
			stduint sx = (dx * src_w) / dst_w;
			if (sx >= src_w) sx = src_w - 1;
			dst[dy * dst_w + dx] = src[sy * src_w + sx];
		}
	}
}

static void TryLoadWallpaper(Size2 target_screen) {
	// VOLUMEPATH is absent when the virtual root was not mounted yet at exec time, hence the literal probes below
	const char* vroot = getenv("VOLUMEPATH");
	if (!vroot || !*vroot) vroot = getenv("VRPATH");
	char vroot_wallpaper[256];
	rostr kWallpaperPaths[3];
	stduint path_count = 0;
	if (vroot && *vroot) {
		sprintf(vroot_wallpaper, "%s/demo/wallpp.png", vroot);
		kWallpaperPaths[path_count++] = vroot_wallpaper;
	}
	kWallpaperPaths[path_count++] = "/mnt/ide2.0/demo/wallpp.png";
	kWallpaperPaths[path_count++] = "/mnt/ahci1.0/demo/wallpp.png";//{TODO} drop these literals when VRPATH is always present
	FILE* fp = nullptr;
	for (int retry = 0; retry < 3; ++retry) {
		for (stduint i = 0; i < path_count; i++) {
			fp = fopen(kWallpaperPaths[i], "rb");
			if (fp) break;
		}
		if (fp) break;
		sysrest(1, 0); // Wait 1 second before retry
	}

	if (!fp) {
		plogwarn("ribbon: no wallpaper at %s", kWallpaperPaths[0]);
		return;
	}

	fseek(fp, 0, SEEK_END);
	long fileSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (fileSize <= 0) {
		plogwarn("ribbon: wallpaper size %d", (int)fileSize);
		fclose(fp);
		return;
	}

	FileBlockDevice storage(fp, (stduint)fileSize);
	PNGCodec pngCodec;
	bool matched = false;
	ImageResult probe_res = pngCodec.Probe(storage, matched);
	if (probe_res != ImageResult::OK || !matched) {
		plogwarn("ribbon: PNG probe res=%d matched=%d", (int)probe_res, (int)matched);
		fclose(fp);
		return;
	}
	{
		ImageBuffer imgBuf;
		ImageBufferClear(imgBuf);
		StdMalloc myMalloc;
		ImageDecodeOptions options;
		ImageDecodeOptionsInit(options);

		ImageResult res = pngCodec.Decode(storage, imgBuf, myMalloc, options);
		if (res != ImageResult::OK || !imgBuf.pixels || !imgBuf.width || !imgBuf.height) {
			plogwarn("ribbon: PNG decode res=%d %ux%u pix=%d", (int)res, (unsigned)imgBuf.width, (unsigned)imgBuf.height, imgBuf.pixels ? 1 : 0);
			if (imgBuf.pixels) ImageBufferFree(imgBuf);
			fclose(fp);
			return;
		}
		stdsint set_ret = -1;
		if (target_screen.x > 0 && target_screen.y > 0 &&
			(imgBuf.width != target_screen.x || imgBuf.height != target_screen.y)) {
			Color* scaled = (Color*)malloc(target_screen.x * target_screen.y * sizeof(Color));
			if (!scaled) {
				plogwarn("ribbon: scale buffer failed for %ux%u", (unsigned)target_screen.x, (unsigned)target_screen.y);
				ImageBufferFree(imgBuf);
				fclose(fp);
				return;
			}
			ScaleImage((Color*)imgBuf.pixels, imgBuf.width, imgBuf.height, scaled, target_screen.x, target_screen.y);
			set_ret = sys_set_wallpaper(scaled, target_screen.x, target_screen.y);
			free(scaled);
		}
		else {
			set_ret = sys_set_wallpaper(imgBuf.pixels, imgBuf.width, imgBuf.height);
		}
		if (set_ret < 0) plogwarn("ribbon: set wallpaper failed ret=%d", (int)set_ret);
		ImageBufferFree(imgBuf);
	}
	fclose(fp);
}

using namespace uni;
using namespace uni::witch::control;

static Button start_btn("Start");
static Button btn_shutdown("Shut down");
static Button btn_reboot("Reboot");
static Button btn_terminal("Terminal");
static Button btn_cal_prev("<");
static Button btn_cal_next(">");
static Button btn_cal_today("Now");
static Label time_label("");
static char cached_time_str[32] = "";
static Lango current_lang = Lango_English;
static Rectangle clock_area;
static bool clock_was_pressed = false;

static void ApplyLango(Lango lang) {
	current_lang = lang;
	switch (lang) {
	case Lango_Japanese:
		start_btn.text = "開始";
		btn_shutdown.text = "電源切";
		btn_reboot.text = "再起動";
		btn_terminal.text = "端末";
		btn_cal_today.text = "今";
		break;
	case Lango_Chinese:
		start_btn.text = "开始";
		btn_shutdown.text = "关机";
		btn_reboot.text = "重启";
		btn_terminal.text = "终端";
		btn_cal_today.text = "今";
		break;
	default:
		start_btn.text = "Start";
		btn_shutdown.text = "Shut down";
		btn_reboot.text = "Reboot";
		btn_terminal.text = "Terminal";
		btn_cal_today.text = "Now";
		break;
	}
}

static void TryLoadConfigLango() {
	const char* vroot = getenv("VOLUMEPATH");
	if (!vroot || !*vroot) vroot = getenv("VRPATH");
	String vroot_conf;
	rostr kConfPaths[3];
	stduint path_count = 0;
	if (vroot && *vroot) {
		vroot_conf = String::newFormat("%s/.mcca.toml", vroot);
		kConfPaths[path_count++] = vroot_conf.reference();
	}
	kConfPaths[path_count++] = "/mnt/ide2.0/.mcca.toml";
	kConfPaths[path_count++] = "/mnt/ahci1.0/.mcca.toml";

	Lango lang = Lango_English;
	for (int retry = 0; retry < 3; ++retry) {
		for (stduint i = 0; i < path_count; i++) {
			HostFile file(kConfPaths[i], FileOpenType::Read);
			if (file) {
				ConfigTOML toml(file);
				if (toml.Probe()) {
					String lang_val = toml.get("root/lango");
					lang = ParseLango(lang_val.reference());
				}
				ApplyLango(lang);
				return;
			}
		}
		sysrest(1, 0);
	}

	ApplyLango(lang);
}

static stdsint RequestNewTerminal() {
	stduint buf[1] = { 0 };
	CommMsg msg;
	msg.data.address = _IMM(buf);
	msg.data.length = sizeof(buf);
	msg.type = _IMM(GraphicMsg::NEW_TERM);
	syscomm(1, Task_ConsoleVideo, &msg);
	syscomm(0, Task_ConsoleVideo, &msg);
	return (stdsint)buf[0];
}

static bool HasNonAscii(rostr s) {
	if (!s) return false;
	while (*s) {
		if ((byte)*s >= 0x80) return true;
		s++;
	}
	return false;
}

static void DrawButtonTextIfNonAscii(stdsint form_id, const Button& btn) {
	if (HasNonAscii(btn.text.reference())) {
		Point pos = btn.sheet_area.getVertex() + btn.getTextVertex();
		sys_draw_default_string(form_id, pos, btn.text.reference(), Color::Black);
	}
}

static constexpr stduint kRibbonClientHeight = 30;
static constexpr stduint kStartButtonWidth = 54;

static Color RColor(uint32 argb)
{
	return argb;
}

static void FillRect(Color* buffer, stduint pitch, stduint width, stduint height,
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

static struct tm* ReadRibbonDateTime() {
	// Check raw syscall failure before a C library can normalize it into a date.
	if (sysdate() == uint32(-1) || systime() == uint32(-1)) return nullptr;
	time_t now = time(nullptr);
	return now != time_t(-1) ? localtime(&now) : nullptr;
}

static constexpr stduint kMaxTaskbarWindows = 16;
static WindowInfo cached_win_list[kMaxTaskbarWindows] = {};
static stduint cached_win_count = 0;
static Button* taskbar_buttons[kMaxTaskbarWindows] = {};
static bool taskbar_btn_was_pressed[kMaxTaskbarWindows] = {};

static bool CheckWindowListChanged(const WindowInfo* new_list, stduint new_count, const WindowInfo* old_list, stduint old_count) {
	if (new_count != old_count) return true;
	for (stduint i = 0; i < new_count; i++) {
		if (new_list[i].pid != old_list[i].pid ||
			new_list[i].form_id != old_list[i].form_id ||
			new_list[i].state != old_list[i].state ||
			new_list[i].is_top != old_list[i].is_top) {
			return true;
		}
		const char* s1 = new_list[i].title;
		const char* s2 = old_list[i].title;
		while (*s1 && *s1 == *s2) { s1++; s2++; }
		if (*s1 != *s2) return true;
	}
	return false;
}

static void DrawRibbon(Color* buffer, stduint width, stduint height)
{
	FillRect(buffer, width, width, height, 0, 0, width, height, RColor(0xFFC0C0C0));
	FillRect(buffer, width, width, height, 0, 0, width, 1, RColor(0xFFFFFFFF));
	FillRect(buffer, width, width, height, 0, 1, width, 1, RColor(0xFFDFDFDF));

	stduint button_y = 3;
	stduint button_h = height > 6 ? height - 6 : height;

	start_btn.sheet_area = Rectangle(Point(3, button_y), Size2(kStartButtonWidth, button_h));
	start_btn.doshow(nullptr);

	if (start_btn.sheet_buffer) {
		for0(y, start_btn.sheet_area.height) {
			for0(x, start_btn.sheet_area.width) {
				buffer[(start_btn.sheet_area.y + y) * width + start_btn.sheet_area.x + x] =
					start_btn.sheet_buffer[y * start_btn.sheet_area.width + x];
			}
		}
	}

	// Clock display on the bottom right
	constexpr stduint kClockWidth = 96;
	stduint clock_x = width > (kClockWidth + 6) ? width - kClockWidth - 4 : 0;
	stduint clock_y = button_y;
	clock_area = Rectangle(Point(clock_x, clock_y), Size2(kClockWidth, button_h));

	// Sunken box background & borders
	FillRect(buffer, width, width, height, clock_x, clock_y, kClockWidth, button_h, RColor(0xFFC0C0C0));
	FillRect(buffer, width, width, height, clock_x, clock_y, kClockWidth, 1, RColor(0xFF808080));
	FillRect(buffer, width, width, height, clock_x, clock_y, 1, button_h, RColor(0xFF808080));
	FillRect(buffer, width, width, height, clock_x, clock_y + button_h - 1, kClockWidth, 1, RColor(0xFFFFFFFF));
	FillRect(buffer, width, width, height, clock_x + kClockWidth - 1, clock_y, 1, button_h, RColor(0xFFFFFFFF));

	// Get current time string (e.g. "Jan02:18:08")
	struct tm* tm_info = ReadRibbonDateTime();
	char time_buf[32] = "0/0/0 0:0:0";
	if (tm_info) {
		strftime(time_buf, sizeof(time_buf), "%b%d:%H:%M", tm_info);
	}
	for0(i, 32) cached_time_str[i] = time_buf[i];

	time_label.text = time_buf;
	constexpr stduint kTextWidth = 11 * 8; // 88 px
	constexpr stduint kTextHeight = 16;
	stdsint text_x = clock_x + (kClockWidth > kTextWidth ? (kClockWidth - kTextWidth) / 2 : 2);
	stdsint text_y = clock_y + (button_h > kTextHeight ? (button_h - kTextHeight) / 2 : 2);
	time_label.sheet_area = Rectangle(Point(text_x, text_y), Size2(kTextWidth, kTextHeight));
	if (time_label.sheet_buffer) {
		free(time_label.sheet_buffer);
		time_label.sheet_buffer = nullptr;
	}
	time_label.doshow(nullptr);
	if (time_label.sheet_buffer) {
		for0(y, time_label.sheet_area.height) {
			for0(x, time_label.sheet_area.width) {
				Color c = time_label.sheet_buffer[y * time_label.sheet_area.width + x];
				if (c != 0) {
					buffer[(time_label.sheet_area.y + y) * width + time_label.sheet_area.x + x] = c;
				}
			}
		}
	}

	// Layout and render taskbar buttons from cached_win_list
	stduint cur_x = kStartButtonWidth + 8;
	stduint right_bound = clock_x > 8 ? clock_x - 8 : cur_x;
	stduint avail_w = right_bound > cur_x ? right_bound - cur_x : 0;
	stduint item_w = 120;
	if (cached_win_count > 0 && cached_win_count * item_w > avail_w) {
		item_w = avail_w / cached_win_count;
		if (item_w < 36) item_w = 36;
	}

	for (stduint i = 0; i < cached_win_count; i++) {
		if (cur_x + item_w > right_bound) break;
		if (!taskbar_buttons[i]) {
			taskbar_buttons[i] = new Button("");
		}
		taskbar_buttons[i]->text = cached_win_list[i].title;
		// Top active window is visually pressed; background/minimized is unpressed
		taskbar_buttons[i]->pressed = (cached_win_list[i].is_top != 0 && cached_win_list[i].state != 1);
		taskbar_buttons[i]->sheet_area = Rectangle(Point(cur_x, button_y), Size2(item_w > 4 ? item_w - 4 : item_w, button_h));
		if (taskbar_buttons[i]->sheet_buffer) {
			free(taskbar_buttons[i]->sheet_buffer);
			taskbar_buttons[i]->sheet_buffer = nullptr;
		}
		taskbar_buttons[i]->doshow(nullptr);
		if (taskbar_buttons[i]->sheet_buffer) {
			for0(y, taskbar_buttons[i]->sheet_area.height) {
				for0(x, taskbar_buttons[i]->sheet_area.width) {
					buffer[(taskbar_buttons[i]->sheet_area.y + y) * width + taskbar_buttons[i]->sheet_area.x + x] =
						taskbar_buttons[i]->sheet_buffer[y * taskbar_buttons[i]->sheet_area.width + x];
				}
			}
		}
		cur_x += item_w;
	}
}

static void FlushRibbon(stdsint form_id) {
	sys_update_form(form_id, nullptr);
	DrawButtonTextIfNonAscii(form_id, start_btn);
	for (stduint i = 0; i < cached_win_count; i++) {
		if (taskbar_buttons[i]) {
			DrawButtonTextIfNonAscii(form_id, *taskbar_buttons[i]);
		}
	}
}

static void FlushStartMenu(stdsint sm_id) {
	sys_update_form(sm_id, nullptr);
	DrawButtonTextIfNonAscii(sm_id, btn_shutdown);
	DrawButtonTextIfNonAscii(sm_id, btn_reboot);
	DrawButtonTextIfNonAscii(sm_id, btn_terminal);
}

static Size2 GetScreenSize()
{
	Size2 screen = {};
	if (sys_get_screen_size(&screen) != 0 || screen.x < 80 || screen.y < 60) {
		screen = Size2(640, 480);
	}
	return screen;
}

static void DrawStartMenu(Color* buffer, stduint menu_w, stduint menu_h) {
	FillRect(buffer, menu_w, menu_w, menu_h, 0, 0, menu_w, menu_h, RColor(0xFFC6C6C6));
	FillRect(buffer, menu_w, menu_w, menu_h, 0, 0, menu_w, 1, RColor(0xFFFFFFFF));
	FillRect(buffer, menu_w, menu_w, menu_h, 0, 0, 1, menu_h, RColor(0xFFFFFFFF));
	FillRect(buffer, menu_w, menu_w, menu_h, menu_w - 1, 0, 1, menu_h, RColor(0xFF000000));
	FillRect(buffer, menu_w, menu_w, menu_h, 0, menu_h - 1, menu_w, 1, RColor(0xFF000000));

	btn_shutdown.sheet_area = Rectangle(Point(5, 5), Size2(190, 30));
	btn_shutdown.doshow(nullptr);
	if (btn_shutdown.sheet_buffer) {
		for0(y, btn_shutdown.sheet_area.height) {
			for0(x, btn_shutdown.sheet_area.width) {
				buffer[(btn_shutdown.sheet_area.y + y) * menu_w + btn_shutdown.sheet_area.x + x] =
					btn_shutdown.sheet_buffer[y * btn_shutdown.sheet_area.width + x];
			}
		}
	}

	btn_reboot.sheet_area = Rectangle(Point(5, 40), Size2(190, 30));
	btn_reboot.doshow(nullptr);
	if (btn_reboot.sheet_buffer) {
		for0(y, btn_reboot.sheet_area.height) {
			for0(x, btn_reboot.sheet_area.width) {
				buffer[(btn_reboot.sheet_area.y + y) * menu_w + btn_reboot.sheet_area.x + x] =
					btn_reboot.sheet_buffer[y * btn_reboot.sheet_area.width + x];
			}
		}
	}

	btn_terminal.sheet_area = Rectangle(Point(5, 75), Size2(190, 30));
	btn_terminal.doshow(nullptr);
	if (btn_terminal.sheet_buffer) {
		for0(y, btn_terminal.sheet_area.height) {
			for0(x, btn_terminal.sheet_area.width) {
				buffer[(btn_terminal.sheet_area.y + y) * menu_w + btn_terminal.sheet_area.x + x] =
					btn_terminal.sheet_buffer[y * btn_terminal.sheet_area.width + x];
			}
		}
	}
}

class BufferSheet : public SheetTrait {
public:
	BufferSheet(Color* buf, stduint w, stduint h) : SheetTrait() {
		sheet_area = Rectangle(Point(0, 0), Size2(w, h));
		sheet_buffer = buf;
	}
	virtual void doshow(void*) override {}
	virtual void onrupt(SheetEvent, Point, ...) override {}
};

static constexpr stduint kCalWidth = 256;
static constexpr stduint kCalHeight = 316;
static constexpr stduint kCalGridLeft = 9;
static constexpr stduint kCalColWidth = 34;

static void BlitButton(Color* buffer, stduint buf_w, Button& btn) {
	btn.doshow(nullptr);
	if (!btn.sheet_buffer) return;
	for0(y, btn.sheet_area.height) {
		for0(x, btn.sheet_area.width) {
			buffer[(btn.sheet_area.y + y) * buf_w + btn.sheet_area.x + x] =
				btn.sheet_buffer[y * btn.sheet_area.width + x];
		}
	}
}

static stduint MeasureUtf8DisplayWidth(rostr s) {
	if (!s) return 0;
	String tmp(s);
	stduint w = 0;
	for (auto cp : tmp.chars()) {
		w += ((uint32)cp >= 0x100) ? 16 : 8;
	}
	return w;
}

static void DrawPopupText(BufferSheet& sheet, stdsint x, stdsint y, rostr str, Color col) {
	if (!str || !*str || HasNonAscii(str)) return;
	DrawString_16(sheet, Point2(x, y), str, col);
}

static void FlushPopupTextIfNonAscii(stdsint form_id, stdsint x, stdsint y, rostr str, Color col) {
	if (!str || !*str || !HasNonAscii(str)) return;
	sys_draw_default_string(form_id, Point(x, y), str, col);
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

static stdsint CreateStartMenuForm(Size2 screen, Color** out_buffer, stduint menu_w, stduint menu_h) {
	stduint form_height = kRibbonClientHeight;
	Rectangle rect{
		Point(0, screen.y > form_height + menu_h ? screen.y - form_height - menu_h : 0),
		Size2(menu_w, menu_h)
	};

	stdsint form_id = sys_create_form(~_IMM0, &rect, GraphicFormStyle_Titleless | GraphicFormStyle_Dock);
	if (form_id < 0) return form_id;

	Color* buffer = (Color*)malloc(menu_w * menu_h * sizeof(Color));
	if (!buffer) {
		sys_close_form(form_id);
		return -1;
	}

	sys_set_form_buffer(form_id, buffer);
	DrawStartMenu(buffer, menu_w, menu_h);
	FlushStartMenu(form_id);

	*out_buffer = buffer;
	return form_id;
}

static stdsint CreateRibbonForm(Size2 screen, Color** out_buffer)
{
	stduint form_height = kRibbonClientHeight;
	Rectangle rect{
		Point(0, screen.y > form_height ? screen.y - form_height : 0),
		Size2(screen.x, form_height)
	};

	stdsint form_id = sys_create_form(~_IMM0, &rect, GraphicFormStyle_Titleless | GraphicFormStyle_Dock);
	if (form_id < 0) return form_id;

	stduint client_w = rect.width;
	stduint client_h = rect.height;
	Color* buffer = (Color*)malloc(client_w * client_h * sizeof(Color));
	if (!buffer) {
		sys_close_form(form_id);
		return -1;
	}

	sys_set_form_buffer(form_id, buffer);

	// Initial window list fetch
	WindowInfo all_wins[kMaxTaskbarWindows];
	stduint total_w = 0;
	cached_win_count = 0;
	if (sys_get_window_list(all_wins, kMaxTaskbarWindows, &total_w) == 0) {
		for (stduint i = 0; i < total_w && cached_win_count < kMaxTaskbarWindows; i++) {
			if (all_wins[i].title[0] == '\0') continue;
			cached_win_list[cached_win_count++] = all_wins[i];
		}
	}

	DrawRibbon(buffer, client_w, client_h);
	FlushRibbon(form_id);
	sys_set_timer(form_id, 200);

	*out_buffer = buffer;
	return form_id;
}

int main(int argc, char** argv)
{

	Size2 screen = GetScreenSize();
	TryLoadWallpaper(screen);
	TryLoadConfigLango();
	Color* buffer = nullptr;
	stdsint form_id = CreateRibbonForm(screen, &buffer);
	if (form_id < 0) {
		outsfmt("Ribbon: Failed to create form.\r\n");
		return -1;
	}

	SheetMessage smsg;
	while (sys_fetch_msg(form_id, true, &smsg)) {
		if (smsg.event == SheetEvent::onTimer) {
			Size2 next_screen = GetScreenSize();
			if (next_screen.x != screen.x || next_screen.y != screen.y) {
				screen = next_screen;
				TryLoadWallpaper(screen);
				sys_close_form(form_id);
				if (buffer) free(buffer);
				buffer = nullptr;
				form_id = CreateRibbonForm(screen, &buffer);
				if (form_id < 0) {
					outsfmt("Ribbon: Failed to recreate form.\r\n");
					return -1;
				}
				continue;
			}

			// Check if time changed
			struct tm* tm_now = ReadRibbonDateTime();
			char now_str[32] = "0/0/0 0:0:0";
			if (tm_now) {
				strftime(now_str, sizeof(now_str), "%b%d:%H:%M", tm_now);
			}
			bool time_changed = (StrCompare(now_str, cached_time_str) != 0);

			// Query latest window list from kernel
			WindowInfo latest_wins[kMaxTaskbarWindows];
			stduint total_w = 0;
			stduint filtered_count = 0;
			WindowInfo filtered_wins[kMaxTaskbarWindows];
			bool win_changed = false;
			if (sys_get_window_list(latest_wins, kMaxTaskbarWindows, &total_w) == 0) {
				for (stduint i = 0; i < total_w && filtered_count < kMaxTaskbarWindows; i++) {
					if (latest_wins[i].title[0] == '\0') continue;
					filtered_wins[filtered_count++] = latest_wins[i];
				}

				win_changed = CheckWindowListChanged(filtered_wins, filtered_count, cached_win_list, cached_win_count);
				if (win_changed) {
					cached_win_count = filtered_count;
					for (stduint i = 0; i < filtered_count; i++) {
						cached_win_list[i] = filtered_wins[i];
					}
				}
			}

			if ((time_changed || win_changed) && buffer) {
				DrawRibbon(buffer, screen.x, kRibbonClientHeight);
				FlushRibbon(form_id);
			}
		}
		else if (smsg.event == SheetEvent::onClick || smsg.event == SheetEvent::onLeave || smsg.event == SheetEvent::onMoved) {
			Point rel_p(smsg.args[0], smsg.args[1]);
			stduint para1 = smsg.args[2];

			bool was_pressed = start_btn.pressed;

			if (start_btn.sheet_area.ifContain(rel_p)) {
				start_btn.onrupt(smsg.event, rel_p - start_btn.sheet_area.getVertex(), para1);
			} else {
				start_btn.onrupt(SheetEvent::onLeave, rel_p, 1);
			}

			bool clicked = (was_pressed && !start_btn.pressed && smsg.event == SheetEvent::onClick && start_btn.sheet_area.ifContain(rel_p));

			if (was_pressed != start_btn.pressed && buffer) {
				if (clicked) {
					start_btn.pressed = true; // Keep visually pressed while menu is open
				}
				DrawRibbon(buffer, screen.x, kRibbonClientHeight);
				FlushRibbon(form_id);

				if (clicked) {
					stduint menu_w = 200;
					stduint menu_h = 300;
					Color* sm_buf = nullptr;
					stdsint sm_id = CreateStartMenuForm(screen, &sm_buf, menu_w, menu_h);

					if (sm_id >= 0) {
						bool menu_open = true;
						while (menu_open) {
							SheetMessage mmsg;
							if (sys_fetch_msg(sm_id, true, &mmsg)) {
								if (mmsg.event == SheetEvent::onLeave) {
									menu_open = false;
									break;
								}
								if (mmsg.event == SheetEvent::onClick || mmsg.event == SheetEvent::onLeave || mmsg.event == SheetEvent::onMoved) {
									Point rel_p(mmsg.args[0], mmsg.args[1]);
									stduint para1 = mmsg.args[2];

									bool sd_was_pressed = btn_shutdown.pressed;
									if (btn_shutdown.sheet_area.ifContain(rel_p)) {
										btn_shutdown.onrupt(mmsg.event, rel_p - btn_shutdown.sheet_area.getVertex(), para1);
									} else {
										btn_shutdown.onrupt(SheetEvent::onLeave, rel_p, 1);
									}

									bool rb_was_pressed = btn_reboot.pressed;
									if (btn_reboot.sheet_area.ifContain(rel_p)) {
										btn_reboot.onrupt(mmsg.event, rel_p - btn_reboot.sheet_area.getVertex(), para1);
									} else {
										btn_reboot.onrupt(SheetEvent::onLeave, rel_p, 1);
									}

									bool tm_was_pressed = btn_terminal.pressed;
									if (btn_terminal.sheet_area.ifContain(rel_p)) {
										btn_terminal.onrupt(mmsg.event, rel_p - btn_terminal.sheet_area.getVertex(), para1);
									} else {
										btn_terminal.onrupt(SheetEvent::onLeave, rel_p, 1);
									}

									if (sd_was_pressed != btn_shutdown.pressed ||
										rb_was_pressed != btn_reboot.pressed ||
										tm_was_pressed != btn_terminal.pressed) {
										DrawStartMenu(sm_buf, menu_w, menu_h);
										FlushStartMenu(sm_id);
									}

									if (sd_was_pressed && !btn_shutdown.pressed && mmsg.event == SheetEvent::onClick && btn_shutdown.sheet_area.ifContain(rel_p)) {
										sysshutdown();
										menu_open = false;
									} else if (rb_was_pressed && !btn_reboot.pressed && mmsg.event == SheetEvent::onClick && btn_reboot.sheet_area.ifContain(rel_p)) {
										sysreboot();
										menu_open = false;
									} else if (tm_was_pressed && !btn_terminal.pressed && mmsg.event == SheetEvent::onClick && btn_terminal.sheet_area.ifContain(rel_p)) {
										RequestNewTerminal();
										menu_open = false;
									}
								}
							}
						}

						sys_close_form(sm_id);
						free(sm_buf);

						SheetMessage flush_msg;
						while (sys_fetch_msg(form_id, false, &flush_msg) == 1) {
						}

						start_btn.pressed = false;
						btn_shutdown.pressed = false;
						btn_reboot.pressed = false;
						btn_terminal.pressed = false;
						DrawRibbon(buffer, screen.x, kRibbonClientHeight);
						FlushRibbon(form_id);
					}
				}
			}

			// Handle Clock area click to toggle calendar & clock popup
			bool clock_in = clock_area.ifContain(rel_p);
			bool clock_down = (smsg.event == SheetEvent::onClick) && ((para1 & 0x10) != 0) && clock_in;
			bool clock_clicked = (clock_was_pressed && smsg.event == SheetEvent::onClick && ((para1 & 0x10) == 0) && clock_in);
			if (smsg.event == SheetEvent::onClick) {
				clock_was_pressed = clock_down;
			} else if (smsg.event == SheetEvent::onLeave && para1 == 1) {
				clock_was_pressed = false;
			}

			if (clock_clicked && buffer) {
				struct tm* tm_init = ReadRibbonDateTime();
				word view_year = tm_init ? (word)(1900 + tm_init->tm_year) : 0;
				word view_month = tm_init ? (word)(1 + tm_init->tm_mon) : 0;
				int last_sec = tm_init ? tm_init->tm_sec : -1;
				int last_yday = tm_init ? tm_init->tm_yday : -1;

				Color* cal_buf = nullptr;
				stdsint cal_id = CreateCalendarForm(screen, &cal_buf, view_year, view_month);
				if (cal_id >= 0) {
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
									if (!tm_cur) { view_year = 0; view_month = 0; }
									else if (!view_month) {
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

					SheetMessage flush_msg;
					while (sys_fetch_msg(form_id, false, &flush_msg) == 1) {
					}

					btn_cal_prev.pressed = false;
					btn_cal_next.pressed = false;
					btn_cal_today.pressed = false;
					DrawRibbon(buffer, screen.x, kRibbonClientHeight);
					FlushRibbon(form_id);
				}
			}

			// Handle Taskbar button clicks
			for (stduint i = 0; i < cached_win_count; i++) {
				if (!taskbar_buttons[i]) continue;
				bool was_p = taskbar_btn_was_pressed[i];
				if (taskbar_buttons[i]->sheet_area.ifContain(rel_p)) {
					taskbar_buttons[i]->onrupt(smsg.event, rel_p - taskbar_buttons[i]->sheet_area.getVertex(), para1);
				} else {
					taskbar_buttons[i]->onrupt(SheetEvent::onLeave, rel_p, 1);
				}
				taskbar_btn_was_pressed[i] = taskbar_buttons[i]->pressed;

				bool task_clicked = (was_p && !taskbar_buttons[i]->pressed && smsg.event == SheetEvent::onClick && taskbar_buttons[i]->sheet_area.ifContain(rel_p));
				if (task_clicked) {
					if (cached_win_list[i].is_top != 0 && cached_win_list[i].state != 1) {
						// Already top active: minimize it
						sys_minimize_form(cached_win_list[i].form_id, cached_win_list[i].pid);
						cached_win_list[i].state = 1;
						cached_win_list[i].is_top = 0;
					} else {
						// Background or minimized: restore and bring to top
						sys_restore_form(cached_win_list[i].form_id, cached_win_list[i].pid);
						for (stduint k = 0; k < cached_win_count; k++) {
							cached_win_list[k].is_top = 0;
						}
						if (cached_win_list[i].state == 1) {
							cached_win_list[i].state = 0;
						}
						cached_win_list[i].is_top = 1;
					}
					if (buffer) {
						DrawRibbon(buffer, screen.x, kRibbonClientHeight);
						FlushRibbon(form_id);
					}
					break;
				}
			}
		}
	}

	if (buffer) free(buffer);
	return 0;
}
