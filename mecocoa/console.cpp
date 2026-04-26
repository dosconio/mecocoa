// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <cpp/Witch/Form.hpp>
#include <cpp/Witch/Control/Control-Label.hpp>
#include <cpp/Witch/Control/Control-TextBox.hpp>



#if (_MCCA & 0xFF00) == 0x8600

Cursor* Cursor::global_cursor = nullptr;
SheetTrait* Cursor::moving_sheet = nullptr;
bool Cursor::mouse_btnl_dn = false;
bool Cursor::mouse_btnm_dn = false;
bool Cursor::mouse_btnr_dn = false;

#endif


#if 1 // ---- ---- TTY ---- ----

// vtty0: global_ground
// vtty1: form1...
Dchain ttys = { nullptr };// offs->ConT*, type->B/V
static void VTTY_Free(pureptr_t inp) {
	Letvar(nod, Dnode*, inp);
	// skip offs
	auto& block = *(vtty_type_t*)nod->type;
	if (block.innput_queue.slice.address) {
		delete (byte*)block.innput_queue.slice.address;
	}
	if (block.output_queue.slice.address) {
		_TODO
	}
	memf((void*)nod->type);
}
Dnode* VTTY_Append(Console_t* con) {
	if (!con) {
		plogerro("%s: You may lose impl Console_t for con", __FUNCIDEN__);
		return nullptr;
	}
	auto p = vttys.Append(con);
	if (!p) return nullptr;
	const stduint SIZE_INN_BUF = 64;
	auto p_innbuf = new byte[SIZE_INN_BUF];
	p->type = _IMM(new vtty_type_t({ 0, QueueLimited({_IMM(p_innbuf), SIZE_INN_BUF}), QueueLimited({0, 0}) }));
	return p;
}
Dchain vttys = { VTTY_Free };// offs->ConT*, type->vtty_type_t

#endif

Vector<stduint> blocked_vtty_pid;
// total: may need change after Remove
unsigned current_screen_TTY = 0;

#if (_MCCA & 0xFF00) == 0x8600

// consider CLI
BareConsole Bcons[TTY_NUMBER];// TTY 0~3 and their buffer
// consider GUI
byte _BUF_cursor[byteof(Cursor)];
// global
bool ento_gui = false;
bool enable_dubuffer = false;
OstreamTrait* con0_out = 0;
#ifndef _UEFI
GloScreenARGB8888 local_vci;
#else
GloScreenARGB8888 vga_ARGB8888;
GloScreenABGR8888 vga_ABGR8888;
#endif
VideoControlInterface* real_pvci = nullptr;


#ifndef _UEFI
void blink() {
	static bool b = false;
	local_vci.DrawRectangle(Rectangle(Point(0, 0), Size2(8, 16), b ? Color::Black : Color::White));
	b = !b;
}
void blink2() {
	static bool b = false;
	local_vci.DrawRectangle(Rectangle(Point(8, 0), Size2(8, 16), b ? Color::Black : Color::White));
	b = !b;
}
#endif

#endif


//// ---- ---- DYNAMIC CORE ---- ---- ////
#ifdef _ARC_x86 // x86:
static void InitializeBottomBar() {
	// BCON:
	struct element { byte ch; byte attr; };
	Letvar(Ribbon, element*, (_VIDEO_ADDR_BUFFER + 80 * 2 * 24));
	Ribbon[0].ch = '^';
	Ribbon[1].ch = '-';
	Ribbon[2].ch = '+';
	Ribbon[77].ch = '+';
	Ribbon[78].ch = '-';
	Ribbon[79].ch = '^';
}
#endif

#if 1
static bool ifContainBlockedTTY(ProcessBlock* ppb) {
	for0(i, blocked_vtty_pid.Count()) {
		if (Taskman::Locate(blocked_vtty_pid[i])->focus_tty == ppb->focus_tty) {
			return true;
		}
	}
	return false;
}// To OPTIMIZE

struct FMT_ConsoleMsg_FNEW {
	stduint pform_id;// in pforms
	Rectangle* usrp_rect;
};
static stdsint ConsoleMsg_FNEW(const FMT_ConsoleMsg_FNEW* data, ProcessBlock* pb) {
	//{TODO} pform_id
	Rectangle rect; MccaMemCopyP(&rect, NULL, data->usrp_rect, pb, sizeof(rect));
	ploginfo("FNEW: new form (%u,%u)", rect.width, rect.height);
	// Find an empty slot in pforms
	stdsint slot_idx = -1;
	for (stduint i = 0; i < _TEMP 4; i++) {
		if (pb->pforms[i] == nullptr) {
			slot_idx = i;
			break;
		}
	}
	if (slot_idx == -1) return -1;

	// Create a new empty form
	auto pfrm = new ::uni::Witch::Form();
	if (!pfrm) return -1;
	pfrm->Title = "New Form";

	// Allocate and set sheet for the form
	Color* sheet_buffer = (Color*)mem.allocate(rect.getArea() * sizeof(Color));
	if (!sheet_buffer) {
		delete pfrm;
		return -1;
	}
	pfrm->setSheet(global_layman, rect, sheet_buffer);

	// Register in global layer manager
	global_layman.Append(pfrm);
	// Move the new form to the second layer (behind the cursor)
	Nnode* target = &pfrm->refSheetNode();
	if (global_layman.subf && target != global_layman.subf) {
		// Remove from the tail where Append put it
		if (target->left) target->left->next = target->next;
		if (global_layman.subl == target) global_layman.subl = target->left;
		// Insert after the top layer (cursor)
		Nnode* top = global_layman.subf;
		Nnode* sec = top->next;
		target->left = top;
		target->next = sec;
		top->next = target;
		if (sec) sec->left = target;
		else global_layman.subl = target;// target is now the new tail
	}
	global_layman.Update(pfrm, Rectangle(Point(0, 0), rect.getSize()));

	// Store in process block pforms
	pb->pforms[slot_idx] = pfrm;

	return slot_idx;
}

