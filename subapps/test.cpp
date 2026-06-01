//#include <c/stdinc.h>
#include "aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <c/ISO_IEC_STD/signal.h>
#include <sys/mman.h>

using namespace uni;

// static Color test_buffer[318 * 221];
static Color colors[] = { Color::Red, Color::Green, Color::Blue, Color::Yellow, (0xFF00FFFF), (0xFFFF00FF) };
static int color_idx = 0;

int main(int argc, char** argv)
{
	#if __BITS__ == 64
	_preprocess();
	#endif
	// unsigned id = getpid();// TEST
	// outsfmt("C(%d)\n\r", id);// OUTC

	#if 1 // TEST FORM
	Color* test_buffer = (Color*)malloc(318 * 221 * sizeof(Color));// static Color test_buffer[318 * 221];
	Rectangle rect{ Point(100, 80), Size2(320, 240) };
	auto form_id = sys_create_form(-_IMM0, &rect);
	if (form_id >= 0) {
		sys_set_form_buffer(form_id, test_buffer);
		for (int i = 0; i < 318 * 221; i++) {
			test_buffer[i].r = 0xFF;
			test_buffer[i].g = 0xFF;
			test_buffer[i].b = 0xFF;
			test_buffer[i].a = 0xFF;
		}
		sys_update_form(form_id, nullptr);

		sys_draw_default_string(form_id, Point2(50, 50), "Ciallo~", Color::Maroon);
		sys_draw_point(form_id, Point2(100, 100), Color::Red);
		sys_draw_line(form_id, Point2(80, 100), Point2(40, 30), Color::Blue);
		Rectangle rect0{ Point(16, 16), Size2(10, 40), Color::Green };
		sys_draw_rectangle(form_id, &rect0);
		sys_set_timer(form_id, 2000); // 2s timer
	}
	else {
		outsfmt("Create form failed with code %d\n\r", form_id);
	}
	if (0) for0(i, 3) {
		outsfmt("TEST(%d)\n\r", syssecond());// TIME
		sysrest(0 _Comment(s), 1);// REST
	}// delay 3 s
	SheetMessage smsg;
	keyboard_event_t* key_event;
	while (sys_fetch_msg(form_id, true, &smsg)) {
		switch (smsg.event) {
		case SheetEvent::onTimer:
		{
			Color c = colors[color_idx];
			for (int i = 0; i < 318 * 221; i++) test_buffer[i] = c;
			color_idx = (color_idx + 1) % 6;
			sys_update_form(form_id, nullptr);
			// outsfmt("msg: onTimer at %u\n\r", smsg.args[3]);
			break;
		}
		case SheetEvent::onMoved:
			// ploginfo("msg: _moved at (%d, %d)", smsg.args[0], smsg.args[1]);
			break;
		case SheetEvent::onClick:
			if (smsg.args[3] == 1 && !(smsg.args[2] & 0x10)) {
				// Left button release on Close Button
				ploginfo("msg: _close requested via button");
				sys_close_form(form_id);
				return 0;
			} else {
				ploginfo("msg: _click at (%d, %d), comp=%x", smsg.args[0], smsg.args[1], smsg.args[2]);
			}
			break;
		case SheetEvent::onKeybd:
			key_event = (keyboard_event_t*)smsg.args;
			// exit when Alt+F4
			if (key_event->keycode == _UKEY_F4 && (key_event->mod.l_alt || key_event->mod.r_alt)) {
				_exit(0);
			}
			ploginfo("msg: kboard code=%[32H]", *key_event);
			break;
		default:
			ploginfo("msg: typ%u", smsg.event);
			break;
		}
	}
	if (0) {
		if (form_id >= 0) sys_close_form(form_id);
		form_id = -1;
	};



	#endif



	// loop sysrest(0, 0);
	return 0;
}
