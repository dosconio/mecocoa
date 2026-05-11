// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <cpp/Witch/Control/Control-Label.hpp>
#include <cpp/Witch/Control/Control-TextBox.hpp>

#if _GUI_ENABLE
extern Spinlock gui_lock;
#endif
extern Spinlock scheduler_lock;
#include "../include/filesys.hpp"



#if 1 // ---- ---- TTY ---- ----


// vtty0: global_ground
// vtty1: form1...
Dchain ttys = { nullptr };// offs->ConT*, type->B/V
static void VTTY_Free(pureptr_t inp) {
	Letvar(nod, Dnode*, inp);
	// skip offs
	auto pblock = (vtty_type_t*)nod->type;
	DevFs::free_tty_id(pblock->id);
	if (pblock->innput_queue.slice.address) {
		free((byte*)pblock->innput_queue.slice.address);
	}
	if (pblock->output_queue.slice.address) {
		_TODO
	}
	free(pblock);
}
Dnode* VTTY_Append(Console_t* con) {
	// ploginfo("VTTY_Append called for console at %p", con);
	if (!con) {
		plogerro("%s: You may lose impl Console_t for con", __FUNCIDEN__);
		return nullptr;
	}

	#if _GUI_ENABLE
	SpinlockLocal guard(&gui_lock);
	#endif
	auto p = vttys.Append(con);
	if (!p) return nullptr;
	const stduint SIZE_INN_BUF = 64;
	auto p_innbuf = new byte[SIZE_INN_BUF];
	auto pblock = new vtty_type_t();
	pblock->innput_queue = QueueLimited({_IMM(p_innbuf), SIZE_INN_BUF});
	pblock->output_queue = QueueLimited({0, 0});
	pblock->id = DevFs::allocate_tty_id();
	p->type = _IMM(pblock);

	if (Taskman::current_thread[Taskman::getID()]) {
		pblock->master_pid = Taskman::CurrentPID();
	}
	else {
		pblock->master_pid = 0;
	}

	return p;
}
Dchain vttys = { VTTY_Free };// offs->ConT*, type->vtty_type_t

#endif
Vector<stduint> blocked_vtty_pid;


struct BlockedFormMsg {
	stduint sig_src; // thread id
	stduint pform_id; // form index in pforms
	SheetMessage* usr_msg_ptr; // user space buffer pointer

	bool operator==(const BlockedFormMsg& other) const {
		return sig_src == other.sig_src && pform_id == other.pform_id && usr_msg_ptr == other.usr_msg_ptr;
	}
};
Vector<BlockedFormMsg> blocked_form_msgs;


//// ---- ---- Bottom Impl ---- ---- ////
#ifdef _ARC_x86 // x86:

extern BareConsole Bcons[TTY_NUMBER];
void uni::BareConsole::doshow(void* _) {
	unsigned id = _IMM(_);
	if (id == Consman::current_screen_TTY) return;
	if (id >= TTY_NUMBER) {
		plogerro("TTY Id %u", id);
		return;
	}
	Bcons[Consman::current_screen_TTY].last_curposi = curget();
	Bcons[id].setStartLine(topline + crtline);
	Consman::current_screen_TTY = id;
	curset(Bcons[id].last_curposi);
	//
	for0(i, 4) Bcons[i].auto_incbegaddr = false;
	Bcons[id].auto_incbegaddr = true;
}

#endif

