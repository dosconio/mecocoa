// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Graphic - Video, Mouse and GUI Operations
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

static void SafeLaymanUpdate(SheetTrait* sheet, const Rectangle& rect) {
	#if _GUI_ENABLE
	global_layman.Lock()->Update(sheet, rect);
	#endif
}


#include <c/driver/mouse.h>
#include <c/driver/keyboard.h>
#include "../include/console.hpp"
#include "../include/filesys.hpp"


#if (_MCCA & 0xFF00) == 0x8600 // _GUI_ENABLE

const int kMouseCursorWidth = 15;
const int kMouseCursorHeight = 24;
char mouse_cursor_shape[kMouseCursorHeight][kMouseCursorWidth + 1] = {
	"@              ",
	"@@             ",
	"@.@            ",
	"@..@           ",
	"@...@          ",
	"@....@         ",
	"@.....@        ",
	"@......@       ",
	"@.......@      ",
	"@........@     ",
	"@.........@    ",
	"@..........@   ",
	"@...........@  ",
	"@............@ ",
	"@......@@@@@@@@",
	"@.....@        ",
	"@....@         ",
	"@...@          ",
	"@..@           ",
	"@.@            ",
	"@@             ",
	"@              ",
	"               ",
	"               ",
};

void Cursor::doshow(void* _) {
	auto p = _ ? (Color*)_ : sheet_buffer;
	if (!p) return;
	for0(dy, kMouseCursorHeight) for0(dx, kMouseCursorWidth) {
		if (mouse_cursor_shape[dy][dx] == '@') {
			*p++ = 0xFF000000;// Point(position.x + dx, position.y + dy)
		}
		else if (mouse_cursor_shape[dy][dx] == '.') {
			*p++ = 0x7FFFFFFF;
		}
		else {
			*p++ = 0x00FFFFFF;
		}
	}
}

void Cursor::setSheet(LayerManager& layman, const Point& vertex) {
	sheet_buffer = (Color*)mem.allocate(kMouseCursorWidth * kMouseCursorHeight * sizeof(Color));
	doshow(sheet_buffer);
	InitializeSheet(layman, vertex, { kMouseCursorWidth,kMouseCursorHeight }, sheet_buffer);
	layman.Append(this);
}

// hand_mouse - runs only in Graphic thread, no lock needed
void hand_mouse(MouseMessage mmsg) {
	byte change_btns = 0;// 0RML0RML
	if (Cursor::mouse_btnl_dn != mmsg.BtnLeft) change_btns |= 0b001;
	if (Cursor::mouse_btnm_dn != mmsg.BtnMiddle) change_btns |= 0b010;
	if (Cursor::mouse_btnr_dn != mmsg.BtnRight) change_btns |= 0b100;
	if (mmsg.BtnLeft) change_btns |= 0b00010000;
	if (mmsg.BtnMiddle) change_btns |= 0b00100000;
	if (mmsg.BtnRight) change_btns |= 0b01000000;

	if ((change_btns & 0b1) && !mmsg.BtnLeft) {
		Cursor::moving_sheet = nullptr;
	}

	Cursor::mouse_btnl_dn = mmsg.BtnLeft;
	Cursor::mouse_btnm_dn = mmsg.BtnMiddle;
	Cursor::mouse_btnr_dn = mmsg.BtnRight;
	Point cursor_p = Cursor::global_cursor->sheet_area.getVertex();

	// Handle Clicks and Z-order
	SheetTrait* sheet = nullptr;
	bool needs_update = false;
	SheetTrait* leave_sheet = nullptr;
	SheetTrait* click_sheet = nullptr;
	Point click_rel_p;

	{
		auto layman = global_layman.Lock();
		if ((change_btns & 0b111) && (sheet = layman->getTop(cursor_p, 1))) {
			click_rel_p = cursor_p - sheet->sheet_area.getVertex();
			if (Consman::last_click_sheet && Consman::last_click_sheet != sheet) {
				leave_sheet = Consman::last_click_sheet;
			}
			click_sheet = sheet;
			Consman::last_click_sheet = sheet;

			// Bring to front (Insert after subf, which is the cursor)
			Nnode* target = &sheet->refSheetNode();
			if (layman->subf && target != layman->subf->next && target != layman->subl) {
				// Nchain::Exchange-like manual manipulation
				Nnode* prev = target->left;
				Nnode* next = target->next;
				if (prev) prev->next = next;
				if (next) next->left = prev;

				Nnode* top = layman->subf;
				Nnode* sec = top->next;

				target->left = top;
				target->next = sec;
				top->next = target;
				if (sec) sec->left = target;

				needs_update = true;
			}
		}
	}
	
	if (leave_sheet) {
		leave_sheet->onrupt(SheetEvent::onLeave, Point(0, 0), 1);
	}
	if (click_sheet) {
		click_sheet->onrupt(SheetEvent::onClick, click_rel_p, change_btns);
	}
	
	if (needs_update) {
		SafeLaymanUpdate(nullptr, sheet->sheet_area);
	}

	// Handle Movement
	if (mmsg.X || mmsg.Y) {
		{
			auto layman = global_layman.Lock();
			layman->Domove(Cursor::global_cursor, { mmsg.X, mmsg.Y });
			if (Cursor::moving_sheet) {
				layman->Domove(Cursor::moving_sheet, { mmsg.X, mmsg.Y });
			}
		}

		static uint64 last_onmoved_tick = 0;
		if ((change_btns & 0b111) || (tick - last_onmoved_tick >= 20)) {
			Point current_cursor_p = Cursor::global_cursor->sheet_area.getVertex();
			SheetTrait* hover_sheet = global_layman.Lock()->getTop(current_cursor_p, 1);
			if (hover_sheet) {
				Point rel_p = current_cursor_p - hover_sheet->sheet_area.getVertex();
				hover_sheet->onrupt(SheetEvent::onMoved, rel_p, change_btns);
			}
			last_onmoved_tick = tick;
		}
	}
}

// USB mouse callback — enqueues to message_queue_conv so serv_graf_loop
// (Graphic thread) processes the event via hand_mouse. This avoids racing
// with GraphicMsg::FDEL on global_layman / Consman::last_click_sheet.
void hand_mouse_usb(MouseMessage mmsg) {
	if (!Consman::ento_gui) return; // No GUI: mouse events are meaningless
	extern SpinlockBlock<uni::Queue<SysMessage>> message_queue_conv;
	SysMessage msg;
	msg.type = SysMessage::RUPT_MOUSE;
	msg.args.mou_event = mmsg;
	message_queue_conv.Lock()->Enqueue(msg);
}

