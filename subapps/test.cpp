//#include <c/stdinc.h>
#include "../accmlib/inc/aaaaa.h"
#include "c/consio.h"

using namespace uni;

// mnt33 fat16
// mnt34 fat32

int main(int argc, char** argv)
{
	unsigned id = systest('T', 'E', 'S');// TEST
	outsfmt("C(%d)\n\r", id);// OUTC

	#if 1

	#if __BITS__ == 32
	rostr filename = "/mnt33/owo.txt";
	#else
	rostr filename = "/md0/owo.txt";
	#endif

	int fd = sysopen(filename);
	if (fd < 0) {
		fd = sys_createfil(filename);
	}

	if (fd >= 0) {
		outsfmt("Open success! FD=%d\n\r", fd);
		const char* msg = "Hello from Mecocoa VFS!\n";
		syswrite(fd, (void*)msg, 24);
		sysclose(fd);

		fd = sysopen(filename);
		if (fd >= 0) {
			char buf[32] = {0};
			sysread(fd, buf, 24);
			outsfmt("Read result: %s\n\r", buf);
			sysclose(fd);
		}
	} else {
		outsfmt("Open failed with code %d\n\r", fd);
	}

	sys_removefil(filename);
	#endif

	#if 1
	Rectangle rect{ Point(100, 80), Size2(320, 240) };
	stduint form_id = sys_create_form(-_IMM0, &rect);
	if (form_id >= 0) {
		sys_draw_default_string(form_id, Point2(50, 50), "Ciallo~", Color::Maroon);
		sys_draw_point(form_id, Point2(100, 100), Color::Red);
		sys_draw_line(form_id, Point2(80, 100), Point2(40, 30), Color::Blue);
		Rectangle rect0{ Point(16, 16), Size2(10, 40), Color::Green };
		sys_draw_rectangle(form_id, &rect0);
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
		case SheetEvent::onClick:
			ploginfo("msg: _click %x(%d, %d)", smsg.args[2], smsg.args[0], smsg.args[1]);
			break;
		case SheetEvent::onKeybd:
			key_event = (keyboard_event_t*)smsg.args;
			// exit when Alt+F4
			if (key_event->keycode == _UKEY_F4 && (key_event->mod.l_alt || key_event->mod.r_alt)) {
				exit(0);
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