#if _GUI_ENABLE
_RET_CreateVconsole Consman::CreateVconsole(const Rectangle& rect, rostr title) {
	_RET_CreateVconsole ret;
	auto pcon = new VideoConsole2(NULL,
		Rectangle(Point(2, 2), Size2(rect.width - 10, rect.height - 30)),
		Color::Black, 0xFFFCEAF1
	);
	ret.pcon = pcon;
	// plogwarn(">pcon: %x", pcon);
	// auto vcon_buf = (Color*).allocate(pcon->window.getArea() * sizeof(Color));// Vcon Gen1
	auto text_buf = new BufferChar[pcon->getCols() * pcon->getRows()];
	// plogwarn(">text_buf: %x", text_buf);
	auto line_buf = new Color[pcon->getLineBufferSize()];
	// plogwarn(">line_buf: %x", line_buf);
	if (!text_buf || !line_buf) {
		free(pcon);
		plogerro("?");
		return ret;
	}
	pcon->setBuffers(nullptr, text_buf, line_buf);

	auto pform = new ::uni::Witch::Form();
	// plogwarn(">pform: %x", pform);
	if (!pform) {
		free(pcon);
		plogerro("?");
		return ret;
	}
	ret.pform = pform;
	pform->Title = title;
	pcon->InitializeSheet(*pform, pcon->window.getVertex(), pcon->window.getSize());
	// pcon->setModeBuffer(vcon_buf); Vcon Gen1
	pcon->Clear();
	pform->AppendControl(pcon);
	pform->setSheet(global_layman, rect, new Color[rect.getArea()]);
	pform->setFocus(pcon);

	{
		// [Lock]: Protect global layman chain and focus switch
		#if _GUI_ENABLE
		SpinlockLocal guard(&gui_lock);
		#endif
		global_layman.Append(pform);
		Consman::SwitchForm(pform);
	}

	pcon->Start();//{} 2buffer only now

	VTTY_Append(pcon);
	ret.tty_no = vttys.Locate((pureptr_t)pcon, false);
	// ploginfo("CreateVconsole: tty_no=%u", ret.tty_no);
	// pcon->OutFormat("Hello");
	return ret;
}

void Consman::RemoveVconsole(Dnode* nod) {
	if (!nod) return;
	auto pcon = (uni::VideoConsole2*)nod->offs;
	if (pcon) {
		// Identify and cleanup the parent Form
		auto pclient = pcon->refSheetParent();
		auto pfrm = (pclient) ? (uni::Witch::Form*)pclient->refSheetParent() : nullptr;
		if (pclient) {
			// Focus cleanup: reset global pointers if they point to any component of this window
			if (Consman::last_click_sheet == pclient || 
				Consman::last_click_sheet == pcon) {
				Consman::last_click_sheet = nullptr;
			}
			if (Cursor::moving_sheet == pclient || Cursor::moving_sheet == pcon) {
				Cursor::moving_sheet = nullptr;
			}

			Rectangle area = pclient->sheet_area;
			{
				// [Lock]: Caller must hold gui_lock. 
				// In _CleanSingleForm, it is already held.
				pclient->Remove(pcon); // Unlink pcon from parent Form
				global_layman.Remove(pclient); // Ensure it's removed from desktop too
				global_layman.Update(nullptr, area);
			}
		}

		// Stop the console object (this will free text_buf and line_buf using free())
		pcon->Stop();
		free(pcon);
	}

	// Nullify focus_tty references for ALL processes in the system to prevent UAF
	{
		SpinlockLocal guard(&scheduler_lock);
		for (auto pnod = Taskman::chain.Root(); pnod; pnod = pnod->next) {
			auto p = cast<ProcessBlock*>(pnod->offs);
			if (p->focus_tty == nod) p->focus_tty = nullptr;
		}
	}

	// Remove from VTTY list and trigger VTTY_Free
	// [Lock]: Caller must hold gui_lock.
	vttys.Remove(nod);

	// Clean up any blocked PIDs waiting for this TTY
	for (stduint i = 0; i < blocked_vtty_pid.Count(); i++) {
		ProcessBlock* p = Taskman::Locate(blocked_vtty_pid[i]);
		if (!p || p->focus_tty == nod || p->focus_tty == nullptr) {
			blocked_vtty_pid.Remove(i--);
		}
	}
}
void Consman::SwitchForm(SheetTrait* pfrm) {
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
	// [Note]: global_layman.Update is protected by the caller's gui_lock or should be locked if called alone
	global_layman.Update(pfrm, Rectangle(Point(0, 0), pfrm->sheet_area.getSize()));
}
#endif

//// ---- ---- DYNAMIC CORE ---- ---- ////

static bool ifContainBlockedTTY(ProcessBlock* ppb) {
	if (!ppb) return false;
	for0(i, blocked_vtty_pid.Count()) {
		ProcessBlock* p = Taskman::Locate(blocked_vtty_pid[i]);
		if (p && p->focus_tty == ppb->focus_tty) {
			return true;
		}
	}
	return false;
}// To OPTIMIZE

