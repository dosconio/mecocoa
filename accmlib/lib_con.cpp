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

stdsint sys_close_form(stduint form_id) {
	stduint buf[1] = { form_id };
	CommMsg msg;
	msg.data.address = _IMM(buf);
	msg.data.length = sizeof(buf);
	msg.type = _IMM(ConsoleMsg::FDEL);
	syscomm(1, Task_Console, &msg);
	syscomm(0, Task_Console, &msg);
	return buf[0];
}

stdsint sys_fetch_msg(stduint form_id, stduint if_blocked, SheetMessage* u_msg) {
	FMT_ConsoleMsg_FMSG fmsg;
	stdsint ret = -1;
	fmsg.pform_id = form_id;
	fmsg.if_blocked = if_blocked;
	fmsg.message = u_msg;
	CommMsg msg;
	msg.data.address = _IMM(&fmsg);
	msg.data.length = sizeof(fmsg);
	msg.type = _IMM(ConsoleMsg::FMSG);
	syscomm(1, Task_Console, &msg);
	msg.data.address = _IMM(&ret);
	msg.data.length = sizeof(ret);
	syscomm(0, Task_Console, &msg);
	return ret;
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

stdsint sys_draw_point(stduint form_id, Point po, Color co) {
	FMT_ConsoleMsg_FDRW fdraw;
	FMT_ConsoleMsg_FDRW::ShapeInfo::ColorPoint cp = { po, co };
	stdsint ret = -1;
	fdraw.pform_id = form_id;
	fdraw.shape_type = FMT_ConsoleMsg_FDRW::Shape::Point;
	fdraw.usr_shape_info.cpoint = &cp;
	CommMsg msg;
	msg.data.address = _IMM(&fdraw);
	msg.data.length = sizeof(fdraw);
	msg.type = _IMM(ConsoleMsg::FDRW);
	syscomm(1, Task_Console, &msg);
	msg.data.address = _IMM(&ret);
	msg.data.length = sizeof(ret);
	syscomm(0, Task_Console, &msg);
	return ret;// should be 0
}

stdsint sys_draw_line(stduint form_id, Point disp, Size2 size, Color co) {
	FMT_ConsoleMsg_FDRW fdraw;
	FMT_ConsoleMsg_FDRW::ShapeInfo::ColorLine cl = { disp, size, co };
	stdsint ret = -1;
	fdraw.pform_id = form_id;
	fdraw.shape_type = FMT_ConsoleMsg_FDRW::Shape::Line;
	fdraw.usr_shape_info.cline = &cl;
	CommMsg msg;
	msg.data.address = _IMM(&fdraw);
	msg.data.length = sizeof(fdraw);
	msg.type = _IMM(ConsoleMsg::FDRW);
	syscomm(1, Task_Console, &msg);
	msg.data.address = _IMM(&ret);
	msg.data.length = sizeof(ret);
	syscomm(0, Task_Console, &msg);
	return ret;// should be 0
}

stdsint sys_draw_rectangle(stduint form_id, const Rectangle* rect) {
	FMT_ConsoleMsg_FDRW fdraw;
	stdsint ret = -1;
	fdraw.pform_id = form_id;
	fdraw.shape_type = FMT_ConsoleMsg_FDRW::Shape::Rect;
	fdraw.usr_shape_info.crect = (Rectangle*)rect;
	CommMsg msg;
	msg.data.address = _IMM(&fdraw);
	msg.data.length = sizeof(fdraw);
	msg.type = _IMM(ConsoleMsg::FDRW);
	syscomm(1, Task_Console, &msg);
	msg.data.address = _IMM(&ret);
	msg.data.length = sizeof(ret);
	syscomm(0, Task_Console, &msg);
	return ret;// should be 0
}
