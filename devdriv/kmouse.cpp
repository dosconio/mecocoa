// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: Mouse - PS2
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../include/mecocoa.hpp"

_ESYM_C void R_MOU_INIT();

#if _MCCA == 0x8632
#if 1
__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_MOU{
	.init = R_MOU_INIT,
	.name = "MOU-PS2",
};
#endif

void R_MOU_INIT() {
	IC[IRQ_PS2_Mouse].setRange(mglb(Handint_MOU_Entry), SegCo32);
	Mouse_Init();
}

extern uni::Queue<SysMessage> message_queue_conv;
static bool fa_mouse = false;
static byte mouse_buf[4] = { 0 };
QueueLimited* queue_mouse;

static void process_mouse(byte ch) {
	mouse_buf[mouse_buf[3]++] = ch;
	mouse_buf[3] %= 3;
	MouseMessage& mm = *(MouseMessage*)mouse_buf;
	if (!mouse_buf[3]) {
		SysMessage smsg{
			.type = SysMessage::RUPT_MOUSE,
		};
		mm.Y = -mm.Y;
		smsg.args.mou_event = mm,
		message_queue_conv.Enqueue(smsg);
	}
	else if (mouse_buf[3] == 1) {
		if (!mm.HIGH) mouse_buf[3] = 0;
	}
}
void Handint_MOU() {
	byte state = innpb(PORT_KEYBOARD_CMD);
	if (state & 0x20); else return;//{} check AUX, give KBD
	byte ch = innpb(PORT_KEYBOARD_DAT);
	// if (ch != (byte)0xFA)// 0xFA is ready signal
	if (!fa_mouse && ch == (byte)0xFA) {
		fa_mouse = true;
		return;
	}
	process_mouse(ch);// asserv(queue_mouse)->OutChar(ch);
	while ((innpb(PORT_KEYBOARD_CMD) & 0x21) == 0x21)// 0x01 for OBF, 0x20 for AUX
	{
		process_mouse(innpb(PORT_KEYBOARD_DAT));
	}
}


#endif