void _Comment(R1) serv_cons_loop()
{
	#ifdef _ARC_x86 // x86:
	InitializeBottomBar();
	#endif

	devfs_register_and_mount();

	volatile stduint sig_type = 0, sig_src, ret;
	volatile stduint to_args[4];

	int ch;

	while (true) {
		for0 (i, blocked_vtty_pid.Count()) if (stduint pid = blocked_vtty_pid[i]) {
			auto ppb = Taskman::Locate(pid);
			if (!ppb) {
				plogerro("%s", __FUNCIDEN__);
				continue;
			}
			if (ppb->focus_tty && -1 != (ch = VTTY_INNQ(ppb->focus_tty)->inn())) {
				stdsint val = ch; // The character is already translated through sysmsg_kbd
				blocked_vtty_pid.Remove(i);
				syssend(pid, &val, byteof(val));
			}
		}

		// Process potential message
		if (syscall(syscall_t::TMSG)) {
			sysrecv(ANYPROC, (void*)to_args, byteof(to_args), (usize*)&sig_type, (usize*)&sig_src);
			auto th = Taskman::LocateThread(sig_src);
			switch (ConsoleMsg(sig_type)) {
			case ConsoleMsg::TEST: break;

			#ifdef _ARC_x86 // x86:
			case ConsoleMsg::READ:
				_TODO
				break;
			case ConsoleMsg::WRIT:
				_TODO
				break;
			#endif

			case ConsoleMsg::INNC:// ReadChar(ASCII): normal \n \r ...
				if (!ifContainBlockedTTY(th->parent_process)) {
					blocked_vtty_pid.Append(th->parent_process->getID());
					// ploginfo("Blocked TTY %u by %u", pb->focus_tty, pb->getID());
				}
				else {
					ret = ~_IMM0;
					syssend(sig_src, (void*)&ret, sizeof(ret), 0);
				}
				break;
			case ConsoleMsg::FNEW:
				ploginfo("creating new form %[x]", to_args[0]);
				ret = ConsoleMsg_FNEW((FMT_ConsoleMsg_FNEW*)to_args, th->parent_process);
				syssend(sig_src, (void*)&ret, sizeof(ret), 0);
				break;





			default:
				plogerro("Unknown syscall type %u", sig_type);
				break;
			}
		}
		syscall(syscall_t::REST);
	}
}
#endif

//// ---- ---- Bottom Impl ---- ---- ////
#ifdef _ARC_x86 // x86:

void uni::BareConsole::doshow(void* _) {}

#endif

//// ---- ---- STATIC CORE ---- ---- ////
#if ((_MCCA & 0xFF00) == 0x8600)
LayerManager2 global_layman;
#if defined(_UEFI) && _MCCA == 0x8664
extern UefiData uefi_data;
#endif

::uni::Witch::Form form2 _TEMP;