void LayerManager::Dorupt(SheetTrait* who, SheetEvent event, Point rel_p, para_list args) {
	if (event == SheetEvent::onClick) {
		byte state = para_next(args, stduint);
		if ((state & 0b10001) == 0b10001) {
			Cursor::moving_sheet = who;
		}
	}
}

// LayerManager2::Update - runs only in Graphic thread, no lock needed
void LayerManager2::Update(SheetTrait* who, const Rectangle& rect) {
	Rectangle abs_rect = who ? who->sheet_area : window;
	abs_rect.x += rect.x;
	abs_rect.y += rect.y;
	Rectangle dirty_rect(Point(abs_rect.x, abs_rect.y), Size2(rect.width, rect.height));

	this->AddDirty(dirty_rect);
	if (!lazy_update) UpdateForce(who, rect);
}

void LayerManager2::UpdateForce(SheetTrait* who, const Rectangle& rect) {
	if (!this) return;
	// Local VCI for blending into the sheet_buffer (back-buffer)
	VideoControlInterfaceMARGB8888 vcim(sheet_buffer, window.getSize());
	Rectangle abs_rect = who ? who->sheet_area : window;
	abs_rect.x += rect.x;
	abs_rect.y += rect.y;
	// Iterate through the rectangle and composite layers
	for0(i, rect.height) {
		Point point(abs_rect.x + 0, abs_rect.y + i);
		if (point.y >= window.height) break;
		for0(j, rect.width) {
			if (point.x >= window.width) break;
			if (sheet_buffer) {
				vcim.DrawPoint(point, EvaluateColor(point));
			}
			else if (pvci) pvci->DrawPoint(point, EvaluateColor(point));
			point.x++;
		}
	}
	// Propagate to parent if exists
	if (sheet_parent) {
		sheet_parent->Update(this, Rectangle(
			Point(abs_rect.x, abs_rect.y),
			Size2(rect.width, rect.height)
		));
	}
}

#endif

// ---- ---- ---- ---- . ---- ---- ---- ----

static SysMessage _BUF_Message_Conv[64];
SpinlockBlock<uni::Queue<SysMessage>> message_queue_conv(_BUF_Message_Conv, numsof(_BUF_Message_Conv));
volatile bool has_pending_timer = false;

extern void sysmsg_kbd(keyboard_event_t kbd_event);

struct GuiCleanupJob {
	stduint pid = 0;
	uni::Vector<SheetTrait*> forms;
};

static GuiCleanupJob* _BUF_PendingGuiCleanupJobs[32];
static SpinlockBlock<uni::Queue<GuiCleanupJob*>> pending_gui_cleanup_jobs(
	_BUF_PendingGuiCleanupJobs,
	numsof(_BUF_PendingGuiCleanupJobs));

static bool PopGuiCleanupJob(GuiCleanupJob*& out_job) {
	out_job = nullptr;
	return pending_gui_cleanup_jobs.Lock()->Dequeue(out_job);
}


// ---- ---- GUI Operation Helpers (Graphic thread only) ---- ----

#if _GUI_ENABLE
extern Mutex console_waiters_mutex;

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

// Detach and clean up all child layers under the specified root layer manager.
static void DetachLayerChildren(LayerManager* root) {
	if (!root) return;
	for (auto child = root->subf; child; child = child->next) {
		if (!child->offs) continue;
		auto sheet = cast<SheetTrait*>(child->offs);
		sheet->refSheetParent() = nullptr;
		global_layman.Lock()->UnregisterTimer(sheet);
	}
	root->subf = nullptr;
	root->subl = nullptr;
}

// Clear global TTY references to a vtty node after its GUI subtree has been detached.
// Must NOT hold scheduler_lock while acquiring focus_tty's Mutex, because
// Mutex::Acquire may sleep, and sleeping with a spinlock held deadlocks SMP.
static void CleanupTTYReferences(Dnode* nod) {
	if (!nod) return;
	// Collect PIDs under scheduler_lock (short hold, no allocation)
	constexpr stduint MAX_PROCS = 128;
	stduint pids[MAX_PROCS];
	stduint count = 0;
	{
		extern Spinlock scheduler_lock;
		SpinlockLocal guard(&scheduler_lock);
		for (auto pnod = Taskman::chain.Root(); pnod && count < MAX_PROCS; pnod = pnod->next) {
			auto p = cast<ProcessBlock*>(pnod->offs);
			pids[count++] = p->pid;
		}
	}
	// Clear focus_tty references outside scheduler_lock
	for (stduint i = 0; i < count; i++) {
		ProcessBlock* pb = ProcessBlock::AcquireActiveByPID(pids[i]);
		if (!pb) continue;
		auto focus_tty = pb->focus_tty.Lock();
		if (*focus_tty == nod) *focus_tty = nullptr;
		ProcessBlock::Release(pb);
	}
}

// Clear any pforms slot that still points at a detached Form.
// Same rationale as CleanupTTYReferences: no Mutex acquisition under spinlock.
static void CleanupFormReferences(::uni::Witch::Form* pfrm) {
	if (!pfrm) return;
	constexpr stduint MAX_PROCS = 128;
	stduint pids[MAX_PROCS];
	stduint count = 0;
	{
		extern Spinlock scheduler_lock;
		SpinlockLocal guard(&scheduler_lock);
		for (auto pnod = Taskman::chain.Root(); pnod && count < MAX_PROCS; pnod = pnod->next) {
			auto p = cast<ProcessBlock*>(pnod->offs);
			pids[count++] = p->pid;
		}
	}
	for (stduint i = 0; i < count; i++) {
		ProcessBlock* pb = ProcessBlock::AcquireActiveByPID(pids[i]);
		if (!pb) continue;
		auto pforms = pb->pforms.Lock();
		for (stduint j = 0; j < pforms->Count(); j++) {
			if ((*pforms)[j] == pfrm) {
				(*pforms)[j] = nullptr;
			}
		}
		ProcessBlock::Release(pb);
	}
}

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

