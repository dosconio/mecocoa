#include "ribbon.hpp"
#include "cpp/Format/ConfigText/TOML.hpp"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <c/format/picture/PNG.h>
#include <cpp/trait/StorageTrait.hpp>

Lango current_lang = Lango_English;
static sint32 ribbon_utc_offset_minutes = 0;

struct tm* ReadRibbonDateTime() {
	timeval_t now{};
	if (sysgettime(&now) < 0) return nullptr;
	const time_t seconds = now.sec + sint64(ribbon_utc_offset_minutes) * 60;
	return gmtime(&seconds);
}

static Button start_btn("Start");
static Label time_label("");
static char cached_time_str[32] = "";
static Rectangle task_strip_area;
static Rectangle speaker_area;
static bool speaker_was_pressed = false;
static Rectangle clock_area;
static bool clock_was_pressed = false;

static constexpr stduint kMaxTaskbarWindows = 16;
static WindowInfo cached_win_list[kMaxTaskbarWindows] = {};
static stduint cached_win_count = 0;
static Button* taskbar_buttons[kMaxTaskbarWindows] = {};
static bool taskbar_btn_was_pressed[kMaxTaskbarWindows] = {};

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
	String vroot_wallpaper;
	rostr kWallpaperPaths[3];
	stduint path_count = 0;
	if (vroot && *vroot) {
		vroot_wallpaper = String::newFormat("%s/demo/wallpp.png", vroot);
		kWallpaperPaths[path_count++] = vroot_wallpaper.reference();
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

static void ApplyLango(Lango lang) {
	current_lang = lang;
	switch (lang) {
	case Lango_Japanese:
		start_btn.text = "開始";
		break;
	case Lango_Chinese:
		start_btn.text = "开始";
		break;
	default:
		start_btn.text = "Start";
		break;
	}
	ApplyStartMenuLango(lang);
	ApplyCalendarLango(lang);
	ApplyVolumeLango(lang);
}

static void TryLoadConfigLango() {
	ribbon_utc_offset_minutes = 0;
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
					const stdsint hours = toml.getNumber("root/utc");
					if (hours >= -24 && hours <= 24) ribbon_utc_offset_minutes = sint32(hours * 60);
				}
				ApplyLango(lang);
				return;
			}
		}
		sysrest(1, 0);
	}

	ApplyLango(lang);
}

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

static bool RefreshCachedWindowList() {
	static WindowInfo latest_wins[kMaxTaskbarWindows];
	static WindowInfo filtered_wins[kMaxTaskbarWindows];
	stduint total_w = 0;
	stduint filtered_count = 0;
	if (sys_get_window_list(latest_wins, kMaxTaskbarWindows, &total_w) != 0) {
		return false;
	}
	for (stduint i = 0; i < total_w && filtered_count < kMaxTaskbarWindows; i++) {
		if (latest_wins[i].title[0] == '\0') continue;
		filtered_wins[filtered_count++] = latest_wins[i];
	}
	bool changed = CheckWindowListChanged(filtered_wins, filtered_count, cached_win_list, cached_win_count);
	if (changed) {
		cached_win_count = filtered_count;
		for (stduint i = 0; i < filtered_count; i++) {
			cached_win_list[i] = filtered_wins[i];
		}
	}
	return changed;
}

static void DrawRibbon(Color* buffer, stduint width, stduint height)
{
	FillRect(buffer, width, width, height, 0, 0, width, height, RColor(0xFFC0C0C0));
	FillRect(buffer, width, width, height, 0, 0, width, 1, RColor(0xFFFFFFFF));
	FillRect(buffer, width, width, height, 0, 1, width, 1, RColor(0xFFDFDFDF));

	stduint button_y = 3;
	stduint button_h = height > 6 ? height - 6 : height;

	start_btn.sheet_area = Rectangle(Point(3, button_y), Size2(kStartButtonWidth, button_h));
	BlitButton(buffer, width, start_btn);

	// Clock and optional speaker tray box on the bottom right
	constexpr stduint kClockWidth = 96;
	const bool show_speaker = HasAudioService();
	const stduint spk_tray_w = show_speaker ? 22 : 0;
	const stduint tray_w = spk_tray_w + kClockWidth;
	stduint tray_x = width > (tray_w + 6) ? width - tray_w - 4 : 0;
	stduint tray_y = button_y;
	stduint clock_x = tray_x + spk_tray_w;

	speaker_area = show_speaker ? Rectangle(Point(tray_x, tray_y), Size2(spk_tray_w, button_h)) : Rectangle();
	clock_area = Rectangle(Point(clock_x, tray_y), Size2(kClockWidth, button_h));

	// Sunken tray box background & borders
	FillRect(buffer, width, width, height, tray_x, tray_y, tray_w, button_h, RColor(0xFFC0C0C0));
	FillRect(buffer, width, width, height, tray_x, tray_y, tray_w, 1, RColor(0xFF808080));
	FillRect(buffer, width, width, height, tray_x, tray_y, 1, button_h, RColor(0xFF808080));
	FillRect(buffer, width, width, height, tray_x, tray_y + button_h - 1, tray_w, 1, RColor(0xFFFFFFFF));
	FillRect(buffer, width, width, height, tray_x + tray_w - 1, tray_y, 1, button_h, RColor(0xFFFFFFFF));

	// Draw 16x16 color speaker icon inside tray only when Task_Audio_Serv exists
	if (show_speaker) {
		stduint spk_x = tray_x + (spk_tray_w > 16 ? (spk_tray_w - 16) / 2 : 0);
		stduint spk_y = tray_y + (button_h > 16 ? (button_h - 16) / 2 : 0);
		DrawSpeakerTrayIcon(buffer, width, width, height, spk_x, spk_y);
	}

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
	stdsint text_y = tray_y + (button_h > kTextHeight ? (button_h - kTextHeight) / 2 : 2);
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
	stduint right_bound = tray_x > 8 ? tray_x - 8 : cur_x;
	stduint avail_w = right_bound > cur_x ? right_bound - cur_x : 0;
	task_strip_area = Rectangle(Point(cur_x, button_y), Size2(avail_w, button_h));

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
		BlitButton(buffer, width, *taskbar_buttons[i]);
		cur_x += item_w;
	}
}

