#include "inc/aaaaa.h"
#include "c/ustring.h"
#include "c/consio.h"

stdsint sys_create_form(stduint form_id, const Rectangle* rect)
{
	stduint buf[4] = { form_id, _IMM(rect) };
	CommMsg msg;
	msg.data.address = _IMM(buf);
	msg.data.length = sizeof(buf);
	msg.type = _IMM(ConsoleMsg::FNEW);
	syscomm(1, Task_Console, &msg);
	syscomm(0, Task_Console, &msg);
	return buf[0];
}

stdsint sys_draw_default_string(stduint form_id, Point vertex, rostr string, Color color)
{
	stduint buf[4] = { form_id, _IMM(&vertex), _IMM(string), _IMM(color) };
	CommMsg msg;
	msg.data.address = _IMM(buf);
	msg.data.length = sizeof(buf);
	msg.type = _IMM(ConsoleMsg::FCHR);
	syscomm(1, Task_Console, &msg);
	syscomm(0, Task_Console, &msg);
	return buf[0];// should be 0
}