#if _GUI_ENABLE
struct FMT_ConsoleMsg_FNEW {
	stduint pform_id;// in pforms
	Rectangle* usrp_rect;
};
static stdsint ConsoleMsg_FNEW(const FMT_ConsoleMsg_FNEW* data, ProcessBlock* pb) {
	//{TODO} nested pform_id
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
	Color* sheet_buffer = new Color[rect.getArea()];
	if (!sheet_buffer) {
		free(pfrm);
		return -1;
	}
	pfrm->setSheet(global_layman, rect, sheet_buffer);

	{
		// [Lock]: Protect global registration
		#if _GUI_ENABLE
		SpinlockLocal guard(&gui_lock);
		#endif
		global_layman.Append(pfrm);
		Consman::SwitchForm(pfrm);
	}

	// Store in process block pforms
	pb->pforms[slot_idx] = pfrm;

	return slot_idx;
}

static void _CleanSingleForm(ProcessBlock* pb, stduint pform_id) {
	if (pform_id >= _TEMP 4) return;
	SheetTrait* pfrm = pb->pforms[pform_id];
	if (!pfrm) return;

	// Recursive destroy sub-forms belonging to this process that are children of this form
	for (stduint i = 0; i < _TEMP 4; i++) {
		if (pb->pforms[i] && pb->pforms[i]->refSheetParent() == (LayerManager*)pfrm) {
			_CleanSingleForm(pb, i);
		}
	}

	Rectangle area = pfrm->sheet_area;
	{
		// [Lock]: Protect global layman chain and focus switch
		#if _GUI_ENABLE
		SpinlockLocal guard(&gui_lock);
		#endif

		LayerManager* parent = pfrm->refSheetParent();
		if (parent) parent->Remove(pfrm);
		global_layman.Update(nullptr, area);

		// [TTY Leak Fix]: If this form OR any of its children is a TTY (VideoConsole2), remove it from the global TTY chain
		extern uni::Dchain vttys;
		auto UnbindTTY = [&](SheetTrait* target) {
			for (auto nod = vttys.Root(); nod; nod = nod->next) {
				if (nod->offs == (pureptr_t)target) {
					// Specialized cleanup handles global focus_tty sanitization
					Consman::RemoveVconsole(nod);
					return true;
				}
			}
			return false;
			};

		UnbindTTY(pfrm);
		// Also check immediate children (VideoConsole2 is a child of the Form)
		LayerManager* pman = (LayerManager*)pfrm;
		Nnode* nod = pman->subf;
		while (nod) {
			Nnode* next = nod->next; // Save next before potential deletion
			UnbindTTY(cast<SheetTrait*>(nod->offs));
			nod = next;
		}

		// [Ghost Pointer Fix]: Clear global pointers if they target the form OR any of its descendants
		auto IsDescendantOf = [](SheetTrait* target, SheetTrait* root) -> bool {
			SheetTrait* curr = target;
			while (curr) {
				if (curr == root) return true;
				curr = (SheetTrait*)curr->refSheetParent();
			}
			return false;
			};

		if (Consman::last_click_sheet && IsDescendantOf(Consman::last_click_sheet, pfrm)) {
			Consman::last_click_sheet = nullptr;
		}
		if (Cursor::moving_sheet && IsDescendantOf(Cursor::moving_sheet, pfrm)) {
			Cursor::moving_sheet = nullptr;
		}

		global_layman.UnregisterTimer(pfrm);
	}

	// Unblock threads waiting for this specific form
	for (stduint i = 0; i < blocked_form_msgs.Count(); i++) {
		if (blocked_form_msgs[i].pform_id == pform_id) {
			stdsint err_val = -1;
			ThreadBlock* th_target = Taskman::LocateThread(blocked_form_msgs[i].sig_src);
			if (th_target && th_target->parent_process && th_target->parent_process->state == ProcessBlock::State::Active) {
				syssend(blocked_form_msgs[i].sig_src, &err_val, sizeof(err_val));
			}
			blocked_form_msgs.Remove(i--);
		}
	}

	// Safe memory release
	if (pfrm->sheet_buffer) {
		free(pfrm->sheet_buffer);
		pfrm->sheet_buffer = nullptr;
	}

	// [Message Queue Fix]: Remove any pending blocking requests for this form
	for (stduint i = 0; i < blocked_form_msgs.Count(); i++) {
		if (blocked_form_msgs[i].pform_id == pform_id) {
			blocked_form_msgs.Remove(i);
			i--;
		}
	}

	free(pfrm);
	pb->pforms[pform_id] = nullptr;
}