// Destroy one detached vconsole/vtty node.
static void DestroyVconsoleNode(Dnode* nod) {
	if (!nod) return;
	extern Mutex console_waiters_mutex;
	MutexLocal guard(&console_waiters_mutex);
	auto pcon = (uni::VideoConsole2*)nod->offs;
	if (pcon) {
		pcon->Stop();
		delete pcon;
		nod->offs = nullptr;
	}
	extern Dchain vttys;
	vttys.Remove(nod);
}

// Resolve a Console_t base pointer back to its vtty node.
static Dnode* FindVconsoleNodeByConsole(Console_t* con) {
	if (!con) return nullptr;
	extern Dchain vttys;
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

// DetachForm - runs only in Graphic thread, no lock needed.
// Detaches one Form subtree from GUI roots, clears graf input references.
Rectangle Consman::DetachForm(::uni::Witch::Form* pfrm, SheetTrait* exact_sheet) {
	if (!pfrm) return Rectangle{};

	Rectangle area = pfrm->sheet_area;

	// Clear runtime input refs so input paths cannot repopulate them before the subtree is detached.
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

	global_layman.Lock()->UnregisterTimer(pfrm);
	LayerManager* parent = pfrm->refSheetParent();
	if (parent) parent->Remove(pfrm);
	global_layman.Lock()->Remove(pfrm);
	pfrm->refSheetParent() = nullptr;
	DetachLayerChildren(static_cast<LayerManager*>(pfrm));
	// Also detach Form's sheet_node.subf chain (close_btn, title_bar, client_area)
	for (auto child = pfrm->refSheetNode().subf; child; child = child->next) {
		if (!child->offs) continue;
		auto sheet = cast<SheetTrait*>(child->offs);
		sheet->refSheetParent() = nullptr;
		global_layman.Lock()->UnregisterTimer(sheet);
	}
	return area;
}

// SwitchForm - runs only in Graphic thread, no lock needed.
void Consman::SwitchForm(SheetTrait* pfrm) {
	if (Consman::last_click_sheet && Consman::last_click_sheet != pfrm) {
		Consman::last_click_sheet->onrupt(SheetEvent::onLeave, Point(0, 0), 1);
	}
	if (pfrm) {
		pfrm->onrupt(SheetEvent::onClick, Point(0, 0), 0);
	}
	Consman::last_click_sheet = pfrm;

	Nnode* target = &pfrm->refSheetNode();
	{
		auto layman = global_layman.Lock();
		if (layman->subf && target != layman->subf) {
			// Remove from the tail where Append put it
			if (target->left) target->left->next = target->next;
			if (layman->subl == target) layman->subl = target->left;
			// Insert after the top layer (cursor)
			Nnode* top = layman->subf;
			Nnode* sec = top->next;
			target->left = top;
			target->next = sec;
			top->next = target;
			if (sec) sec->left = target;
			else layman->subl = target;// target is now the new tail
		}
	}
	SafeLaymanUpdate(pfrm, Rectangle(Point(0, 0), pfrm->sheet_area.getSize()));
}

// ---- ---- GUI message handlers (Graphic thread only) ---- ----

struct FMT_ConsoleMsg_FNEW {
	stduint pform_id;// in pforms
	Rectangle* usrp_rect;
};

static stdsint GraphicMsg_FNEW(const FMT_ConsoleMsg_FNEW* data, ProcessBlock* pb) {
	Rectangle rect;
	auto mmcp_len = MccaMemCopyP(
		&rect, NULL, true,
		data->usrp_rect, pb, false,
		sizeof(rect));
	if (mmcp_len != sizeof(rect)) {
		plogerro("GraphicMsg_FNEW: invalid data length (%u)", mmcp_len);
		return -1;
	}
	ploginfo("FNEW: new form (%u,%u)", rect.width, rect.height);
	stdsint slot_idx = ProcFormsFindEmptyOrAppend(pb);
	if (slot_idx == -1) return -1;

	auto pfrm = new ::uni::Witch::Form();
	if (!pfrm) return -1;
	pfrm->Title = "New Form";

	Color* sheet_buffer = new Color[rect.getArea()];
	if (!sheet_buffer) {
		delete pfrm;
		return -1;
	}
	pfrm->setSheet(*global_layman.Lock(), rect, sheet_buffer);

	IC.enInterrupt(false);
	global_layman.Lock()->Append(pfrm);
	Consman::SwitchForm(pfrm);
	IC.enInterrupt(true);

	ProcFormsSet(pb, slot_idx, pfrm);
	pfrm->usrp_owner = pb;

	return slot_idx;
}

static void _CleanSingleForm(ProcessBlock* pb, stduint pform_id);

static stdsint GraphicMsg_FDEL(stduint pform_id, ProcessBlock* pb) {
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

static void DestroyDetachedForm(::uni::Witch::Form* pfrm) {
	if (!pfrm) return;

	Rectangle area = pfrm->sheet_area;
	area = Consman::DetachForm(pfrm);
	// Delete VideoConsole2 children from client_area.subf
	LayerManager& client = pfrm->getClientSheet();
	while (client.subf) {
		SheetTrait* child_sheet = cast<SheetTrait*>(client.subf->offs);
		client.Remove(child_sheet);
		if (child_sheet) {
			child_sheet->refSheetParent() = nullptr;
			global_layman.Lock()->UnregisterTimer(child_sheet);
			Console_t* con_base = static_cast<Console_t*>(static_cast<uni::VideoConsole2*>(child_sheet));
			CleanupAndDestroyVconsoleByConsole(con_base);
		}
	}
	DetachLayerChildren(&pfrm->getClientSheet());
	pfrm->refSheetNode().subf = nullptr;
	SafeLaymanUpdate(nullptr, area);

	// Nullify any pforms slot still pointing at this detached Form before delete.
	CleanupFormReferences(pfrm);
	if (pfrm->sheet_buffer) {
		delete[] pfrm->sheet_buffer;
		pfrm->sheet_buffer = nullptr;
	}
	delete pfrm;
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
	ProcFormsSet(pb, pform_id, nullptr);
	DestroyDetachedForm(static_cast<::uni::Witch::Form*>(pfrm));
}

void Global_CleanProcessForms(ProcessBlock* pb) {
	if (!pb) return;
	for (stduint pform_id = 0, count = ProcFormsCount(pb); pform_id < count; pform_id++) {
		_CleanSingleForm(pb, pform_id);
	}
}

static bool GuiCleanupJobContainsParent(GuiCleanupJob* job, LayerManager* parent) {
	if (!job || !parent) return false;
	for0(i, job->forms.Count()) {
		if ((LayerManager*)job->forms[i] == parent) return true;
	}
	return false;
}

static void RunGuiCleanupFormRecursive(GuiCleanupJob* job, ::uni::Witch::Form* pfrm) {
	if (!job || !pfrm) return;
	for0(i, job->forms.Count()) {
		auto child_form = job->forms[i];
		if (child_form && child_form->refSheetParent() == (LayerManager*)pfrm) {
			job->forms[i] = nullptr;
			RunGuiCleanupFormRecursive(job, static_cast<::uni::Witch::Form*>(child_form));
		}
	}
	DestroyDetachedForm(pfrm);
}

static void RunGuiCleanupJob(GuiCleanupJob* job) {
	if (!job) return;
	for0(i, job->forms.Count()) {
		auto form = job->forms[i];
		if (!form) continue;
		auto parent = form->refSheetParent();
		if (parent && GuiCleanupJobContainsParent(job, parent)) {
			continue;
		}
		job->forms[i] = nullptr;
		RunGuiCleanupFormRecursive(job, static_cast<::uni::Witch::Form*>(form));
	}
	delete job;
}

void QueueGuiCleanupForProcess(ProcessBlock* pb) {
	if (!pb) return;
	auto job = new GuiCleanupJob();
	if (!job) {
		plogerro("QueueGuiCleanupForProcess: alloc job failed for pid=%u", pb->pid);
		return;
	}
	job->pid = pb->pid;
	{
		auto pforms = pb->pforms.Lock();
		for0(i, pforms->Count()) {
			auto form = (*pforms)[i];
			if (form) {
				job->forms.Append(form);
				(*pforms)[i] = nullptr;
			}
		}
		pforms->Clear();
	}
	if (!job->forms.Count()) {
		delete job;
		return;
	}
	if (!pending_gui_cleanup_jobs.Lock()->Enqueue(job)) {
		plogerro("QueueGuiCleanupForProcess: enqueue job failed for pid=%u", job->pid);
		delete job;
	}
}

static stdsint GraphicMsg_FBID(const FMT_ConsoleMsg_FBID* data, ProcessBlock* pb) {
	::uni::Witch::Form* pf = static_cast<::uni::Witch::Form*>(ProcFormsGet(pb, data->pform_id));
	if (!pf) return -1;

	pf->usrp_buffer = data->usrp_buffer;
	pf->usrp_owner = pb;

	return 0;
}

static stdsint GraphicMsg_FUPD(const FMT_ConsoleMsg_FUPD* data, ProcessBlock* pb) {
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
		if (MccaMemCopyP(
			&dirty_rect, NULL, true,
			data->usrp_rect, pb, false,
			sizeof(dirty_rect)) == sizeof(dirty_rect)) {
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
		MccaMemCopyP(
			dst, NULL, true,
			src, owner, false,
			dirty_rect.width * sizeof(Color));
	}

	Rectangle update_rect(
		Point(client_area.x + dirty_rect.x, client_area.y + dirty_rect.y),
		Size2(dirty_rect.width, dirty_rect.height)
	);
	SafeLaymanUpdate(pfrm, update_rect);
	return 0;
}

static stdsint GraphicMsg_FMSG(const FMT_ConsoleMsg_FMSG* data, ProcessBlock* pb, stduint sig_src) {
	SheetTrait* pfrm = ProcFormsGet(pb, data->pform_id);
	if (!pfrm) return -1;

	auto pf = static_cast<::uni::Witch::Form*>(pfrm);
	if (pf->msg_queue.Count()) {
		SheetMessage msg;
		pf->msg_queue.Dequeue(msg);
		MccaMemCopyP(
			data->message, pb, false,
			&msg, NULL, true,
			sizeof(msg));
		return 1; // Message fetched
	}

	if (data->if_blocked) {
		// Store blocking request to be handled later by Graphic thread
		MutexLocal waiters_guard(&console_waiters_mutex);
		blocked_form_msgs.Append({ sig_src, data->pform_id, data->message });
		RefreshConsoleBlockedStateUnlocked();
		return -2; // Signal that reply is deferred
	}

	return 0; // No message available
}

static stdsint GraphicMsg_FDRW(const FMT_ConsoleMsg_FDRW* data, ProcessBlock* pb) {
	SheetTrait* pfrm = ProcFormsGet(pb, data->pform_id);
	if (!pfrm) return -1;
	switch (data->shape_type) {
	case FMT_ConsoleMsg_FDRW::Shape::Point: {
		FMT_ConsoleMsg_FDRW::ShapeInfo::ColorPoint cp;
		if (MccaMemCopyP(
			&cp, NULL, true,
			data->usr_shape_info.cpoint, pb, false,
			sizeof(cp)) != sizeof(cp)) return -1;
		if (!pfrm->sheet_buffer) return -1;
		pfrm->sheet_buffer[cp.po.y * pfrm->sheet_area.width + cp.po.x] = cp.co;
		return 0;
	}
	case FMT_ConsoleMsg_FDRW::Shape::Line: {
		FMT_ConsoleMsg_FDRW::ShapeInfo::ColorLine cl;
		if (MccaMemCopyP(
			&cl, NULL, true,
			data->usr_shape_info.cline, pb, false,
			sizeof(cl)) != sizeof(cl)) return -1;
		if (!pfrm->sheet_buffer) return -1;

		VideoControlInterfaceMARGB8888 vcim(pfrm->sheet_buffer, pfrm->sheet_area.getSize());
		LayerManager lm(&vcim, Rectangle{ Point(0,0), pfrm->sheet_area.getSize() });

		Size2dif off;
		off.x = cl.size.x;
		off.y = cl.size.y;
		lm.DrawLine(cl.disp, off, cl.color);

		stdsint x1 = cl.disp.x, y1 = cl.disp.y;
		stdsint x2 = cl.disp.x + cl.size.x, y2 = cl.disp.y + cl.size.y;
		stdsint rx = (x1 < x2 ? x1 : x2) - 1;
		stdsint ry = (y1 < y2 ? y1 : y2) - 1;
		stdsint rw = (x1 < x2 ? x2 - x1 : x1 - x2) + 3;
		stdsint rh = (y1 < y2 ? y2 - y1 : y1 - y2) + 3;

		SafeLaymanUpdate(pfrm, Rectangle(Point(rx, ry), Size2(rw, rh)));
		return 0;
	}
	case FMT_ConsoleMsg_FDRW::Shape::Rect: {
		Rectangle rect;
		if (MccaMemCopyP(
			&rect, NULL, true,
			data->usr_shape_info.crect, pb, false,
			sizeof(rect)) != sizeof(rect)) return -1;
		if (!pfrm->sheet_buffer) return -1;
		VideoControlInterfaceMARGB8888 vcim(pfrm->sheet_buffer, pfrm->sheet_area.getSize());
		vcim.DrawRectangle(rect);
		SafeLaymanUpdate(pfrm, rect);
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

static stdsint GraphicMsg_FCHR(const FMT_ConsoleMsg_FCHR* data, ProcessBlock* pb) {
	SheetTrait* pfrm = ProcFormsGet(pb, data->pform_id);
	if (!pfrm) return -1;

	Point vertex;
	if (MccaMemCopyP(
		&vertex, NULL, true,
		data->usrp_vertex, pb, false,
		sizeof(vertex)) != sizeof(vertex)) return -1;
	String buf(String::Charset::UTF8, 256);
	stduint len = StrCopyP(buf.reflect(), kernel_paging, data->usrp_str, pb->paging, 256);
	buf.Refresh();

	// [Security Fix]: Validate drawing bounds to prevent heap corruption
	if ((vertex.x + (stdsint)buf.getByteCount() * _TEMP 8) > pfrm->sheet_area.width ||
		(vertex.y + 16) > pfrm->sheet_area.height) {
		return -1;
	}

	uni::DrawString_16(*pfrm, vertex, String(buf), data->color);

	Rectangle dirty_rect(vertex, Size2(buf.getByteCount() * 8, 16));
	SafeLaymanUpdate(pfrm, dirty_rect);

	return 0;
}

static stdsint GraphicMsg_FTIM(stduint pform_id, stduint ms, ProcessBlock* pb) {
	SheetTrait* pfrm = ProcFormsGet(pb, pform_id);
	if (!pfrm) return -1;

	stduint period = ms * CONFIG_SysTickFreq / 1000;
	if (ms && !period) period = 1;

	global_layman.Lock()->UnregisterTimer(pfrm);
	if (period) {
		global_layman.Lock()->RegisterTimer(pfrm);
		pfrm->timer_timeout_period = period;
		pfrm->timer_timeout_tick = tick + period;
		ploginfo("Timer set for form %u: period %u ticks", pform_id, period);
	}
	else {
		pfrm->timer_timeout_period = 0;
	}

	return 0;
}

// CreateVconsole - runs only in Graphic thread, no lock needed.
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
	pcon->setFontEngine(&gui_font_engine);
	#endif

	ret.pcon = pcon;
	auto text_buf = new BufferChar[pcon->getCols() * pcon->getRows()];
	auto line_buf = new Color[pcon->getLineBufferSize()];
	if (!text_buf || !line_buf) {
		delete[] text_buf;
		delete[] line_buf;
		delete pcon;
		plogerro("?");
		return ret;
	}
	pcon->setBuffers(nullptr, text_buf, line_buf);

	auto pform = new ::uni::Witch::Form();
	if (!pform) {
		delete pcon;
		plogerro("?");
		return ret;
	}
	ret.pform = pform;
	pform->Title = title;
	pcon->InitializeSheet(*pform, pcon->window.getVertex(), pcon->window.getSize());
	pcon->Clear();
	pform->AppendControl(pcon);
	Color* form_buf = new Color[rect.getArea()];
	pform->setSheet(*global_layman.Lock(), rect, form_buf);
	pform->setFocus(pcon);
	// ploginfo("CreateVconsole: form ready");

	global_layman.Lock()->Append(pform);
	Consman::SwitchForm(pform);
	// ploginfo("CreateVconsole: attached to layman");

	pcon->Start();

	extern Dchain vttys;
	Dnode* pnode = VTTY_Append(pcon);
	ret.tty_node = pnode;
	ret.tty_no = pnode ? vttys.Locate((pureptr_t)pcon, false) : 0;
	// ploginfo("CreateVconsole: done tty_no=%u", ret.tty_no);
	return ret;
}

// RemoveVconsole - runs only in Graphic thread, no lock needed.
void Consman::RemoveVconsole(Dnode* nod) {
	if (!nod) return;
	auto pcon = (uni::VideoConsole2*)nod->offs;
	auto pclient = pcon ? pcon->refSheetParent() : nullptr;
	auto pfrm = pclient ? static_cast<uni::Witch::Form*>(pclient->refSheetParent()) : nullptr;
	Rectangle area = pfrm ? pfrm->sheet_area : (pclient ? pclient->sheet_area : Rectangle{});

	if (pcon) {
		global_layman.Lock()->UnregisterTimer(pcon);
	}
	if (pclient) {
		pclient->Remove(pcon);
	}
	if (pfrm) {
		area = Consman::DetachForm(pfrm, pcon);
		DetachLayerChildren(&static_cast<uni::Witch::Form*>(pfrm)->getClientSheet());
		pfrm->refSheetNode().subf = nullptr;
	}

	if (area.width || area.height) {
		SafeLaymanUpdate(nullptr, area);
	}

	// Phase 2: clean process ownership and blocked-waiter bookkeeping
	CleanupFormReferences(pfrm);
	CleanupTTYReferences(nod);

	// Phase 3: destroy detached objects
	DestroyVconsoleNode(nod);
	if (pfrm) {
		if (pfrm->sheet_buffer) {
			delete[] pfrm->sheet_buffer;
			pfrm->sheet_buffer = nullptr;
		}
		delete pfrm;
	}
}

#endif // _GUI_ENABLE

// ---- ---- Graphic Thread Main Loop ---- ----

void serv_graf_loop() {
	SysMessage msg;// Inner Module Message System
	#if _GUI_ENABLE == 0
	if (auto th = Taskman::CurrentTB()) {
		th->Block(ThreadBlock::BlockReason::BR_Waiting);
	}
	while (true) {
		HALT(); // Sleep the CPU core until the next interrupt
		Taskman::Schedule(true);// yield
	}
	#else
	global_layman.Lock()->lazy_update = _GUI_DOUBLE_BUFFER;// Only enable lazy mode if double buffering is enabled
	
	#if (_MCCA == 0x8632) || (_MCCA == 0x8664)
	while (true) {
		GuiCleanupJob* cleanup_job = nullptr;
		if (PopGuiCleanupJob(cleanup_job)) {
			RunGuiCleanupJob(cleanup_job);
			continue;
		}
		// Priority 1: check internal interrupt queue (non-blocking)
		bool has_int_msg = false;
		{
			auto msg_queue = message_queue_conv.Lock();
			has_int_msg = msg_queue->Count() > 0;
			if (has_int_msg) msg_queue->Dequeue(msg);
		}

		if (has_int_msg) {
			// Handle interrupt messages - no lock needed, Graphic thread is sole GUI owner
			switch (msg.type) {
			case SysMessage::RUPT_TIMER:
				if (Consman::ento_gui) {
					global_layman.Lock()->CheckTimers(tick);
				}
				has_pending_timer = false;
				Consman::WakeBlockedWaiters();
				break;
			case SysMessage::RUPT_MOUSE:
				if (Consman::ento_gui) {
					hand_mouse(msg.args.mou_event);
				}
				Consman::WakeBlockedWaiters();
				break;
			case SysMessage::RUPT_KBD:
				if (Consman::ento_gui) {
					#if _MCCA == 0x8664
					// USB keyboard: sysmsg_kbd handles modifiers, LEDs, hotkeys,
					// and forwards to last_click_sheet->onrupt — all in Graphic thread.
					sysmsg_kbd(msg.args.kbd_event);
					#else
					// PS/2 keyboard: modifiers/LEDs/hotkeys already handled in
					// interrupt context; just forward to the focused sheet.
					auto& kbd_event = msg.args.kbd_event;
					if (Consman::last_click_sheet) {
						Consman::last_click_sheet->onrupt(SheetEvent::onKeybd, Point(0, 0), &kbd_event);
					}
					#endif
				}
				Consman::WakeBlockedWaiters();
				break;
			case SysMessage::RUPT_FLUSH:
				// Asynchronous rendering: Compose and then Flush to screen
				if (Consman::ento_gui) {
					uni::Rectangle flush_rect;
					Color* buf = nullptr;
					{
						auto layman = global_layman.Lock();
						if (layman->pvci && layman->sheet_buffer) {
							if (msg.args.rect.w > 0 && msg.args.rect.h > 0) {
								layman->UpdateForce(nullptr, msg.args.rect.toRectangle());
								flush_rect = msg.args.rect.toRectangle();
								buf = layman->sheet_buffer;
							}
						}
					}
					// Flush to hardware outside the lock to prevent MMIO livelocks
					if (buf && Consman::real_pvci) {
						Consman::real_pvci->DrawPoints(flush_rect, buf);
					}
				}
				break;
			case SysMessage::RUPT_NEW_TERM:
			{
				ProcessBlock* shell_p = Taskman::Create((void*)&serv_shell_process, RING_M);
				if (shell_p) {
					ploginfo("Create new shell-form: pid%u", shell_p->pid);
				}
				break;
			}
			default:
				plogerro("%s Unknown int message type: %d", __FUNCIDEN__, msg.type);
				break;
			}
			// After handling interrupts, check blocked form message requests
			// since new events may have queued messages for blocked forms.
			#if _GUI_ENABLE
			{
				MutexLocal waiters_guard(&console_waiters_mutex);
				for (stduint i = 0; i < blocked_form_msgs.Count(); i++) {
					auto& b = blocked_form_msgs[i];
					ProcessBlock* pb = ProcessBlock::AcquireActive(b.sig_src);
					if (!pb) {
						blocked_form_msgs.Remove(i--);
						continue;
					}
					SheetTrait* pfrm = ProcFormsGet(pb, b.pform_id);
					if (!pfrm) {
						ProcessBlock::Release(pb);
						blocked_form_msgs.Remove(i--);
						continue;
					}
					auto pf = static_cast<::uni::Witch::Form*>(pfrm);
					if (pf->msg_queue.Count()) {
						SheetMessage smsg;
						pf->msg_queue.Dequeue(smsg);
						MccaMemCopyP(
							b.usr_msg_ptr, pb, false,
							&smsg, NULL, true,
							sizeof(smsg));

						stduint val = 1;
						stduint target_sig_src = b.sig_src;
						blocked_form_msgs.Remove(i--);
						syssend_async(target_sig_src, &val, byteof(val));
					}
					ProcessBlock::Release(pb);
				}
				RefreshConsoleBlockedStateUnlocked();
			}
			#endif
			continue;// Prioritize interrupt messages
		}

		// Priority 2: try non-blocking IPC receive
		// Use TMSG to check for pending messages, then sysrecv only if available
		volatile stduint sig_type = 0, sig_src;
		volatile stduint to_args[4];
		if (!syscall(syscall_t::TMSG)) {
			// No IPC message pending, sleep briefly then re-check interrupts
			syscall(syscall_t::REST, 1, 2);
			continue;
		}
		sysrecv(ANYPROC, (void*)to_args, byteof(to_args), (usize*)&sig_type, (usize*)&sig_src);
		ProcessBlock* safe_pb = ProcessBlock::Acquire(sig_src);
		if (!safe_pb) continue;

		#if _GUI_ENABLE
		stduint ret = 0;
		switch ((GraphicMsg)sig_type) {
		case GraphicMsg::FNEW:
			ret = GraphicMsg_FNEW((FMT_ConsoleMsg_FNEW*)to_args, safe_pb);
			syssend_async(sig_src, (void*)&ret, sizeof(ret));
			break;
		case GraphicMsg::FDEL:
		{
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
				ret = GraphicMsg_FDEL(to_args[0], pb_target);
				if (need_release) {
					ProcessBlock::Release(pb_target);
				}
			}
			else {
				ret = (stduint)-1;
			}
		}
		syssend_async(sig_src, (void*)&ret, sizeof(ret));
		break;
		case GraphicMsg::FBID:
			ret = GraphicMsg_FBID((FMT_ConsoleMsg_FBID*)to_args, safe_pb);
			syssend_async(sig_src, (void*)&ret, sizeof(ret));
			break;
		case GraphicMsg::FUPD:
			ret = GraphicMsg_FUPD((FMT_ConsoleMsg_FUPD*)to_args, safe_pb);
			syssend_async(sig_src, (void*)&ret, sizeof(ret));
			break;
		case GraphicMsg::FMSG:
			ret = GraphicMsg_FMSG((FMT_ConsoleMsg_FMSG*)to_args, safe_pb, sig_src);
			if ((stdsint)ret != -2) {
				syssend_async(sig_src, (void*)&ret, sizeof(ret));
			}
			break;
		case GraphicMsg::FDRW:
			ret = GraphicMsg_FDRW((FMT_ConsoleMsg_FDRW*)to_args, safe_pb);
			syssend_async(sig_src, (void*)&ret, sizeof(ret));
			break;
		case GraphicMsg::FCHR:
			ret = GraphicMsg_FCHR((FMT_ConsoleMsg_FCHR*)to_args, safe_pb);
			syssend_async(sig_src, (void*)&ret, sizeof(ret));
			break;
		case GraphicMsg::FTIM:
			ret = GraphicMsg_FTIM(to_args[0], to_args[1], safe_pb);
			syssend_async(sig_src, (void*)&ret, sizeof(ret));
			break;
		case GraphicMsg::FCLEANPROC:
		{
			auto data = (FMT_ConsoleMsg_FCLEANPROC*)to_args;
			ProcessBlock* target_pb = (ProcessBlock*)data->process_block;
			ret = target_pb ? 0 : (stduint)-1;
			if (target_pb) {
				QueueGuiCleanupForProcess(target_pb);
				ProcessBlock::Release(target_pb);
			}
			break;
		}
		case GraphicMsg::VCON_CREATE:
		{
			// to_args: [x, y, w, h] - rectangle for the new vconsole
			Rectangle rect(Point(to_args[0], to_args[1]), Size2(to_args[2], to_args[3]));
			auto result = Consman::CreateVconsole(rect, "Terminal");
			syssend_async(sig_src, &result, sizeof(result));
			break;
		}
		case GraphicMsg::VCON_REMOVE:
		{
			// to_args[0] = Dnode* cast to stduint
			Dnode* nod = (Dnode*)to_args[0];
			Consman::RemoveVconsole(nod);
			ret = 0;
			syssend_async(sig_src, (void*)&ret, sizeof(ret));
			break;
		}
		default:
			plogerro("%s Unknown GraphicMsg type: %d", __FUNCIDEN__, sig_type);
			break;
		}
		#endif // _GUI_ENABLE

		ProcessBlock::Release(safe_pb);

		// Handle blocked form message requests
		#if _GUI_ENABLE
		{
			MutexLocal waiters_guard(&console_waiters_mutex);
			for (stduint i = 0; i < blocked_form_msgs.Count(); i++) {
				auto& b = blocked_form_msgs[i];
				ProcessBlock* pb = ProcessBlock::AcquireActive(b.sig_src);
				if (!pb) {
					blocked_form_msgs.Remove(i--);
					continue;
				}
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
					MccaMemCopyP(
						b.usr_msg_ptr, pb, false,
						&msg, NULL, true,
						sizeof(msg));

					stduint val = 1;
					stduint target_sig_src = b.sig_src;
					blocked_form_msgs.Remove(i--);
					syssend_async(target_sig_src, &val, byteof(val));
				}
				ProcessBlock::Release(pb);
			}
			RefreshConsoleBlockedStateUnlocked();
		}
		#endif
	}
	#else
	loop;
	#endif

	#endif
}

// ---- ---- ---- ---- . ---- ---- ---- ----

// GloScreen
#if 0// GloScreenRGB888

extern ModeInfoBlock* video_info;

uint8& GloScreenRGB888::Locate(const Point& disp) const {
	return *((uint8*)(video_info->PhysBasePtr) +
		(disp.x * video_info->BitsPerPixel >> 3) +
		disp.y * video_info->BytesPerScanLine);// without boundary check
}
void GloScreenRGB888::SetCursor(const Point& disp) const { _TODO; }// MAYBE unused
Point GloScreenRGB888::GetCursor() const { _TODO return {0, 0}; }// MAYBE unused
void GloScreenRGB888::DrawPoint(const Point& disp, Color color) const {
	uint8* p = &Locate(disp);
	*p++ = color.b;
	*p++ = color.g;
	*p++ = color.r;
}
void GloScreenRGB888::DrawRectangle(const Rectangle& rect) const {// RGB 24bpp only
	uint8* p = &Locate(rect.getVertex());
	uint8 comm[3 * 4] = {
		rect.color.b, rect.color.g, rect.color.r,
		rect.color.b, rect.color.g, rect.color.r,
		rect.color.b, rect.color.g, rect.color.r,
		rect.color.b, rect.color.g, rect.color.r,
	};
	uint32 (*com)[3] = (uint32(*)[3])comm;
	for0(y, rect.height) {
		union {
			uint8* pp8;
			uint32* pp32;
		};
		pp8 = p;
		for0r(x, rect.width) {
			if (!(_IMM(pp8) & 0b11) && x >= 12) {
				pp32[0] = treat<uint32>(&comm[4 * 0]);
				pp32[1] = treat<uint32>(&comm[4 * 1]);
				pp32[2] = treat<uint32>(&comm[4 * 2]);
				x -= 3 - 1;
				pp8 += 12;
				continue;
			}
			*pp8++ = rect.color.b;
			*pp8++ = rect.color.g;
			*pp8++ = rect.color.r;
		}
		p += video_info->BytesPerScanLine;
	}
}
void GloScreenRGB888::DrawFont(const Point& disp, const DisplayFont& font, const String& str) const {
	_TODO;
}
Color GloScreenRGB888::GetColor(Point p) const {
	return cast<Color>(Locate(p));
}

#elif (_MCCA & 0xFF00) == 0x8600


inline static stduint VCI_LimitX() { return global_layman.Lock()->window.width; }
uint32& GloScreenARGB8888::Locate(const Point& disp) const {
	return *((uint32*)(global_layman.Lock()->video_memory) + disp.x + disp.y * VCI_LimitX());
}
uint32& GloScreenABGR8888::Locate(const Point& disp) const {
	return *((uint32*)(global_layman.Lock()->video_memory) + disp.x + disp.y * VCI_LimitX());
}


void GloScreenARGB8888::SetCursor(const Point& disp) const { _TODO; }// MAYBE unused
Point GloScreenARGB8888::GetCursor() const { _TODO return {0, 0}; }// MAYBE unused
void GloScreenARGB8888::DrawPoint(const Point& disp, Color color) const {
	Locate(disp) = cast<uint32>(color);
}
void GloScreenARGB8888::DrawRectangle(const Rectangle& rect) const {
	uint32* p = &Locate(rect.getVertex());
	for0(y, rect.height) {
		for0(x, rect.width) p[x] = rect.color.val;// cast<uint32>(rect.color);
		p += VCI_LimitX();
	}
}
void GloScreenARGB8888::DrawFont(const Point& disp, const DisplayFont& font, const String& str) const {
	_TODO;
}
Color GloScreenARGB8888::GetColor(Point p) const {
	return cast<Color>(Locate(p));
}

#if defined(_MCCA) && ((_MCCA & 0xFF00) == 0x8600)
__attribute__((target("general-regs-only")))
#endif
void GloScreenARGB8888::DrawPoints(const Rectangle& rect, const Color* base) const {
	uint32* p = &Locate(rect.getVertex());
	const Color* pbase = base + rect.y * VCI_LimitX() + rect.x;
	for0(y, rect.height) {
		for0(x, rect.width) p[x] = pbase[x].val;
		p += VCI_LimitX();
		pbase += VCI_LimitX();
	}
}



void GloScreenABGR8888::SetCursor(const Point& disp) const { _TODO; }// MAYBE unused
Point GloScreenABGR8888::GetCursor() const { _TODO return { 0, 0 }; }// MAYBE unused
void GloScreenABGR8888::DrawPoint(const Point& disp, Color color) const {
	uint32 val = 
		(_IMM(color.r) << 0) |
		(_IMM(color.g) << 8) |
		(_IMM(color.b) << 16) |
		(_IMM(color.a) << 24);
	Locate(disp) = val;
}
void GloScreenABGR8888::DrawRectangle(const Rectangle& rect) const {
	uint32 val;
	val = (_IMM(rect.color.r) << 0)
		| (_IMM(rect.color.g) << 8)
		| (_IMM(rect.color.b) << 16)
		| (_IMM(rect.color.a) << 24);
	uint32* p = &Locate(rect.getVertex());
	for0(y, rect.height) {
		for0(x, rect.width) p[x] = val;
		//cast<byte*>(p) += config.pixels_per_scan_line;
		p += VCI_LimitX();
	}
}
void GloScreenABGR8888::DrawFont(const Point& disp, const DisplayFont& font, const String& str) const {
	_TODO;
}
Color GloScreenABGR8888::GetColor(Point p) const {
	Color color;
	uint32 val = Locate(p);
	color.r = (val >> 0) & 0xFF;
	color.g = (val >> 8) & 0xFF;
	color.b = (val >> 16) & 0xFF;
	color.a = (val >> 24) & 0xFF;
	return color;
}
void GloScreenABGR8888::DrawPoints(const Rectangle& rect, const Color* base) const {
	
}


#endif


// Double Buffer
#if (_MCCA & 0xFF00) == 0x8600
void Consman::enable_2buffer() {
	// Double Buffer
	//{} put off
	#if _GUI_DOUBLE_BUFFER
	// Pre-calculate size and allocate without lock, since mem.allocate can log to screen and trigger SafeLaymanUpdate!
	stduint vcon0_size = 0;
	{
		auto layman = global_layman.Lock();
		vcon0_size = layman->window.getArea() * sizeof(Color);
	}
	Color* new_buffer = (Color*)mem.allocate(vcon0_size);
	
	if (!new_buffer) {
		plogerro("Failed to allocate back buffer for Layman");
	} else {
		// Clear buffer outside the lock to avoid spinlock timeouts
		for0(i, vcon0_size / sizeof(Color)) {
			new_buffer[i] = Color::Black;
		}
		auto layman = global_layman.Lock();
		layman->sheet_buffer = new_buffer;
		layman->sheet_area = layman->window;
		
		// Redirect global_layman's VCI to point to our back-buffer
		Consman::real_pvci = layman->pvci;
		// Allocate a static VCI for the back-buffer
		static byte _BUF_VCI[sizeof(VideoControlInterfaceMARGB8888)];
		VideoControlInterfaceMARGB8888* back_vci = new (_BUF_VCI) VideoControlInterfaceMARGB8888(layman->sheet_buffer, layman->window.getSize());
		layman->pvci = back_vci;
	}
	Consman::enable_dubuffer = true;
	#endif
}
void RenderFrameFlush() {
	if (Consman::ento_gui) {
		static uint64 last_timer_check = 0;
		if (tick - last_timer_check >= 10) { // Limit to 100ms interval
			if (!has_pending_timer) {
				has_pending_timer = true;
				last_timer_check = tick;
				SysMessage msg;
				msg.type = SysMessage::RUPT_TIMER;
				message_queue_conv.Lock()->Enqueue(msg);
			}
		}
	}
	if (!Consman::enable_dubuffer) return;
	
	bool is_dirty = false;
	{
		auto layman = global_layman.unsafe_ptr(); // Use unsafe_ptr in interrupt handler
		if (Consman::ento_gui && layman->pvci && layman->is_dirty) {
			is_dirty = true;
		}
	}
	if (is_dirty) {
		static uint64 last_flush_time = 0;
		if (tick - last_flush_time >= 5) { // 100 FPS target (10ms)
			SysMessage msg;
			msg.type = SysMessage::RUPT_FLUSH;
			{
				auto layman = global_layman.Lock();
				layman->is_dirty = false;
				stdsint x = layman->dirty_area.x;
				stdsint y = layman->dirty_area.y;
				stdsint w = layman->dirty_area.width;
				stdsint h = layman->dirty_area.height;

				if (x < 0) { w += x; x = 0; }
				if (y < 0) { h += y; y = 0; }

				stdsint max_w = layman->window.width - x;
				stdsint max_h = layman->window.height - y;

				if (w > max_w) w = max_w;
				if (h > max_h) h = max_h;

				if (w < 0) w = 0;
				if (h < 0) h = 0;

				msg.args.rect.x = x;
				msg.args.rect.y = y;
				msg.args.rect.w = w;
				msg.args.rect.h = h;
				layman->dirty_area = {};
			}
			message_queue_conv.Lock()->Enqueue(msg);

			last_flush_time = tick;
		}
	}
}
#endif
