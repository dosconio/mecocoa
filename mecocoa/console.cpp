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

				//
				#if _GUI_ENABLE
			case ConsoleMsg::FNEW:
				ploginfo("creating new form %[x]", to_args[0]);
				ret = ConsoleMsg_FNEW((FMT_ConsoleMsg_FNEW*)to_args, th->parent_process);
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

//// ---- ---- Bottom Impl ---- ---- ////
#ifdef _ARC_x86 // x86:

void uni::BareConsole::doshow(void* _) {}

#endif




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

