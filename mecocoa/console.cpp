// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <cpp/Witch/Control/Control-Label.hpp>
#include <cpp/Witch/Control/Control-TextBox.hpp>

#if _GUI_ENABLE
extern RecursiveMutex gui_lock;
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
		free((byte*)pblock->output_queue.slice.address);
	}
	free(pblock);
}
static void RefreshConsoleBlockedStateUnlocked();
Dnode* VTTY_Append(Console_t* con) {
	// ploginfo("VTTY_Append called for console at %p", con);
	if (!con) {
		plogerro("%s: You may lose impl Console_t for con", __FUNCIDEN__);
		return nullptr;
	}
	// VTTY registration is console/TTY bookkeeping and does not need GUI tree ownership.
	auto p = vttys.Append(con);
	if (!p) return nullptr;
	const stduint SIZE_INN_BUF = 64;
	auto p_innbuf = new byte[SIZE_INN_BUF];
	const stduint SIZE_OUT_BUF = 4096;// ring buffer for async TTY text output
	auto p_outbuf = new byte[SIZE_OUT_BUF];
	auto pblock = new vtty_type_t();
	pblock->innput_queue = QueueLimited({_IMM(p_innbuf), SIZE_INN_BUF});
	pblock->output_queue = QueueLimited({_IMM(p_outbuf), SIZE_OUT_BUF});
	pblock->id = DevFs::allocate_tty_id();
	p->type = _IMM(pblock);

	if (Taskman::CurrentTB()) {
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
static Mutex console_waiters_mutex;
static volatile byte console_has_blocked_waiters = 0;
static volatile byte console_wake_pending = 0;

// Teardown paths use the current sheet-parent chain to check whether a
// graf-side input reference still points into the subtree being destroyed.
static bool IsSheetInSubtreeOrSelf(SheetTrait* target, SheetTrait* root) {
	SheetTrait* curr = target;
	while (curr) {
		if (curr == root) return true;
		curr = (SheetTrait*)curr->refSheetParent();
	}
	return false;
}

//{TODO} to graphic
// Detach and clean up all child layers under the specified root layer manager.
// Resets each child's parent reference to nullptr and unregisters it from the
// global layout manager timer to prevent callbacks on deleted contexts.
#if _GUI_ENABLE
static void DetachLayerChildren(LayerManager* root) {
	if (!root) return;
	for (auto child = root->subf; child; child = child->next) {
		if (!child->offs) continue;
		auto sheet = cast<SheetTrait*>(child->offs);
		sheet->refSheetParent() = nullptr;
		global_layman.UnregisterTimer(sheet);
	}
	root->subf = nullptr;
	root->subl = nullptr;
}

// Clear global TTY references to a vtty node after its GUI subtree has been
// detached, so later wait/console paths do not follow stale Dnode pointers.
static void CleanupTTYReferences(Dnode* nod) {
	if (!nod) return;

	{
		SpinlockLocal guard(&scheduler_lock);
		for (auto pnod = Taskman::chain.Root(); pnod; pnod = pnod->next) {
			auto p = cast<ProcessBlock*>(pnod->offs);
			auto focus_tty = p->focus_tty.Lock();
			if (*focus_tty == nod) *focus_tty = nullptr;
		}
	}
	{
		MutexLocal waiters_guard(&console_waiters_mutex);
		for (stduint i = 0; i < blocked_vtty_pid.Count(); i++) {
			ProcessBlock* p = ProcessBlock::AcquireActiveByPID(blocked_vtty_pid[i]);
			bool remove_waiter = !p;
			if (p) {
				auto focus_tty = p->focus_tty.Lock();
				remove_waiter = (*focus_tty == nod || *focus_tty == nullptr);
			}
			if (remove_waiter) {
				blocked_vtty_pid.Remove(i--);
			}
			if (p) {
				ProcessBlock::Release(p);
			}
		}
		RefreshConsoleBlockedStateUnlocked();
	}
}

// Clear any pforms slot that still points at a detached Form so later process
// cleanup does not rediscover and free the same Form again.
static void CleanupFormReferences(::uni::Witch::Form* pfrm) {
	if (!pfrm) return;

	SpinlockLocal guard(&scheduler_lock);
	for (auto pnod = Taskman::chain.Root(); pnod; pnod = pnod->next) {
		auto pb = cast<ProcessBlock*>(pnod->offs);
		auto pforms = pb->pforms.Lock();
		for (stduint i = 0; i < pforms->Count(); i++) {
			if ((*pforms)[i] == pfrm) {
				(*pforms)[i] = nullptr;
			}
		}
	}
}

// Destroy one detached vconsole/vtty node. The caller is responsible for any
// higher-level focus_tty / blocked waiter cleanup before removing the node.
static void DestroyVconsoleNode(Dnode* nod) {
	if (!nod) return;
	auto pcon = (uni::VideoConsole2*)nod->offs;
	if (pcon) {
		pcon->Stop();
		delete pcon;
		nod->offs = nullptr;
	}
	vttys.Remove(nod);
}

// Resolve a Console_t base pointer back to its vtty node so teardown paths can
// share the same TTY cleanup sequence.
static Dnode* FindVconsoleNodeByConsole(Console_t* con) {
	if (!con) return nullptr;
	for (auto nod = vttys.Root(); nod; nod = nod->next) {
		if (nod->offs == (pureptr_t)con) {
			return nod;
		}
	}
	return nullptr;
}

static void CleanupAndDestroyVconsoleByConsole(Console_t* con) {
	Dnode* nod = FindVconsoleNodeByConsole(con);
	if (!nod) return;
	CleanupTTYReferences(nod);
	DestroyVconsoleNode(nod);
}

Rectangle Consman::DetachForm(::uni::Witch::Form* pfrm, SheetTrait* exact_sheet) {
	if (!pfrm) return Rectangle{};

	Rectangle area = pfrm->sheet_area;

	// Clear runtime input refs while gui_lock is held so input paths cannot
	// repopulate them before the subtree is detached.
	if (Consman::last_click_sheet &&
		(IsSheetInSubtreeOrSelf(Consman::last_click_sheet, pfrm) ||
		 Consman::last_click_sheet == exact_sheet)) {
		Consman::last_click_sheet = nullptr;
	}
	if (Cursor::moving_sheet &&
		(IsSheetInSubtreeOrSelf(Cursor::moving_sheet, pfrm) ||
		 Cursor::moving_sheet == exact_sheet)) {
		Cursor::moving_sheet = nullptr;
	}

	global_layman.UnregisterTimer(pfrm);
	LayerManager* parent = pfrm->refSheetParent();
	if (parent) parent->Remove(pfrm);
	global_layman.Remove(pfrm);
	pfrm->refSheetParent() = nullptr;
	DetachLayerChildren(static_cast<LayerManager*>(pfrm));
	// Also detach Form's sheet_node.subf chain (close_btn, title_bar, client_area)
	// which is used by Form::getPoint but NOT by LayerManager::subf.
	for (auto child = pfrm->refSheetNode().subf; child; child = child->next) {
		if (!child->offs) continue;
		auto sheet = cast<SheetTrait*>(child->offs);
		sheet->refSheetParent() = nullptr;
		global_layman.UnregisterTimer(sheet);
	}
	return area;
}
#endif

static void RefreshConsoleBlockedStateUnlocked() {
	byte has_waiters = blocked_vtty_pid.Count() || blocked_form_msgs.Count();
	__atomic_store_n(&console_has_blocked_waiters, has_waiters, __ATOMIC_RELEASE);
}

void Consman::WakeBlockedWaiters() {
	if (!__atomic_load_n(&console_has_blocked_waiters, __ATOMIC_ACQUIRE)) return;
	if (__atomic_exchange_n(&console_wake_pending, 1, __ATOMIC_ACQ_REL)) return;
	syssend_async(Task_Console, nullptr, 0, (stduint)ConsoleMsg::TEST);
}

void Consman::WakeBlockedWaitersDeferred() {
	if (!__atomic_load_n(&console_has_blocked_waiters, __ATOMIC_ACQUIRE)) return;
	if (__atomic_exchange_n(&console_wake_pending, 1, __ATOMIC_ACQ_REL)) return;
	#if _MCCA == 0x8664 && defined(_UEFI)
	SysMessage msg = {};
	msg.type = SysMessage::RUPT_CONSOLE_WAKE;
	message_queue.Enqueue(msg);
	#else
	// Fallback for targets that do not run serv_sysmsg(): wake Task_Console directly.
	CommMsg notification_msg = {};
	notification_msg.type = (stduint)ConsoleMsg::TEST;
	msg_send(Taskman::CurrentTB(), Task_Console, &notification_msg, true);
	#endif
}


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
#if _MCCA == 0x8632
static uni::BitmapFontEngine fallback_engine(1);
#else
static uni::BitmapFontEngine gui_font_engine(1);
#endif

_RET_CreateVconsole Consman::CreateVconsole(const Rectangle& rect, rostr title) {
	_RET_CreateVconsole ret;
	auto pcon = new VideoConsole2(NULL,
		Rectangle(Point(2, 2), Size2(rect.width - 10, rect.height - 30)),
		Color::Black, 0xFFFCEAF1
	);

	#if _MCCA == 0x8632
	extern uni::FontEngine* global_ft_engine;
	if (global_ft_engine) {
		pcon->setFontEngine(global_ft_engine);
	}
	else {
		pcon->setFontEngine(&fallback_engine);
	}
	#else
		// Bind standard 16x8 bitmap font engine for 64-bit target
	pcon->setFontEngine(&gui_font_engine);
	#endif

	ret.pcon = pcon;
	// plogwarn(">pcon: %x", pcon);
	// auto vcon_buf = (Color*).allocate(pcon->window.getArea() * sizeof(Color));// Vcon Gen1
	auto text_buf = new BufferChar[pcon->getCols() * pcon->getRows()];
	// plogwarn(">text_buf: %x", text_buf);
	auto line_buf = new Color[pcon->getLineBufferSize()];
	// plogwarn(">line_buf: %x", line_buf);
	if (!text_buf || !line_buf) {
		delete[] text_buf;// ?
		delete[] line_buf;// ?
		delete pcon;
		plogerro("?");
		return ret;
	}
	pcon->setBuffers(nullptr, text_buf, line_buf);

	auto pform = new ::uni::Witch::Form();
	// plogwarn(">pform: %x", pform);
	if (!pform) {
		delete pcon;
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
		RecursiveMutexLocal guard(&gui_lock);
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
	auto pclient = pcon ? pcon->refSheetParent() : nullptr;
	// pcon->refSheetParent() returns &client_area (set by AppendControl),
	// NOT the Form. The Form is client_area's parent (set by setSheet).
	auto pfrm = pclient ? static_cast<uni::Witch::Form*>(pclient->refSheetParent()) : nullptr;
	Rectangle area = pfrm ? pfrm->sheet_area : (pclient ? pclient->sheet_area : Rectangle{});
	{
		#if _GUI_ENABLE
		RecursiveMutexLocal gui_guard(&gui_lock);
		#endif
		if (pcon) {
			global_layman.UnregisterTimer(pcon);
		}
		if (pclient) {
			pclient->Remove(pcon);
		}
		if (pfrm) {
			area = Consman::DetachForm(pfrm, pcon);
			// client_area's children (VideoConsole2 etc.) also need detachment
			DetachLayerChildren(&static_cast<uni::Witch::Form*>(pfrm)->getClientSheet());
			pfrm->refSheetNode().subf = nullptr;
		}

		if (area.width || area.height) {
			global_layman.Update(nullptr, area);
		}
	}

	// Phase 2: once the GUI subtree is detached, clean process ownership and
	// blocked-waiter bookkeeping without holding gui_lock at the same time.
	CleanupFormReferences(pfrm);
	CleanupTTYReferences(nod);

	// Phase 3: destroy detached objects after GUI roots and process bookkeeping
	// no longer reference them.
	// Nullify nod->offs before vttys.Remove so that DnodeHeapFreeSimple
	// calls free(nullptr) (a no-op), avoiding double-free of pcon.
	DestroyVconsoleNode(nod);
	if (pfrm) {
		if (pfrm->sheet_buffer) {
			delete[] pfrm->sheet_buffer;
			pfrm->sheet_buffer = nullptr;
		}
		delete pfrm;
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
	MutexLocal waiters_guard(&console_waiters_mutex);
	for0(i, blocked_vtty_pid.Count()) {
		ProcessBlock* p = ProcessBlock::AcquireActiveByPID(blocked_vtty_pid[i]);
		if (p) {
			auto blocked_focus_tty = p->focus_tty.Lock();
			auto owner_focus_tty = ppb->focus_tty.Lock();
			bool match = (*blocked_focus_tty == *owner_focus_tty);
			ProcessBlock::Release(p);
			if (match) {
				return true;
			}
		}
	}
	return false;
}// To OPTIMIZE

static stduint ProcFormsCount(ProcessBlock* pb) {
	auto pforms = pb->pforms.Lock();
	return pforms->Count();
}

static SheetTrait* ProcFormsGet(ProcessBlock* pb, stduint idx) {
	auto pforms = pb->pforms.Lock();
	return idx < pforms->Count() ? (*pforms)[idx] : nullptr;
}

static void ProcFormsSet(ProcessBlock* pb, stduint idx, SheetTrait* value) {
	auto pforms = pb->pforms.Lock();
	if (idx < pforms->Count()) {
		(*pforms)[idx] = value;
	}
}

static stdsint ProcFormsFindEmptyOrAppend(ProcessBlock* pb) {
	auto pforms = pb->pforms.Lock();
	for (stduint i = 0; i < pforms->Count(); i++) {
		if ((*pforms)[i] == nullptr) {
			return i;
		}
	}
	if (pforms->Count() < DEFAULT_FILES_LIMIT) {
		stduint slot_idx = pforms->Count();
		pforms->Append(nullptr);
		return slot_idx;
	}
	return -1;
}

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
	stdsint slot_idx = ProcFormsFindEmptyOrAppend(pb);
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

	{
		// [Lock]: Protect global registration
		#if _GUI_ENABLE
		RecursiveMutexLocal guard(&gui_lock);
		#endif
		global_layman.Append(pfrm);
		Consman::SwitchForm(pfrm);
	}

	// Store in process block pforms
	ProcFormsSet(pb, slot_idx, pfrm);
	pfrm->usrp_owner = pb;

	return slot_idx;
}

static void _CleanSingleForm(ProcessBlock* pb, stduint pform_id) {
	SheetTrait* pfrm = ProcFormsGet(pb, pform_id);
	if (!pfrm) return;

	// Recursive destroy sub-forms belonging to this process that are children of this form
	for (stduint i = 0, count = ProcFormsCount(pb); i < count; i++) {
		SheetTrait* child_form = ProcFormsGet(pb, i);
		if (child_form && child_form->refSheetParent() == (LayerManager*)pfrm) {
			_CleanSingleForm(pb, i);
		}
	}

	Rectangle area = pfrm->sheet_area;
	{
		// [Lock]: Protect global layman chain, focus switch, and object lifetime.
		#if _GUI_ENABLE
		RecursiveMutexLocal guard(&gui_lock);
		#endif

		area = Consman::DetachForm(static_cast<::uni::Witch::Form*>(pfrm));
		// Delete VideoConsole2 children from client_area.subf
		LayerManager& client = static_cast<uni::Witch::Form*>(pfrm)->getClientSheet();
		while (client.subf) {
			SheetTrait* child_sheet = cast<SheetTrait*>(client.subf->offs);
			client.Remove(child_sheet);
			if (child_sheet) {
				child_sheet->refSheetParent() = nullptr;
				global_layman.UnregisterTimer(child_sheet);
				// IMPORTANT: vttys stores Console_t* (base class), but child_sheet
				// is SheetTrait* (another base). Cast through VideoConsole2* first
				// so the lookup uses the correct Console_t base address.
				Console_t* con_base = static_cast<Console_t*>(static_cast<uni::VideoConsole2*>(child_sheet));
				CleanupAndDestroyVconsoleByConsole(con_base);
			}
		}
		DetachLayerChildren(&static_cast<uni::Witch::Form*>(pfrm)->getClientSheet());
		pfrm->refSheetNode().subf = nullptr;
		global_layman.Update(nullptr, area);

		// Phase 2: nullify any pforms slot still pointing at this detached Form
		// before delete so later process cleanup cannot rediscover the same object.
		CleanupFormReferences(static_cast<::uni::Witch::Form*>(pfrm));
		ProcFormsSet(pb, pform_id, nullptr);
		// Safe memory release remains under gui_lock so no stale GUI path can race the free.
		if (pfrm->sheet_buffer) {
			delete[] pfrm->sheet_buffer;
			pfrm->sheet_buffer = nullptr;
		}

		delete static_cast<::uni::Witch::Form*>(pfrm);
	}

	// Unblock threads waiting for this specific form
	{
		MutexLocal waiters_guard(&console_waiters_mutex);
		for (stduint i = 0; i < blocked_form_msgs.Count(); i++) {
			if (blocked_form_msgs[i].pform_id == pform_id) {
				stdsint err_val = -1;
				ProcessBlock* safe_pb = ProcessBlock::AcquireActive(blocked_form_msgs[i].sig_src);
				if (safe_pb) {
					syssend_async(blocked_form_msgs[i].sig_src, &err_val, sizeof(err_val));
				}
				if (safe_pb) {
					ProcessBlock::Release(safe_pb);
				}
				blocked_form_msgs.Remove(i--);
			}
		}
		RefreshConsoleBlockedStateUnlocked();
	}

	// [Message Queue Fix]: Remove any pending blocking requests for this form
	{
		MutexLocal waiters_guard(&console_waiters_mutex);
		for (stduint i = 0; i < blocked_form_msgs.Count(); i++) {
			if (blocked_form_msgs[i].pform_id == pform_id) {
				blocked_form_msgs.Remove(i);
				i--;
			}
		}
		RefreshConsoleBlockedStateUnlocked();
	}
}

static stdsint ConsoleMsg_FDEL(stduint pform_id, ProcessBlock* pb) {
	if (pform_id == ~_IMM0) {
		for (stduint i = 0, count = ProcFormsCount(pb); i < count; i++) {
			_CleanSingleForm(pb, i);
		}
		return 0;
	}
	if (!ProcFormsGet(pb, pform_id)) return -1;
	_CleanSingleForm(pb, pform_id);
	return 0;
}

void Global_CleanProcessForms(ProcessBlock* pb) {
	if (!pb) return;
	for (stduint pform_id = 0, count = ProcFormsCount(pb); pform_id < count; pform_id++) {
		_CleanSingleForm(pb, pform_id);
	}
}



static stdsint ConsoleMsg_FBID(const FMT_ConsoleMsg_FBID* data, ProcessBlock* pb) {
	::uni::Witch::Form* pf = static_cast<::uni::Witch::Form*>(ProcFormsGet(pb, data->pform_id));
	if (!pf) return -1;

	pf->usrp_buffer = data->usrp_buffer;
	pf->usrp_owner = pb;

	return 0;
}

static stdsint ConsoleMsg_FUPD(const FMT_ConsoleMsg_FUPD* data, ProcessBlock* pb) {
	SheetTrait* pfrm = ProcFormsGet(pb, data->pform_id);
	if (!pfrm) return -1;

	::uni::Witch::Form* pf = static_cast<::uni::Witch::Form*>(pfrm);
	void* user_buf = pf->usrp_buffer;
	if (!user_buf || !pfrm->sheet_buffer) return -1;

	Rectangle client_area = pf->getClientArea();
	ProcessBlock* owner = (ProcessBlock*)pf->usrp_owner;
	if (!owner) owner = pb;

	Rectangle dirty_rect;
	bool has_dirty = false;
	if (data->usrp_rect) {
		if (MccaMemCopyP(&dirty_rect, NULL, data->usrp_rect, pb, sizeof(dirty_rect)) == sizeof(dirty_rect)) {
			has_dirty = true;
		}
	}

	if (has_dirty) {
		if (dirty_rect.x >= client_area.width || dirty_rect.y >= client_area.height) {
			return 0;
		}
		stduint end_x = dirty_rect.x + dirty_rect.width;
		if (end_x < dirty_rect.x || end_x > client_area.width) {
			dirty_rect.width = client_area.width - dirty_rect.x;
		}
		stduint end_y = dirty_rect.y + dirty_rect.height;
		if (end_y < dirty_rect.y || end_y > client_area.height) {
			dirty_rect.height = client_area.height - dirty_rect.y;
		}
		if (dirty_rect.width == 0 || dirty_rect.height == 0) {
			return 0;
		}
	} else {
		dirty_rect.x = 0;
		dirty_rect.y = 0;
		dirty_rect.width = client_area.width;
		dirty_rect.height = client_area.height;
	}

	// Copy user pixels directly into Form's sheet_buffer at client area + dirty rect offset
	for (stduint i = 0; i < dirty_rect.height; i++) {
		Color* dst = pfrm->sheet_buffer + (client_area.y + dirty_rect.y + i) * pfrm->sheet_area.width + (client_area.x + dirty_rect.x);
		Color* src = (Color*)user_buf + (dirty_rect.y + i) * client_area.width + dirty_rect.x;
		MccaMemCopyP(dst, NULL, src, owner, dirty_rect.width * sizeof(Color));
	}

	Rectangle update_rect(
		Point(client_area.x + dirty_rect.x, client_area.y + dirty_rect.y),
		Size2(dirty_rect.width, dirty_rect.height)
	);
	global_layman.Update(pfrm, update_rect);
	return 0;
}

static stdsint ConsoleMsg_FMSG(const FMT_ConsoleMsg_FMSG* data, ProcessBlock* pb, stduint sig_src) {
	SheetTrait* pfrm = ProcFormsGet(pb, data->pform_id);
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
		MutexLocal waiters_guard(&console_waiters_mutex);
		blocked_form_msgs.Append({ sig_src, data->pform_id, data->message });
		RefreshConsoleBlockedStateUnlocked();
		return -2; // Signal that reply is deferred
	}

	return 0; // No message available
}

static stdsint ConsoleMsg_FDRW(const FMT_ConsoleMsg_FDRW* data, ProcessBlock* pb) {
	// Target form from process pforms
	SheetTrait* pfrm = ProcFormsGet(pb, data->pform_id);
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
	SheetTrait* pfrm = ProcFormsGet(pb, data->pform_id);
	if (!pfrm) return -1;

	// Copy parameters from user space
	Point vertex;
	if (MccaMemCopyP(&vertex, NULL, data->usrp_vertex, pb, sizeof(vertex)) != sizeof(vertex)) return -1;
	String buf(String::Charset::UTF8, 256);
	stduint len = StrCopyP(buf.reflect(), kernel_paging, data->usrp_str, pb->paging, 256);
	buf.Refresh();

	// [Security Fix]: Validate drawing bounds to prevent heap corruption
	if ((vertex.x + (stdsint)buf.getByteCount() * _TEMP 8) > pfrm->sheet_area.width ||
		(vertex.y + 16) > pfrm->sheet_area.height) {
		return -1;
	}

	// Draw using DrawString_16 (8x16 font)
	uni::DrawString_16(*pfrm, vertex, String(buf), data->color);

	// Update the dirty area
	Rectangle dirty_rect(vertex, Size2(buf.getByteCount() * 8, 16));
	global_layman.Update(pfrm, dirty_rect);

	return 0;
}

static stdsint ConsoleMsg_FTIM(stduint pform_id, stduint ms, ProcessBlock* pb) {
	SheetTrait* pfrm = ProcFormsGet(pb, pform_id);
	if (!pfrm) return -1;

	// Calculate timer period (convert ms to ticks)
	stduint period = ms * SysTickFreq / 1000;
	if (ms && !period) period = 1;

	#if _GUI_ENABLE
	RecursiveMutexLocal guard(&gui_lock);
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

static stduint ConsoleMsg_RDWR(const FMT_ConsoleMsg_RDWR* data, bool wr_type) {
	ProcessBlock* pb = ProcessBlock::AcquireActiveByPID(data->pid);
	if (!pb) return 0;
	const stduint rw_buffer_size = 0x8000;
	byte* rw_buffer = (byte*)malloc(rw_buffer_size);
	if (!rw_buffer) {
		ProcessBlock::Release(pb);
		return 0;
	}

	stduint total_bytes = 0;
	stduint bytes_left = data->len;
	stduint curr_addr = data->usr_addr;
	Dnode* tty_node = nullptr;
	{
		// Resolve the current foreground TTY inside Task_Console so fileman
		// does not carry focus_tty ownership across the actual tty I/O.
		auto focus_tty = pb->focus_tty.Lock();
		tty_node = *focus_tty;
	}
	if (!tty_node) {
		ProcessBlock::Release(pb);
		return 0;
	}

	while (bytes_left > 0) {
		stduint chunk = minof(bytes_left, rw_buffer_size);
		stdsint bytes_processed = 0;

		if (wr_type) {
			MemCopyP(rw_buffer, kernel_paging, (void*)curr_addr, pb->paging, chunk);
			bytes_processed = (stdsint)global_devfs.writfl(tty_node, Slice{ 0, chunk }, rw_buffer);
		}
		else {
			bytes_processed = (stdsint)global_devfs.readfl(tty_node, Slice{ 0, chunk }, rw_buffer);
			if (bytes_processed > 0) {
				MemCopyP((void*)curr_addr, pb->paging, rw_buffer, kernel_paging, bytes_processed);
			}
		}

		if (bytes_processed <= 0) {
			break;
		}

		total_bytes += (stduint)bytes_processed;
		curr_addr += (stduint)bytes_processed;
		bytes_left -= (stduint)bytes_processed;
		if ((stduint)bytes_processed < chunk) {
			break;
		}
	}

	free(rw_buffer);
	ProcessBlock::Release(pb);
	return total_bytes;
}

void _Comment(R1) serv_cons_loop()
{
	volatile stduint sig_type = 0, sig_src, ret;
	volatile stduint to_args[4];
	int ch;

	Filesys::MountFilesys(&global_devfs, nullptr, "/dev");
	ploginfo("[DevFs] Mounted to /dev");
	while (true) {
		bool skip_waiter_maintenance = false;
		// Block here to wait for messages or wake notifications
		sysrecv(ANYPROC, (void*)to_args, byteof(to_args), (usize*)&sig_type, (usize*)&sig_src);
		__atomic_store_n(&console_wake_pending, 0, __ATOMIC_RELEASE);
		ProcessBlock* safe_pb = ProcessBlock::Acquire(sig_src);
		if (safe_pb) {
			switch (ConsoleMsg(sig_type)) {
			case ConsoleMsg::TEST: break;

			case ConsoleMsg::READ:
				ret = ConsoleMsg_RDWR((FMT_ConsoleMsg_RDWR*)to_args, false);
				syssend_async(sig_src, (void*)&ret, sizeof(ret));
				// Keep tty read/write isolated from blocked waiter maintenance.
				skip_waiter_maintenance = true;
				break;
			case ConsoleMsg::WRIT:
				ret = ConsoleMsg_RDWR((FMT_ConsoleMsg_RDWR*)to_args, true);
				syssend_async(sig_src, (void*)&ret, sizeof(ret));
				skip_waiter_maintenance = true;
				break;

			case ConsoleMsg::INNC:// ReadChar(ASCII): normal \n \r ...
				if (!ifContainBlockedTTY(safe_pb)) {
					MutexLocal waiters_guard(&console_waiters_mutex);
					blocked_vtty_pid.Append(safe_pb->getID());
					RefreshConsoleBlockedStateUnlocked();
					// ploginfo("Blocked TTY %u by %u", pb->focus_tty, pb->getID());
				}
				else {
					ret = ~_IMM0;
					syssend_async(sig_src, (void*)&ret, sizeof(ret), 0);
				}
				break;

				//
				#if _GUI_ENABLE
			case ConsoleMsg::FNEW:
			{
				#if _GUI_ENABLE
				RecursiveMutexLocal guard(&gui_lock);
				#endif
				ploginfo("creating new form %[x]", to_args[0]);
				ret = ConsoleMsg_FNEW((FMT_ConsoleMsg_FNEW*)to_args, safe_pb);
			}
			syssend_async(sig_src, (void*)&ret, sizeof(ret));
			break;

			case ConsoleMsg::FDEL:
			{
				#if _GUI_ENABLE
				RecursiveMutexLocal guard(&gui_lock);
				#endif
				ProcessBlock* pb_target = nullptr;
				bool need_release = false;
				if (sig_src < TaskCount) {
					pb_target = ProcessBlock::AcquireActiveByPID(to_args[1]);
					need_release = true;
				}
				else {
					pb_target = safe_pb;
				}
				if (pb_target) {
					ret = ConsoleMsg_FDEL(to_args[0], pb_target);
					if (need_release) {
						ProcessBlock::Release(pb_target);
					}
				}
				else {
					ret = -1;
				}
			}
			syssend_async(sig_src, (void*)&ret, sizeof(ret));
			break;


			case ConsoleMsg::FMSG:
			{
				#if _GUI_ENABLE
				RecursiveMutexLocal guard(&gui_lock);
				#endif
				ret = ConsoleMsg_FMSG((FMT_ConsoleMsg_FMSG*)to_args, safe_pb, sig_src);
			}
			if ((stdsint)ret != -2) {
				syssend_async(sig_src, (void*)&ret, sizeof(ret));
			}
			break;

			case ConsoleMsg::FDRW:
			{
				#if _GUI_ENABLE
				RecursiveMutexLocal guard(&gui_lock);
				#endif
				ret = ConsoleMsg_FDRW((FMT_ConsoleMsg_FDRW*)to_args, safe_pb);
			}
			syssend_async(sig_src, (void*)&ret, sizeof(ret));
			break;
			case ConsoleMsg::FCHR:
			{
				#if _GUI_ENABLE
				RecursiveMutexLocal guard(&gui_lock);
				#endif
				ret = ConsoleMsg_FCHR((FMT_ConsoleMsg_FCHR*)to_args, safe_pb);
			}
			syssend_async(sig_src, (void*)&ret, sizeof(ret));
			break;
			case ConsoleMsg::FBID:
			{
				#if _GUI_ENABLE
				RecursiveMutexLocal guard(&gui_lock);
				#endif
				ret = ConsoleMsg_FBID((FMT_ConsoleMsg_FBID*)to_args, safe_pb);
			}
			syssend_async(sig_src, (void*)&ret, sizeof(ret));
			break;
			case ConsoleMsg::FUPD:
			{
				#if _GUI_ENABLE
				RecursiveMutexLocal guard(&gui_lock);
				#endif
				ret = ConsoleMsg_FUPD((FMT_ConsoleMsg_FUPD*)to_args, safe_pb);
			}
			syssend_async(sig_src, (void*)&ret, sizeof(ret));
			break;
			case ConsoleMsg::FTIM:
			{
				#if _GUI_ENABLE
				RecursiveMutexLocal guard(&gui_lock);
				#endif
				ret = ConsoleMsg_FTIM(to_args[0], to_args[1], safe_pb);
			}
			syssend_async(sig_src, (void*)&ret, sizeof(ret));
			break;


			#endif
			#if _GUI_ENABLE
			case ConsoleMsg::FCLEANPROC:
			{
				auto data = (FMT_ConsoleMsg_FCLEANPROC*)to_args;
				ProcessBlock* target_pb = (ProcessBlock*)data->process_block;
				ret = target_pb ? 0 : (stduint)-1;
				if (target_pb) {
					Global_CleanProcessForms(target_pb);
				}
				syssend_async(sig_src, (void*)&ret, sizeof(ret));
				skip_waiter_maintenance = true;
			}
			break;
			#endif


			default:
				plogerro("Unknown consmsg type %u", sig_type);
				break;
			}
			ProcessBlock::Release(safe_pb);
		}
		if (skip_waiter_maintenance) {
			continue;
		}

		{
			MutexLocal waiters_guard(&console_waiters_mutex);
			for (stduint i = 0; i < blocked_vtty_pid.Count(); i++) {
				stduint pid = blocked_vtty_pid[i];
				if (!pid) continue;

				ProcessBlock* ppb = ProcessBlock::AcquireActiveByPID(pid);
				// Identify and clean up processes that are no longer accessible for active resource use.
				if (!ppb) {
					blocked_vtty_pid.Remove(i--);
					continue;
				}

				auto focus_tty = ppb->focus_tty.Lock();
				QueueLimited* q = *focus_tty ? VTTY_INNQ(*focus_tty) : nullptr;
				if (q && -1 != (ch = q->inn())) {
					stdsint val = ch;
					blocked_vtty_pid.Remove(i--);
					syssend_async(pid, &val, byteof(val));
				}
				ProcessBlock::Release(ppb);
			}
			RefreshConsoleBlockedStateUnlocked();
		}

		// Handle blocked form message requests
		{
			MutexLocal waiters_guard(&console_waiters_mutex);
			for (stduint i = 0; i < blocked_form_msgs.Count(); i++) {
				auto& b = blocked_form_msgs[i];
				ProcessBlock* pb = ProcessBlock::AcquireActive(b.sig_src);
				if (!pb) {
					blocked_form_msgs.Remove(i--);
					continue;
				}
				#if _GUI_ENABLE
				RecursiveMutexLocal guard(&gui_lock);
				#endif
				SheetTrait* pfrm = ProcFormsGet(pb, b.pform_id);
				if (!pfrm) {
					ProcessBlock::Release(pb);
					blocked_form_msgs.Remove(i--);
					continue;
				}
				auto pf = static_cast<::uni::Witch::Form*>(pfrm);
				if (pf->msg_queue.Count()) {
					SheetMessage msg;
					pf->msg_queue.Dequeue(msg);
					MccaMemCopyP(b.usr_msg_ptr, pb, &msg, NULL, sizeof(msg));

					stduint val = 1;
					stduint target_sig_src = b.sig_src;
					blocked_form_msgs.Remove(i--);
					syssend_async(target_sig_src, &val, byteof(val));
				}
				ProcessBlock::Release(pb);
			}
			RefreshConsoleBlockedStateUnlocked();
		}
	}
}