static stdsint ConsoleMsg_FDEL(stduint pform_id, ProcessBlock* pb) {
	if (pform_id == ~_IMM0) {
		for (stduint i = 0; i < _TEMP 4; i++) {
			_CleanSingleForm(pb, i);
		}
		return 0;
	}
	if (pform_id >= _TEMP 4 || !pb->pforms[pform_id]) return -1;
	_CleanSingleForm(pb, pform_id);
	return 0;
}

void Global_CleanProcessForms(ProcessBlock* pb) {
	if (!pb) return;
	for (stduint pform_id = 0; pform_id < _TEMP 4; pform_id++) {
		_CleanSingleForm(pb, pform_id);
	}
}



static stdsint ConsoleMsg_FMSG(const FMT_ConsoleMsg_FMSG* data, ProcessBlock* pb, stduint sig_src) {
	if (data->pform_id >= _TEMP 4) return -1;
	SheetTrait* pfrm = pb->pforms[data->pform_id];
	if (!pfrm) return -1;

	// Assuming pfrm is a Form object with a message queue
	auto pf = static_cast<::uni::Witch::Form*>(pfrm);
	if (pf->msg_queue.Count()) {
		SheetMessage msg;
		pf->msg_queue.Dequeue(msg);
		MccaMemCopyP(data->message, pb, &msg, NULL, sizeof(msg));
		return 1; // Message fetched
	}

	if (data->if_blocked) {
		// Store blocking request to be handled later
		blocked_form_msgs.Append({ sig_src, data->pform_id, data->message });
		return -2; // Signal that reply is deferred
	}

	return 0; // No message available
}

static stdsint ConsoleMsg_FDRW(const FMT_ConsoleMsg_FDRW* data, ProcessBlock* pb) {
	// Target form from process pforms
	if (data->pform_id >= _TEMP 4) return -1;
	SheetTrait* pfrm = pb->pforms[data->pform_id];
	if (!pfrm) return -1;
	// ploginfo("ConsoleMsg_FDRW: draw %d", data->shape_type);
	switch (data->shape_type) {
	case FMT_ConsoleMsg_FDRW::Shape::Point: {
		FMT_ConsoleMsg_FDRW::ShapeInfo::ColorPoint cp;
		if (MccaMemCopyP(&cp, NULL, data->usr_shape_info.cpoint, pb, sizeof(cp)) != sizeof(cp)) return -1;
		if (!pfrm->sheet_buffer) return -1;
		pfrm->sheet_buffer[cp.po.y * pfrm->sheet_area.width + cp.po.x] = cp.co;
		return 0;
	}
	case FMT_ConsoleMsg_FDRW::Shape::Line: {
		FMT_ConsoleMsg_FDRW::ShapeInfo::ColorLine cl;
		if (MccaMemCopyP(&cl, NULL, data->usr_shape_info.cline, pb, sizeof(cl)) != sizeof(cl)) return -1;
		if (!pfrm->sheet_buffer) return -1;

		VideoControlInterfaceMARGB8888 vcim(pfrm->sheet_buffer, pfrm->sheet_area.getSize());
		LayerManager lm(&vcim, Rectangle{ Point(0,0), pfrm->sheet_area.getSize() });

		// Now compatible with signed offsets
		Size2dif off;
		off.x = cl.size.x;
		off.y = cl.size.y;
		lm.DrawLine(cl.disp, off, cl.color);

		// Calculate the bounding box for the line update (always positive dimensions for the dirty rect)
		stdsint x1 = cl.disp.x, y1 = cl.disp.y;
		stdsint x2 = cl.disp.x + cl.size.x, y2 = cl.disp.y + cl.size.y;
		stdsint rx = (x1 < x2 ? x1 : x2) - 1;
		stdsint ry = (y1 < y2 ? y1 : y2) - 1;
		stdsint rw = (x1 < x2 ? x2 - x1 : x1 - x2) + 3;
		stdsint rh = (y1 < y2 ? y2 - y1 : y1 - y2) + 3;

		global_layman.Update(pfrm, Rectangle(Point(rx, ry), Size2(rw, rh)));

		return 0;
	}
	case FMT_ConsoleMsg_FDRW::Shape::Rect: {
		Rectangle rect;
		if (MccaMemCopyP(&rect, NULL, data->usr_shape_info.crect, pb, sizeof(rect)) != sizeof(rect)) return -1;
		if (!pfrm->sheet_buffer) return -1;
		VideoControlInterfaceMARGB8888 vcim(pfrm->sheet_buffer, pfrm->sheet_area.getSize());
		vcim.DrawRectangle(rect);
		global_layman.Update(pfrm, rect);
		return 0;
	}

	default: return -1;
	}
}

