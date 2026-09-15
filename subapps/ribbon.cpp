#include "aaaaa.h"
#include "c/consio.h"
#include "cpp/Witch/Control/Button.hpp"
#include "cpp/Witch/Control/Control-Label.hpp"
#include <time.h>
#include <stdio.h>
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
	static const char* kWallpaperPaths[] = {
		"/mnt/ide2.0/demo/wallpp.png",
		"/mnt/ahci1.0/demo/wallpp.png"
	};
	FILE* fp = nullptr;
	for (int retry = 0; retry < 3; ++retry) {
		for0a(i, kWallpaperPaths) {
			fp = fopen(kWallpaperPaths[i], "rb");
			if (fp) break;
		}
		if (fp) break;
		sysrest(1, 0); // Wait 1 second before retry
	}

	if (!fp) return;

	fseek(fp, 0, SEEK_END);
	long fileSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (fileSize <= 0) {
		fclose(fp);
		return;
	}

	FileBlockDevice storage(fp, (stduint)fileSize);
	PNGCodec pngCodec;
	bool matched = false;
	if (pngCodec.Probe(storage, matched) == ImageResult::OK && matched) {
		ImageBuffer imgBuf;
		ImageBufferClear(imgBuf);
		StdMalloc myMalloc;
		ImageDecodeOptions options;
		ImageDecodeOptionsInit(options);

		ImageResult res = pngCodec.Decode(storage, imgBuf, myMalloc, options);
		if (res == ImageResult::OK && imgBuf.pixels && imgBuf.width > 0 && imgBuf.height > 0) {
			if (target_screen.x > 0 && target_screen.y > 0 &&
				(imgBuf.width != target_screen.x || imgBuf.height != target_screen.y)) {
				Color* scaled = (Color*)malloc(target_screen.x * target_screen.y * sizeof(Color));
				if (scaled) {
					ScaleImage((Color*)imgBuf.pixels, imgBuf.width, imgBuf.height, scaled, target_screen.x, target_screen.y);
					sys_set_wallpaper(scaled, target_screen.x, target_screen.y);
					free(scaled);
				} else {
					sys_set_wallpaper(imgBuf.pixels, imgBuf.width, imgBuf.height);
				}
			} else {
				sys_set_wallpaper(imgBuf.pixels, imgBuf.width, imgBuf.height);
			}
			ImageBufferFree(imgBuf);
		}
	}
	fclose(fp);
}

using namespace uni;
using namespace uni::witch::control;

static Button start_btn("Start");
static Button btn_shutdown("Shut down");
static Button btn_reboot("Reboot");
static Label time_label("");
static char cached_time_str[32] = "";

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

	// Sunken box background & borders
	FillRect(buffer, width, width, height, clock_x, clock_y, kClockWidth, button_h, RColor(0xFFC0C0C0));
	FillRect(buffer, width, width, height, clock_x, clock_y, kClockWidth, 1, RColor(0xFF808080));
	FillRect(buffer, width, width, height, clock_x, clock_y, 1, button_h, RColor(0xFF808080));
	FillRect(buffer, width, width, height, clock_x, clock_y + button_h - 1, kClockWidth, 1, RColor(0xFFFFFFFF));
	FillRect(buffer, width, width, height, clock_x + kClockWidth - 1, clock_y, 1, button_h, RColor(0xFFFFFFFF));

	// Get current time string (e.g. "Jan02:18:08")
	time_t cur_time = time(nullptr);
	struct tm* tm_info = localtime(&cur_time);
	char time_buf[32] = {0};
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
	sys_update_form(form_id, nullptr);

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
	sys_update_form(form_id, nullptr);
	sys_set_timer(form_id, 200);

	*out_buffer = buffer;
	return form_id;
}

int main(int argc, char** argv)
{

	Size2 screen = GetScreenSize();
	TryLoadWallpaper(screen);
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
			time_t now_time = time(nullptr);
			struct tm* tm_now = localtime(&now_time);
			char now_str[32] = {0};
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
				sys_update_form(form_id, nullptr);
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
				sys_update_form(form_id, nullptr);

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

									if (sd_was_pressed != btn_shutdown.pressed || rb_was_pressed != btn_reboot.pressed) {
										DrawStartMenu(sm_buf, menu_w, menu_h);
										sys_update_form(sm_id, nullptr);
									}

									if (sd_was_pressed && !btn_shutdown.pressed && mmsg.event == SheetEvent::onClick && btn_shutdown.sheet_area.ifContain(rel_p)) {
										sysshutdown();
										menu_open = false;
									} else if (rb_was_pressed && !btn_reboot.pressed && mmsg.event == SheetEvent::onClick && btn_reboot.sheet_area.ifContain(rel_p)) {
										sysreboot();
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
						DrawRibbon(buffer, screen.x, kRibbonClientHeight);
						sys_update_form(form_id, nullptr);
					}
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
						sys_update_form(form_id, nullptr);
					}
					break;
				}
			}
		}
	}

	if (buffer) free(buffer);
	return 0;
}
