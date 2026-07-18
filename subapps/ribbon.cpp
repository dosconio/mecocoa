#include "aaaaa.h"
#include "c/consio.h"
#include "cpp/Witch/Control/Button.hpp"

using namespace uni;
using namespace uni::witch::control;

static Button start_btn("Start");
static Button btn_shutdown("Shut down");
static Button btn_reboot("Reboot");

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

// Removed DrawStartLogo as we now use Button with "Start" text

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

	stdsint form_id = sys_create_form(~_IMM0, &rect, GraphicFormStyle_Titleless);
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

	stdsint form_id = sys_create_form(~_IMM0, &rect, GraphicFormStyle_Titleless);
	if (form_id < 0) return form_id;

	stduint client_w = rect.width;
	stduint client_h = rect.height;
	Color* buffer = (Color*)malloc(client_w * client_h * sizeof(Color));
	if (!buffer) {
		sys_close_form(form_id);
		return -1;
	}

	sys_set_form_buffer(form_id, buffer);
	DrawRibbon(buffer, client_w, client_h);
	sys_update_form(form_id, nullptr);
	sys_set_timer(form_id, 10000);

	*out_buffer = buffer;
	return form_id;
}

int main(int argc, char** argv)
{
	#if __BITS__ == 64
	_preprocess();
	#endif

	Size2 screen = GetScreenSize();
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
			if (next_screen.x == screen.x && next_screen.y == screen.y) continue;

			screen = next_screen;
			sys_close_form(form_id);
			if (buffer) free(buffer);
			buffer = nullptr;
			form_id = CreateRibbonForm(screen, &buffer);
			if (form_id < 0) {
				outsfmt("Ribbon: Failed to recreate form.\r\n");
				return -1;
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
		}
	}

	if (buffer) free(buffer);
	return 0;
}