struct FMT_ConsoleMsg_FCHR {
	stduint pform_id;// in pforms
	Point* usrp_vertex;
	const char* usrp_str;
	Color color;
};
static stdsint ConsoleMsg_FCHR(const FMT_ConsoleMsg_FCHR* data, ProcessBlock* pb) {
	// Target form from process pforms
	if (data->pform_id >= _TEMP 4) return -1;
	SheetTrait* pfrm = pb->pforms[data->pform_id];
	if (!pfrm) return -1;

	// Copy parameters from user space
	Point vertex;
	if (MccaMemCopyP(&vertex, NULL, data->usrp_vertex, pb, sizeof(vertex)) != sizeof(vertex)) return -1;
	char buf[32];
	stduint len = StrCopyP(buf, kernel_paging, data->usrp_str, pb->paging, sizeof(buf));

	// [Security Fix]: Validate drawing bounds to prevent heap corruption
	stduint str_len = StrLength(buf);
	if ((vertex.x + (stdsint)str_len * 8) > pfrm->sheet_area.width ||
		(vertex.y + 16) > pfrm->sheet_area.height) {
		return -1;
	}

	// Draw using DrawString_16 (8x16 font)
	uni::DrawString_16(*pfrm, vertex, String(buf), data->color);

	// Update the dirty area
	Rectangle dirty_rect(vertex, Size2(StrLength(buf) * 8, 16));
	global_layman.Update(pfrm, dirty_rect);

	return 0;
}

static stdsint ConsoleMsg_FTIM(stduint pform_id, stduint ms, ProcessBlock* pb) {
	if (pform_id >= _TEMP 4) return -1;
	SheetTrait* pfrm = pb->pforms[pform_id];
	if (!pfrm) return -1;

	// Calculate timer period (convert ms to ticks)
	stduint period = ms * SysTickFreq / 1000;
	if (ms && !period) period = 1;

	#if _GUI_ENABLE
	SpinlockLocal guard(&gui_lock);
	#endif
	global_layman.UnregisterTimer(pfrm);
	if (period) {
		global_layman.RegisterTimer(pfrm);
		// RegisterTimer in unisym sets timer_timeout_period to 0, so we must set it AFTER.
		pfrm->timer_timeout_period = period;
		pfrm->timer_timeout_tick = tick + period;
		ploginfo("Timer set for form %u: period %u ticks", pform_id, period);
	}
	else {
		pfrm->timer_timeout_period = 0;
	}

	return 0;
}
#endif

