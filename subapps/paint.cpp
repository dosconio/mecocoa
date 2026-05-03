// d:\her\mecocoa\subapps\paint.cpp
// ASCII C++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: Clean Paint Application
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../accmlib/inc/aaaaa.h"
#include "c/consio.h"

using namespace uni;

int main(int argc, char** argv)
{
	// 1. Create Window
	Rectangle rect{ Point(150, 100), Size2(480, 300) };
	stduint form_id = sys_create_form(-_IMM0, &rect);
	if (form_id < 0) {
		outsfmt("Failed to create window\n\r");
		return -1;
	}

	bool is_drawing = false;
	Point last_p = {0, 0};
	Color pen_color = Color::Red;

	SheetMessage smsg;
	// Main message loop with clear onClick/onMoved separation
	while (sys_fetch_msg(form_id, true, &smsg)) {
		switch (smsg.event) {
		case SheetEvent::onClick:
		{
			// args[2] bit 4: Current state of Left Button (1=Down, 0=Up)
			bool left_down = (smsg.args[2] & 0x10);
			if (left_down) {
				// START DRAWING: Button Pressed
				is_drawing = true;
				last_p = { smsg.args[0], smsg.args[1] };
				sys_draw_point(form_id, last_p, pen_color);
			}
			else {
			 // STOP DRAWING: Button Released
				is_drawing = false;

				// Close Button check (Component ID 1)
				if (smsg.args[3] == 1) {
					sys_close_form(form_id);
					return 0;
				}
			}
			break;
		}

		case SheetEvent::onMoved:
		{
			// Only draw if the button is currently held down
			if (is_drawing) {
				Point curr_p = { smsg.args[0], smsg.args[1] };
				
				// Ensure movement occurred to avoid redundant draws
				if (curr_p.x != last_p.x || curr_p.y != last_p.y) {
					// Draw line segment for continuity
					Size2 delta = { curr_p.x - last_p.x, curr_p.y - last_p.y };
					// ploginfo(">>> %d %d %d %d", last_p.x, last_p.y, delta.x, delta.y);
					sys_draw_line(form_id, last_p, delta, pen_color);

					// Update tracking point
					last_p = curr_p;
				}
			}
			break;
		}

		default:
			break;
		}
	}

	return 0;
}
