// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <cpp/Witch/Control/Control-Label.hpp>
#include <cpp/Witch/Control/Control-TextBox.hpp>

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
void RefreshConsoleBlockedStateUnlocked();
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


Vector<BlockedFormMsg> blocked_form_msgs;
Mutex console_waiters_mutex;
static volatile byte console_has_blocked_waiters = 0;
static volatile byte console_wake_pending = 0;

void RefreshConsoleBlockedStateUnlocked() {
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

static stdsint ConsoleMsg_RDWR(const FMT_ConsoleMsg_RDWR* data, bool wr_type) {
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
		free(rw_buffer);
		return 0;
	}

	// Hold console_waiters_mutex across validation AND I/O to prevent
	// Graphic thread from destroying the TTY node via FCLEANPROC concurrently.
	MutexLocal vttys_guard(&console_waiters_mutex);
	{
		extern Dchain vttys;
		bool found = false;
		for (auto nod = vttys.Root(); nod; nod = nod->next) {
			if (nod == tty_node) { found = true; break; }
		}
		if (!found) {
			ProcessBlock::Release(pb);
			free(rw_buffer);
			return 0;
		}
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
				Dnode* tty_nod = *focus_tty;
				// Verify the TTY node is still alive before accessing its innput_queue.
				// Graphic thread may destroy it concurrently via FCLEANPROC.
				// (console_waiters_mutex already held by outer scope)
				QueueLimited* q = nullptr;
				if (tty_nod) {
					extern Dchain vttys;
					for (auto nod = vttys.Root(); nod; nod = nod->next) {
						if (nod == tty_nod) { q = VTTY_INNQ(tty_nod); break; }
					}
				}
				if (q && -1 != (ch = q->inn())) {
					stdsint val = ch;
					blocked_vtty_pid.Remove(i--);
					syssend_async(pid, &val, byteof(val));
				}
				ProcessBlock::Release(ppb);
			}
			RefreshConsoleBlockedStateUnlocked();
		}
	}
}