#if _GUI_ENABLE
void _Comment(R1) serv_shell_process() {
	uni::Dnode* tty_target = 0;
	::uni::Witch::Form* pf_ptr = nullptr;
	if (Consman::ento_gui) {
		Rectangle rect{ Point(250, 160), Size2(480, 320) };
		auto [pcon, pf, tty_no] = Consman::CreateVconsole(rect, "Terminal");
		pf_ptr = pf;
		tty_target = vttys.LocateNode(tty_no);
		// ploginfo("CreateVconsole->[%[x],%[x],%u]", pcon, pf, tty_no);
	}

	ProcessBlock* p = nullptr;
	stduint src = 0, type = 0;
	sysrecv(ANYPROC, &p, sizeof(p), &type, &src);
	if (p) {
		// Register the process globally to get a valid PID
		Taskman::Append(p);
		// Lock the process state during complex environment setup
		IC.enAble(false);
		p->state = ProcessBlock::State::Hanging;

		p->focus_tty = tty_target;
		if (p->focus_tty) {
			p->Open("/dev/tty", O_RDWR); // O_RDONLY stdin
			p->Open("/dev/tty", O_RDWR); // O_WRONLY stdout
			p->Open("/dev/tty", O_RDWR); // O_WRONLY stderr
		}
		// Register the manually created window in the process space for unified cleanup
		if (pf_ptr) p->pforms[0] = pf_ptr;

		if (auto nod = (Dnode*)p->focus_tty) {
			auto pblock = (vtty_type_t*)nod->type;
			pblock->master_pid = Taskman::CurrentPID();
			pblock->proc_group.Append(p->pid);
		}
		
		// Environment is ready, restore state and start the application thread
		p->state = ProcessBlock::State::Active;
		Taskman::AppendThread(p->main_thread);
		IC.enAble();
	}

	extern const char key_map[256], key_map_shift[256];
	while (true) {
		if (pf_ptr) {
			while (pf_ptr->msg_queue.Count()) {
				SheetMessage smsg;
				pf_ptr->msg_queue.Dequeue(smsg);
				if (smsg.event == SheetEvent::onKeybd) {
					auto* key_event = (keyboard_event_t*)smsg.args;
					if (key_event->method == keyboard_event_t::method_t::keydown) {
						auto ascii_ch = (key_event->mod.l_shift || key_event->mod.r_shift ? key_map_shift : key_map)[key_event->keycode];
						if (ascii_ch) {
							// Taskman::DumpTask(Taskman::Locate(Task_Console));
							// ploginfo("blocked_vtty_pid.Count: %u", blocked_vtty_pid.Count());
							// if (blocked_vtty_pid.Count() == 1) {
							// 	ploginfo("blocked_vtty_pid[0]: %u", blocked_vtty_pid[0]);
							// }
							if (auto* q = VTTY_INNQ(tty_target)) {
								q->OutChar(ascii_ch);
							}
						}
					}
				}
				else if (smsg.event == SheetEvent::onClick) {
					bool left_down = (smsg.args[2] & 0x10);
					if (smsg.args[3] == 1 && !left_down) {
						// Force kill all processes in the group
						if (auto nod = tty_target) {
							auto pblock = (vtty_type_t*)nod->type;
							stduint self_pid = Taskman::CurrentPID();

							// Copy the group to a local buffer to ensure stable iteration
							stduint pid_list[32];
							stduint count = pblock->proc_group.Count();
							if (count > 32) count = 32;
							for (stduint i = 0; i < count; i++) pid_list[i] = pblock->proc_group[i];

							for (stdsint i = count - 1; i >= 0; --i) {
								stduint pid = pid_list[i];

								// Protect system tasks but allow killing the master shell
								if ((pid >= TaskCount || pid == pblock->master_pid) && pid != self_pid) {
									ProcessBlock* pb_to_kill = Taskman::Locate(pid);
									if (pb_to_kill && pb_to_kill->state != ProcessBlock::State::Hanging) {
										stduint exit_para[2] = { pb_to_kill->pid, (stduint)-1 };
										syssend(Task_TaskMan, exit_para, sizeof(exit_para), _IMM(TaskmanMsg::EXIT));
										sysrecv(Task_TaskMan, exit_para, sizeof(exit_para));
									}
								}
							}
						}
						goto shell_exit;
					}
				}
			}
		}


		if (auto nod = tty_target) {
			auto pblock = (vtty_type_t*)nod->type;
			if (p && pblock->proc_group.Count() == 0) {
				goto shell_exit;
			}
		}
		else {
			goto shell_exit;
		}
	}

shell_exit:
	if (Consman::ento_gui) {
		auto pblock = (vtty_type_t*)tty_target->type;
		stduint self_pid = Taskman::CurrentPID();
		for (stdsint i = pblock->proc_group.Count() - 1; i >= 0; --i) {
			stduint pid = pblock->proc_group[i];
			if (pid == self_pid) continue;
			auto pb_to_kill = Taskman::Locate(pid);
			if (pb_to_kill && pb_to_kill->state != ProcessBlock::State::Hanging) {
				stduint exit_para[2] = { pb_to_kill->pid, (stduint)-1 };
				syssend(Task_TaskMan, exit_para, sizeof(exit_para), _IMM(TaskmanMsg::EXIT));
				sysrecv(Task_TaskMan, exit_para, sizeof(exit_para));
			}
		}
		{
			IC.enAble(false);
			Consman::RemoveVconsole(tty_target);
			IC.enAble(true);
		}
	}

	Taskman::ExitCurrent(0);
	// Final exit for the shell process itself
	// stduint exit_para[2] = { Taskman::CurrentPID(), 0 };
	// syssend(Task_TaskMan, exit_para, sizeof(exit_para), _IMM(TaskmanMsg::EXIT));
	// Never reach here: the stack will be reclaimed by Taskman
	while (true);
}
#endif

