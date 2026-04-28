// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <cpp/Witch/Form.hpp>
#include <cpp/Witch/Control/Control-Label.hpp>
#include <cpp/Witch/Control/Control-TextBox.hpp>


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
	if (id == current_screen_TTY) return;
	if (id >= TTY_NUMBER) {
		plogerro("TTY Id %u", id);
		return;
	}
	Bcons[current_screen_TTY].last_curposi = curget();
	Bcons[id].setStartLine(topline + crtline);
	current_screen_TTY = id;
	curset(Bcons[id].last_curposi);
	//
	for0(i, 4) Bcons[i].auto_incbegaddr = false;
	Bcons[id].auto_incbegaddr = true;
}

#endif

//// ---- ---- DYNAMIC CORE ---- ---- ////

static bool ifContainBlockedTTY(ProcessBlock* ppb) {
	for0(i, blocked_vtty_pid.Count()) {
		if (Taskman::Locate(blocked_vtty_pid[i])->focus_tty == ppb->focus_tty) {
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

static stdsint ConsoleMsg_FDEL(stduint pform_id, ProcessBlock* pb) {
	if (pform_id == ~_IMM0) {
		for (stduint i = 0; i < _TEMP 4; i++) {
			if (pb->pforms[i]) ConsoleMsg_FDEL(i, pb);
		}
		return 0;
	}
	if (pform_id >= _TEMP 4) return -1;
	SheetTrait* pfrm = pb->pforms[pform_id];
	if (!pfrm) return -1;

	// Recursive destroy sub-forms belonging to this process
	for (stduint i = 0; i < _TEMP 4; i++) {
		if (pb->pforms[i] && pb->pforms[i]->refSheetParent() == (LayerManager*)pfrm) {
			ConsoleMsg_FDEL(i, pb);
		}
	}

	// Remove from parent layer manager (e.g., global_layman)
	LayerManager* parent = pfrm->refSheetParent();
	if (parent) {
		Nnode* target = &pfrm->refSheetNode();
		if (target->left) target->left->next = target->next;
		else parent->subf = target->next;
		if (target->next) target->next->left = target->left;
		else parent->subl = target->left;
		target->left = target->next = nullptr;
		pfrm->refSheetParent() = nullptr;
	}
	global_layman.Update(pfrm, Rectangle{ Point2(0,0), pfrm->sheet_area.getSize() });

	// Unblock and notify threads waiting for this form
	for (stduint i = 0; i < blocked_form_msgs.Count(); i++) {
		if (blocked_form_msgs[i].pform_id == pform_id) {
			stdsint err_val = -1;
			syssend(blocked_form_msgs[i].sig_src, &err_val, sizeof(err_val));
			blocked_form_msgs.Remove(i--);
		}
	}

	// Release resources
	if (pfrm->sheet_buffer) {
		delete[] pfrm->sheet_buffer;
		pfrm->sheet_buffer = nullptr;
	}
	delete pfrm;
	pb->pforms[pform_id] = nullptr;

	return 0;
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
	ploginfo("ConsoleMsg_FDRW: draw %d", data->shape_type);
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
		lm.DrawLine(cl.disp, cl.size, cl.color);
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

_PACKED(struct) FMT_ConsoleMsg_FCHR {
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

	// Draw using DrawString_16 (8x16 font)
	uni::DrawString_16(*pfrm, vertex, String(buf), data->color);

	// Update the dirty area
	Rectangle dirty_rect(vertex, Size2(StrLength(buf) * 8, 16));
	global_layman.Update(pfrm, dirty_rect);

	return 0;
}
#endif


void _Comment(R1) serv_cons_loop()
{
	volatile stduint sig_type = 0, sig_src, ret;
	volatile stduint to_args[4];	
	int ch;
	
	_TEMP devfs_register_and_mount();
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

		// Handle blocked form message requests
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
				stdsint res_val = 1;
				syssend(b.sig_src, &res_val, sizeof(res_val));
				blocked_form_msgs.Remove(i--);
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
					// If request from Taskman, to_args[1] is the target PID
					if (sig_src == Task_TaskMan) pb_target = Taskman::Locate(to_args[1]);
					ret = ConsoleMsg_FDEL(to_args[0], pb_target);
					syssend(sig_src, (void*)&ret, sizeof(ret));
				}
				break;


			case ConsoleMsg::FMSG:
				ret = ConsoleMsg_FMSG((FMT_ConsoleMsg_FMSG*)to_args, th->parent_process, sig_src);
				if (ret != -2) {
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


				#endif


			default:
				plogerro("Unknown syscall type %u", sig_type);
				break;
			}
		}
		syscall(syscall_t::REST);
	}
}

