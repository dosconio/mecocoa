#include "ribbon.hpp"
#include <stdlib.h>

static Button btn_shutdown("Shut down");
static Button btn_reboot("Reboot");
static Button btn_terminal("Terminal");

void ApplyStartMenuLango(Lango lang) {
	switch (lang) {
	case Lango_Japanese:
		btn_shutdown.text = "電源切";
		btn_reboot.text = "再起動";
		btn_terminal.text = "端末";
		break;
	case Lango_Chinese:
		btn_shutdown.text = "关机";
		btn_reboot.text = "重启";
		btn_terminal.text = "终端";
		break;
	default:
		btn_shutdown.text = "Shut down";
		btn_reboot.text = "Reboot";
		btn_terminal.text = "Terminal";
		break;
	}
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

static void DrawStartMenu(Color* buffer, stduint menu_w, stduint menu_h) {
	FillRect(buffer, menu_w, menu_w, menu_h, 0, 0, menu_w, menu_h, RColor(0xFFC6C6C6));
	FillRect(buffer, menu_w, menu_w, menu_h, 0, 0, menu_w, 1, RColor(0xFFFFFFFF));
	FillRect(buffer, menu_w, menu_w, menu_h, 0, 0, 1, menu_h, RColor(0xFFFFFFFF));
	FillRect(buffer, menu_w, menu_w, menu_h, menu_w - 1, 0, 1, menu_h, RColor(0xFF000000));
	FillRect(buffer, menu_w, menu_w, menu_h, 0, menu_h - 1, menu_w, 1, RColor(0xFF000000));

	btn_shutdown.sheet_area = Rectangle(Point(5, 5), Size2(190, 30));
	BlitButton(buffer, menu_w, btn_shutdown);

	btn_reboot.sheet_area = Rectangle(Point(5, 40), Size2(190, 30));
	BlitButton(buffer, menu_w, btn_reboot);

	btn_terminal.sheet_area = Rectangle(Point(5, 75), Size2(190, 30));
	BlitButton(buffer, menu_w, btn_terminal);
}

static void FlushStartMenu(stdsint sm_id) {
	sys_update_form(sm_id, nullptr);
	DrawButtonTextIfNonAscii(sm_id, btn_shutdown);
	DrawButtonTextIfNonAscii(sm_id, btn_reboot);
	DrawButtonTextIfNonAscii(sm_id, btn_terminal);
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

void RunStartMenuPopup(Size2 screen) {
	stduint menu_w = 200;
	stduint menu_h = 300;
	Color* sm_buf = nullptr;
	stdsint sm_id = CreateStartMenuForm(screen, &sm_buf, menu_w, menu_h);
	if (sm_id < 0) return;

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

	btn_shutdown.pressed = false;
	btn_reboot.pressed = false;
	btn_terminal.pressed = false;
}