void _Comment(R1) serv_cons_loop()
{
	volatile stduint sig_type = 0, sig_src, ret;
	volatile stduint to_args[4];
	int ch;

	Filesys::MountFilesys(&global_devfs, nullptr, "/dev");
	ploginfo("[DevFs] Mounted to /dev");
	while (true) {
		{
			#if _GUI_ENABLE
			SpinlockLocal guard(&gui_lock);
			#endif
			for (stduint i = 0; i < blocked_vtty_pid.Count(); i++) {
				stduint pid = blocked_vtty_pid[i];
				if (!pid) continue;

				ProcessBlock* ppb = Taskman::Locate(pid);
				// Identify and clean up processes that are no longer active (Hanging or Invalid)
				if (!ppb || ppb->state != ProcessBlock::State::Active) {
					blocked_vtty_pid.Remove(i--);
					continue;
				}

				QueueLimited* q = ppb->focus_tty ? VTTY_INNQ(ppb->focus_tty) : nullptr;
				if (q && -1 != (ch = q->inn())) {
					stdsint val = ch;
					blocked_vtty_pid.Remove(i--);
					syssend(pid, &val, byteof(val));
				}
			}
		}

		// Handle blocked form message requests
		{
			#if _GUI_ENABLE
			SpinlockLocal guard(&gui_lock);
			#endif
			for (stduint i = 0; i < blocked_form_msgs.Count(); i++) {
				auto& b = blocked_form_msgs[i];
				ThreadBlock* th = Taskman::LocateThread(b.sig_src);
				if (!th || !th->parent_process) {
					blocked_form_msgs.Remove(i--);
					continue;
				}
				ProcessBlock* pb = th->parent_process;
				SheetTrait* pfrm = pb->pforms[b.pform_id];
				if (!pfrm) {
					blocked_form_msgs.Remove(i--);
					continue;
				}
				auto pf = static_cast<::uni::Witch::Form*>(pfrm);
				if (pf->msg_queue.Count()) {
					SheetMessage msg;
					pf->msg_queue.Dequeue(msg);
					MccaMemCopyP(b.usr_msg_ptr, pb, &msg, NULL, sizeof(msg));

					stduint val = 1;
					blocked_form_msgs.Remove(i--);
					syssend(b.sig_src, &val, byteof(val));
				}
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

				//
				#if _GUI_ENABLE
			case ConsoleMsg::FNEW:
				ploginfo("creating new form %[x]", to_args[0]);
				ret = ConsoleMsg_FNEW((FMT_ConsoleMsg_FNEW*)to_args, th->parent_process);
				syssend(sig_src, (void*)&ret, sizeof(ret));
				break;

			case ConsoleMsg::FDEL:
			{
				ProcessBlock* pb_target = th->parent_process;
				// If request from System Tasks, to_args[1] is the target PID
				if (sig_src < TaskCount) pb_target = Taskman::Locate(to_args[1]);
				ret = ConsoleMsg_FDEL(to_args[0], pb_target);
				syssend(sig_src, (void*)&ret, sizeof(ret));
			}
			break;


			case ConsoleMsg::FMSG:
				ret = ConsoleMsg_FMSG((FMT_ConsoleMsg_FMSG*)to_args, th->parent_process, sig_src);
				if ((stdsint)ret != -2) {
					syssend(sig_src, (void*)&ret, sizeof(ret));
				}
				break;

			case ConsoleMsg::FDRW:
				ret = ConsoleMsg_FDRW((FMT_ConsoleMsg_FDRW*)to_args, th->parent_process);
				syssend(sig_src, (void*)&ret, sizeof(ret));
				break;
			case ConsoleMsg::FCHR:
				ret = ConsoleMsg_FCHR((FMT_ConsoleMsg_FCHR*)to_args, th->parent_process);
				syssend(sig_src, (void*)&ret, sizeof(ret));
				break;
			case ConsoleMsg::FTIM:
				ret = ConsoleMsg_FTIM(to_args[0], to_args[1], th->parent_process);
				syssend(sig_src, (void*)&ret, sizeof(ret));
				break;


				#endif


			default:
				plogerro("Unknown consmsg type %u", sig_type);
				break;
			}
		}
		syscall(syscall_t::REST);
	}
}