void cons_init() {
	con0_out = 0;
	Bcons[0].Reset(bda->screen_columns, 24, _VIDEO_ADDR_BUFFER, 0 * 50); Bcons[0].setShowY(0, 24);
	for1(i, TTY_NUMBER - 1) {
		Bcons[i].Reset(bda->screen_columns, 50, _VIDEO_ADDR_BUFFER, i * 50); Bcons[i].setShowY(0, 25);
	}

	// try first 800xN ARGB8888 Mode
	uint16 vmod_default = nil;
	#if !defined(_UEFI)
	call_ladder(R16FN_LSVM);// list video modes
	for (auto vie = (VideoInfoEntry*)0x78000; _IMM(vie) < 0x80000; vie++) {
		#if !_GUI_ENABLE
		break;
		#endif
		if (!vie->mode) break;
		// ploginfo("mode %[16H], %ux%u, ARGB:%[16H]", vie->mode, vie->width, vie->height, vie->bitmode);
		if (vie->bitmode == 0x8888 && vie->width == 800) {
			vmod_default = vie->mode;
			break;
		}
	}
	#else
	vmod_default = 0xFFFF;
	#endif
	ento_gui = vmod_default;
	if (!vmod_default) {
		con0_out = &Bcons[0];
		Bcons[0].Scroll(24);
		for0a(i, Bcons) ttys.Append(dynamic_cast<Console_t*>(&Bcons[i]));
		for0a(i, Bcons) VTTY_Append((&Bcons[i]));
		plogwarn("There is no default 800xN-8888 Video Mode");
		return;
	}

	// config layman
	#if !defined(_UEFI)
	auto addr = (ModeInfoBlock*)_IMM(call_ladder(R16FN_VMOD, vmod_default));
	Rectangle screen0_win{ Point(0,0), Size2(addr->XResolution, addr->YResolution), Color::Black };
	global_layman.Reset(&local_vci, screen0_win);
	global_layman.video_mode = vmod_default;
	global_layman.video_memory = addr->PhysBasePtr;
	global_layman.pixel_fmt = PixelFormat::ARGB8888;
	#else
	Rectangle screen0_win{ Point(0,0), Size2(uefi_data.frame_buffer_config.horizontal_resolution, uefi_data.frame_buffer_config.vertical_resolution), Color::Black };
	VideoControlInterface* screen;
	switch (uefi_data.frame_buffer_config.pixel_format) {
	case PixelFormat::ARGB8888: screen = &vga_ARGB8888; break;
	case PixelFormat::ABGR8888: screen = &vga_ABGR8888; break;
	default:
		loop HALT();
	}
	global_layman.Reset(screen, screen0_win);
	global_layman.video_mode = vmod_default;
	global_layman.video_memory = (stduint)uefi_data.frame_buffer_config.frame_buffer;
	global_layman.pixel_fmt = uefi_data.frame_buffer_config.pixel_format;
	#endif
	const stduint vcon0_size = global_layman.window.getArea() * sizeof(Color);
	//
	#if _MCCA == 0x8632
	kernel_paging.Map(
		global_layman.video_memory,
		global_layman.video_memory,
		vcon0_size,
		PAGESIZE_4KB, PGPROP_present | PGPROP_writable
	);// VGA
	#endif

	// main screen
	auto vcon0 = new VideoConsole2(&global_layman.getVCI(), screen0_win, Color::Black, Color::White);
	// auto vcon0_buf = (Color*)mem.allocate(vcon0_size);
	vcon0->setBuffers(nullptr,
		new BufferChar[vcon0->getCols() * vcon0->getRows()],
		new Color[vcon0->getLineBufferSize()]
	);
	vcon0->InitializeSheet(global_layman, screen0_win.getVertex(), screen0_win.getSize());
	VTTY_Append(vcon0);

	// cursor
	Cursor::global_cursor = new (_BUF_cursor)Cursor{ &global_layman.getVCI() };
	const Point cursor_pos = { 300,200 };
	Cursor::global_cursor->setSheet(global_layman, cursor_pos);

	if (_TEMP 1) {
		Rectangle rect{ Point(250, 160), Size2(480, 320) };
		auto pcon = new VideoConsole2(NULL,
			Rectangle(Point(2, 2), Size2(470, 290)),
				Color::Black, 0xFFFCEAF1
		);
		// auto vcon_buf = (Color*)mem.allocate(pcon->window.getArea() * sizeof(Color));// Vcon Gen1
		auto text_buf = (BufferChar*)mem.allocate(pcon->getCols() * pcon->getRows() * sizeof(BufferChar));
		auto line_buf = (Color*)mem.allocate(pcon->getLineBufferSize() * sizeof(Color));
		pcon->setBuffers(nullptr, text_buf, line_buf);
		pcon->InitializeSheet(global_layman, pcon->window.getVertex(), pcon->window.getSize());
		// pcon->setModeBuffer(vcon_buf); Vcon Gen1
		pcon->Clear();

		form2.Title = "Console-Gen2";
		form2.AppendControl(pcon);
		form2.setSheet(global_layman, rect, (Color*)mem.allocate(rect.getArea() * sizeof(Color)));
		form2.setFocus(pcon);
		global_layman.Append(&form2);
		pcon->Start();

		VTTY_Append((pcon));
	}// should follow 'init'

	global_layman.Append(vcon0);

	#if _GUI_DOUBLE_BUFFER
	enable_2buffer();
	#endif

	vcon0->Clear();
	con0_out = vcon0;
	current_screen_TTY = _TEMP 1;

	// default tty are all bcon
}
/* 4 BCON TTY

void uni::BareConsole::doshow(void *_) {
	unsigned id = IndexTTY(this);
	if (id == current_screen_TTY) return;
	if (id > 3) {
		plogerro("TTY Id %u", id);
		return;
	}
	bcons[current_screen_TTY]->last_curposi = curget();
	bcons[id]->setStartLine(topline + crtline);
	current_screen_TTY = id;
	curset(bcons[id]->last_curposi);
	//
	for0(i, 4) bcons[i]->auto_incbegaddr = false;
	bcons[id]->auto_incbegaddr = true;
}

*/

#endif