static void FlushRibbonAll(stdsint form_id) {
	sys_update_form(form_id, nullptr);
	DrawButtonTextIfNonAscii(form_id, start_btn);
	for (stduint i = 0; i < cached_win_count; i++) {
		if (taskbar_buttons[i]) {
			DrawButtonTextIfNonAscii(form_id, *taskbar_buttons[i]);
		}
	}
}

static void FlushStartButton(stdsint form_id) {
	sys_update_form(form_id, &start_btn.sheet_area);
	DrawButtonTextIfNonAscii(form_id, start_btn);
}

static void FlushTaskStrip(stdsint form_id) {
	if (!task_strip_area.width || !task_strip_area.height) return;
	sys_update_form(form_id, &task_strip_area);
	for (stduint i = 0; i < cached_win_count; i++) {
		if (taskbar_buttons[i]) {
			DrawButtonTextIfNonAscii(form_id, *taskbar_buttons[i]);
		}
	}
}

static void FlushSpeakerTray(stdsint form_id) {
	if (speaker_area.width && speaker_area.height) {
		sys_update_form(form_id, &speaker_area);
	}
}

static void FlushClockArea(stdsint form_id) {
	if (clock_area.width && clock_area.height) {
		sys_update_form(form_id, &clock_area);
	}
}

static void DrainFormMessages(stdsint form_id) {
	SheetMessage flush_msg;
	while (sys_fetch_msg(form_id, false, &flush_msg) == 1) {
	}
}

static Size2 GetScreenSize()
{
	Size2 screen = {};
	if (sys_get_screen_size(&screen) != 0 || screen.x < 80 || screen.y < 60) {
		screen = Size2(640, 480);
	}
	return screen;
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

	cached_win_count = 0;
	RefreshCachedWindowList();

	DrawRibbon(buffer, client_w, client_h);
	FlushRibbonAll(form_id);
	sys_set_timer(form_id, 200);

	*out_buffer = buffer;
	return form_id;
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	Size2 screen = GetScreenSize();
	TryLoadWallpaper(screen);
	TryLoadConfigLango();
	InitVolumeState();

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

			bool win_changed = RefreshCachedWindowList();
			if ((time_changed || win_changed) && buffer) {
				DrawRibbon(buffer, screen.x, kRibbonClientHeight);
				if (win_changed) FlushTaskStrip(form_id);
				if (time_changed) FlushClockArea(form_id);
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
				FlushStartButton(form_id);

				if (clicked) {
					RunStartMenuPopup(screen);
					DrainFormMessages(form_id);
					start_btn.pressed = false;
					DrawRibbon(buffer, screen.x, kRibbonClientHeight);
					FlushStartButton(form_id);
					continue;
				}
			}

			// Handle speaker tray icon click to open volume popup
			if (HasAudioService()) {
				bool spk_in = speaker_area.ifContain(rel_p);
				bool spk_down = (smsg.event == SheetEvent::onClick) && ((para1 & 0x10) != 0) && spk_in;
				bool spk_clicked = (speaker_was_pressed && smsg.event == SheetEvent::onClick && ((para1 & 0x10) == 0) && spk_in);
				if (smsg.event == SheetEvent::onClick) {
					speaker_was_pressed = spk_down;
				} else if (smsg.event == SheetEvent::onLeave && para1 == 1) {
					speaker_was_pressed = false;
				}

				if (spk_clicked && buffer) {
					bool vol_changed = RunVolumePopup(screen, speaker_area.x + speaker_area.width / 2);
					DrainFormMessages(form_id);
					if (vol_changed) {
						DrawRibbon(buffer, screen.x, kRibbonClientHeight);
						FlushSpeakerTray(form_id);
					}
					continue;
				}
			}

			// Handle clock area click to open calendar & clock popup
			bool clock_in = clock_area.ifContain(rel_p);
			bool clock_down = (smsg.event == SheetEvent::onClick) && ((para1 & 0x10) != 0) && clock_in;
			bool clock_clicked = (clock_was_pressed && smsg.event == SheetEvent::onClick && ((para1 & 0x10) == 0) && clock_in);
			if (smsg.event == SheetEvent::onClick) {
				clock_was_pressed = clock_down;
			} else if (smsg.event == SheetEvent::onLeave && para1 == 1) {
				clock_was_pressed = false;
			}

			if (clock_clicked && buffer) {
				RunCalendarPopup(screen);
				DrainFormMessages(form_id);
				continue;
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
						sys_minimize_form(cached_win_list[i].form_id, cached_win_list[i].pid);
					} else {
						sys_restore_form(cached_win_list[i].form_id, cached_win_list[i].pid);
					}
					RefreshCachedWindowList();
					if (buffer) {
						DrawRibbon(buffer, screen.x, kRibbonClientHeight);
						FlushTaskStrip(form_id);
					}
					break;
				}
			}
		}
	}

	if (buffer) free(buffer);
	return 0;
}
