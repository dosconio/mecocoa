// ASCII g++ TAB4 LF
// Codifiers: @dosconio, @ArinaMgk
// Docutitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <c/task.h>

#if _MCCA == 0x8664 && defined(_UEFI)
extern byte _BUF_xhc[];
#endif


extern uni::Dchain TimerManager;
void _Comment(R0) serv_sysmsg() {
	#if _MCCA == 0x8664 && defined(_UEFI)
	global_layman.Lock()->lazy_update = _GUI_DOUBLE_BUFFER;// Only enable lazy mode if double buffering is enabled
	while (true) {
		IC.enInterrupt(false);
		// auto crt_tick = tick;
		if (!message_queue.Count()) {
			IC.enInterrupt(true);
			HALT();
			continue;
		}
		SysMessage msg;
		message_queue.Dequeue(msg);
		IC.enInterrupt(true);
		auto& xhc = *reinterpret_cast<uni::device::SpaceUSB3::HostController*>(_BUF_xhc);
		switch (msg.type) {
		case SysMessage::RUPT_xHCI:
			if (auto err = xhc.ProcessEvents()) {
				plogerro("Error while ProcessEvent: %s at %s:%d", err.Name(), err.File(), err.Line());
			}
			break;
		case SysMessage::RUPT_TIMER:
			ploginfo("Timer %llu Rupt! tick = %llu, tim = %u", msg.args.timer.iden, msg.args.timer.timeout, TimerManager.Count());
			if (0 && msg.args.timer.iden == 0)
			{
				SysTimer::Append(100, 0);// spinLocked
			}
			break;
		case SysMessage::RUPT_CONSOLE_WAKE:
			Consman::WakeBlockedWaiters();
			break;
		default:
			plogerro("Unknown message type: %d", msg.type);
			break;
		}
	}
	#endif
}

#if 1

#define HARDRUPT 1

void rupt_proc(stduint tid, stduint rupt_no)
{
	auto th = Taskman::LocateThread(tid);
	if (!th) return;
	extern Spinlock comm_lock;
	SpinlockLocal guard(&comm_lock);
	if ((_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_RecvMsg)) &&
		((stduint)th->recv_fo_whom == ANYPROC || (stduint)th->recv_fo_whom == INTRUPT)) {
		// ploginfo("INT-MSG: RUPT-PROC");
		CommMsg tmp_msg = { };
		tmp_msg.type = HARDRUPT;
		MccaMemCopyP(th->unsolved_msg, th->parent_process, &tmp_msg, 0, sizeof(tmp_msg));
		th->wait_rupt_no = nil;
		th->unsolved_msg = NULL;
		th->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
	}
	else {
		th->wait_rupt_no = rupt_no;
	}
}


static bool msg_send_will_deadlock(ThreadBlock* fo_th, ThreadBlock* to_th)
{
	// e.g. A->B->C->A
	KASSERT(to_th != nullptr);
	ThreadBlock* crt = to_th;
	while (true) {
		if (crt->block_reason == ThreadBlock::BlockReason::BR_SendMsg) {
			if (crt->send_to_whom == fo_th) {
				plogerro("deadlock T%u<-...->T%u", fo_th->tid, crt->tid);
				return true;
			}
			if (!crt->send_to_whom) break;
			crt = crt->send_to_whom;
		}
		else break;
	}
	return false;
}

void free_async_msg(pureptr_t ptr) {
	if (ptr) {
		auto* nod = (Dnode*)ptr;
		auto* amsg = (AsyncCommMsg*)nod->offs;
		if (amsg) {
			if (amsg->msg.data.address) {
				delete[] (byte*)amsg->msg.data.address;
			}
			delete amsg;
		}
	}
}

Spinlock comm_lock;
static bool CanCommProcess(ProcessBlock* pb) {
	return pb && pb->state == ProcessBlock::State::Active;
}

static bool CanCommThread(ThreadBlock* th) {
	return th &&
		th->state != ThreadBlock::State::Invalid &&
		th->state != ThreadBlock::State::Exited &&
		th->state != ThreadBlock::State::Hanging &&
		!(_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_Exiting)) &&
		CanCommProcess(th->parent_process);
}

static bool CanUseSyncMsgState(ThreadBlock* th) {
	return CanCommThread(th) && th->unsolved_msg;
}

static void DropQueuedSender(ThreadBlock* to_th, ThreadBlock* prev, ThreadBlock* sender) {
	if (!to_th || !sender) return;
	if (sender == to_th->queue_send_queuehead) {
		to_th->queue_send_queuehead = sender->queue_send_queuenext;
	}
	else if (prev) {
		prev->queue_send_queuenext = sender->queue_send_queuenext;
	}
	sender->send_to_whom = nullptr;
	sender->queue_send_queuenext = nullptr;
	sender->unsolved_msg = nullptr;
	if (sender->state != ThreadBlock::State::Invalid &&
		sender->state != ThreadBlock::State::Exited &&
		sender->state != ThreadBlock::State::Hanging) {
		sender->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
	}
}

int msg_send(ThreadBlock* fo_th, stduint too, _Comment(vaddr) CommMsg* msg, bool is_async)
{
	SpinlockLocal guard(&comm_lock);
	KASSERT(fo_th != nullptr);
	if (!too) return 2;
	ThreadBlock* to_th = Taskman::LocateThread(too);
	if (!to_th) {
		if (auto to_pb = Taskman::Locate(too)) to_th = to_pb->main_thread;
	}
	if (!CanCommThread(fo_th) || !CanCommThread(to_th)) return 1;

	if (!is_async && msg_send_will_deadlock(fo_th, to_th)) {
		plogerro("msg_send_will_deadlock");
		return -1;
	}
	
	ProcessBlock* to = to_th->parent_process;
	ProcessBlock* fo = fo_th->parent_process;

	if ((_IMM(to_th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_RecvMsg)) &&
		(to_th->recv_fo_whom == fo_th || (stduint)to_th->recv_fo_whom == ANYPROC)) {
		// assert to.unsolved_msg && msg
		void* pg_leng = SeekAddress(fo, _IMM(&msg->data.length));
		stduint leng = _IMM(pg_leng) ? *(stduint*)pg_leng : 0;
		auto msg_fo = (CommMsg*)SeekAddress(fo, _IMM(msg));
		void* addr_fo = msg_fo ? (void*)msg_fo->data.address : nullptr;
		auto msg_to = (CommMsg*)SeekAddress(to, _IMM(to_th->unsolved_msg));
		void* addr_to = msg_to ? (void*)msg_to->data.address : nullptr;
		if (msg_to) MIN(leng, msg_to->data.length);
		// if (leng > 500) ploginfo("SEND: T%u->T%u, %u bytes, %p->%p", fo_th->tid, to_th->tid, leng, addr_fo, addr_to);
		if (leng) MccaMemCopyP(addr_to, to, addr_fo, fo, leng);
		if (msg_to && msg_fo) msg_to->type = msg_fo->type;
		if (msg_to) msg_to->src = fo_th->tid;
		to_th->unsolved_msg = NULL;
		to_th->recv_fo_whom = nullptr;
		to_th->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
	}
	else if (is_async) {
		if (to_th->async_messages.Count() >= LIMIT_THREAD_AMSG) {
			return 3;
		}
		void* pg_leng = SeekAddress(fo, _IMM(&msg->data.length));
		stduint leng = _IMM(pg_leng) ? *(stduint*)pg_leng : 0;
		auto msg_fo = (CommMsg*)SeekAddress(fo, _IMM(msg));
		void* addr_fo = msg_fo ? (void*)msg_fo->data.address : nullptr;

		AsyncCommMsg* amsg = new AsyncCommMsg();
		if (!amsg) return 1;
		byte* payload = nullptr;
		if (leng) {
			payload = new byte[leng];
			if (!payload) {
				delete amsg;
				return 1;
			}
			MccaMemCopyP(payload, nullptr, addr_fo, fo, leng);
		}

		amsg->msg.data.address = (stduint)payload;
		amsg->msg.data.length = leng;
		if (msg_fo) {
			amsg->msg.type = msg_fo->type;
		} else {
			amsg->msg.type = 0;
		}
		amsg->msg.src = fo_th->tid;

		to_th->async_messages.Append(amsg);
		if ((_IMM(to_th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_RecvMsg)) &&
			(to_th->recv_fo_whom == fo_th || (stduint)to_th->recv_fo_whom == ANYPROC)) {
			to_th->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
		}
	}
	else {
		fo_th->Block(ThreadBlock::BlockReason::BR_SendMsg);
		fo_th->send_to_whom = to_th;
		if (fo_th->unsolved_msg) plogwarn("T%u, unsolved_msg when send(%u)", fo_th->tid, too);
		fo_th->unsolved_msg = msg;
		// proc sending queue
		if (!to_th->queue_send_queuehead) to_th->queue_send_queuehead = fo_th; else {
			ThreadBlock* crt = to_th->queue_send_queuehead;
			while (crt->queue_send_queuenext) {
				crt = crt->queue_send_queuenext;
				if (!crt) { plogerro("Loss of ThreadBlock since qsend T%u", too); return 1; }
			}
			crt->queue_send_queuenext = fo_th;
		}
		fo_th->queue_send_queuenext = nullptr;// keep this at tail
		// fo_th->ring_coreid = CORE_ID_INVALID;
		guard.~SpinlockLocal();
		#if (_MCCA & 0xFF00) == 0x8600
		Taskman::Schedule(true);
		#endif
	}
	return 0;
}
int msg_recv(ThreadBlock* to_th, stduint foo, _Comment(vaddr) CommMsg* msg)
{
	SpinlockLocal guard(&comm_lock);
	_Comment(Proc - Interrupt) if ((to_th->wait_rupt_no) && (foo == ANYPROC || foo == INTRUPT)) {
		CommMsg tmp_msg = { {0, 0}, HARDRUPT, to_th->wait_rupt_no };
		MccaMemCopyP(msg, to_th->parent_process, &tmp_msg, 0, sizeof(tmp_msg));
		to_th->wait_rupt_no = nil;
		return 0;
	}
	if (!CanCommThread(to_th)) return -1;
	bool determined = false;
	ThreadBlock* prev = nullptr;
	ThreadBlock* fo_th = nullptr;
	if (foo == ANYPROC) {
		while (to_th->queue_send_queuehead && !CanUseSyncMsgState(to_th->queue_send_queuehead)) {
			DropQueuedSender(to_th, nullptr, to_th->queue_send_queuehead);
		}
		if (to_th->queue_send_queuehead) {
			fo_th = to_th->queue_send_queuehead;
			foo = fo_th->tid;
			determined = true;
		}
	}
	else if (foo != INTRUPT) {
		fo_th = Taskman::LocateThread(foo);
		if (!fo_th) {
			if (auto fo_pb = Taskman::Locate(foo)) fo_th = fo_pb->main_thread;
		}
		if (CanUseSyncMsgState(fo_th) &&
			fo_th->block_reason == ThreadBlock::BlockReason::BR_SendMsg &&
			fo_th->send_to_whom == to_th) {
			ThreadBlock* crt = to_th->queue_send_queuehead;
			while (crt) {
				if (crt == fo_th) {
					determined = true; break;
				}
				prev = crt;
				crt = crt->queue_send_queuenext;
			}
		}
	}

	if (determined) {
		if (fo_th == to_th->queue_send_queuehead) {
			to_th->queue_send_queuehead = fo_th->queue_send_queuenext;
			fo_th->queue_send_queuenext = nullptr;
		}
		else {
			if (!prev) { plogerro("!prev in %s", __FUNCIDEN__); return 1; }
			asserv(prev)->queue_send_queuenext = fo_th->queue_send_queuenext;// A->[B]->C => A->C
			fo_th->queue_send_queuenext = nullptr;
		}
		if (!CanUseSyncMsgState(fo_th)) {
			fo_th->send_to_whom = nullptr;
			return -1;
		}
		//
		auto msg_fo = (CommMsg*)SeekAddress(fo_th->parent_process, (stduint)fo_th->unsolved_msg);
		stduint leng0 = msg_fo ? msg_fo->data.length : 0;
		void* addr_fo = msg_fo ? (void*)msg_fo->data.address : nullptr;

		auto msg_to = (CommMsg*)SeekAddress(to_th->parent_process, _IMM(msg));
		stduint leng1 = msg_to ? msg_to->data.length : 0;
		void* addr_to = msg_to ? (void*)msg_to->data.address : nullptr;
		//
		stduint leng = minof(leng0, leng1);
		// if (leng > 500) ploginfo("RECV: %u->%u, %u bytes, %p->%p", fo_th->tid, to_th->tid, leng, addr_fo, addr_to);
		if (leng) MccaMemCopyP(addr_to, to_th->parent_process, addr_fo, fo_th->parent_process, leng);
		if (msg_to && msg_fo) msg_to->type = msg_fo->type;
		if (msg_to) msg_to->src = foo;
		fo_th->unsolved_msg = NULL;
		fo_th->send_to_whom = nullptr;
		fo_th->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
	}
	else {
		Dnode* target_node = nullptr;
		AsyncCommMsg* amsg = nullptr;
		if (foo == ANYPROC) {
			target_node = to_th->async_messages.Root();
		}
		else if (foo != INTRUPT) {
			for (Dnode* nod = to_th->async_messages.Root(); nod; nod = nod->next) {
				auto* candidate = (AsyncCommMsg*)nod->offs;
				if (candidate && candidate->msg.src == foo) {
					target_node = nod;
					break;
				}
			}
		}

		if (target_node) {
			amsg = (AsyncCommMsg*)target_node->offs;
			auto msg_to = (CommMsg*)SeekAddress(to_th->parent_process, _IMM(msg));
			stduint leng1 = msg_to ? msg_to->data.length : 0;
			void* addr_to = msg_to ? (void*)msg_to->data.address : nullptr;

			stduint leng = minof(amsg->msg.data.length, leng1);
			if (leng) {
				MccaMemCopyP(addr_to, to_th->parent_process, (void*)amsg->msg.data.address, nullptr, leng);
			}
			if (msg_to) {
				msg_to->type = amsg->msg.type;
				msg_to->src = amsg->msg.src;
			}
			to_th->async_messages.Remove(target_node);
		}
		else { // block self to wait for msg
			ThreadBlock* fo_th_tgt = nullptr;
			if (foo == ANYPROC || foo == INTRUPT) {
				fo_th_tgt = (ThreadBlock*)foo;
			} else {
				fo_th_tgt = Taskman::LocateThread(foo);
				if (!fo_th_tgt) {
					if (auto fo_pb = Taskman::Locate(foo)) fo_th_tgt = fo_pb->main_thread;
				}
				if (!CanCommThread(fo_th_tgt)) return -1;
			}

			to_th->Block(ThreadBlock::BlockReason::BR_RecvMsg);
			if (to_th->unsolved_msg) plogwarn("T%u, unsolved_msg when recv(%u)", to_th->tid, foo);
			to_th->unsolved_msg = msg;
			to_th->recv_fo_whom = fo_th_tgt;
			// to_th->ring_coreid = CORE_ID_INVALID;
			guard.~SpinlockLocal();
			#if (_MCCA & 0xFF00) == 0x8600
			Taskman::Schedule(true);
			#endif
		}
	}
	return 0;
}


void msg_cleanup_thread(ThreadBlock* th) {
	#if _MCCA == 0x8632
	Queue<ThreadBlock*> wake_list;
	#endif
	SpinlockLocal guard(&comm_lock);
	
	// Case 1: This thread (th) was a target (Receiver), unblock all its senders
	ThreadBlock* sender = th->queue_send_queuehead;
	while (sender) {
		ThreadBlock* s_next = sender->queue_send_queuenext;
		sender->send_to_whom = nullptr;
		sender->queue_send_queuenext = nullptr;
		sender->unsolved_msg = nullptr;
		#if _MCCA == 0x8632
		wake_list.Enqueue(sender);
		#endif
		sender = s_next;
	}
	th->queue_send_queuehead = nullptr;

	// Case 2: This thread (th) was a sender waiting in a target's (target_th) queue
	if (th->send_to_whom) {
		ThreadBlock* target_th = th->send_to_whom;
		if (target_th->queue_send_queuehead == th) {
			// th is the head of the target's queue
			target_th->queue_send_queuehead = th->queue_send_queuenext;
		} else {
			// Find th in the target's queue and unlink it
			ThreadBlock* prev = target_th->queue_send_queuehead;
			while (prev && prev->queue_send_queuenext != th) {
				prev = prev->queue_send_queuenext;
			}
			if (prev) {
				prev->queue_send_queuenext = th->queue_send_queuenext;
			}
		}
		th->send_to_whom = nullptr;
		th->queue_send_queuenext = nullptr;
		th->unsolved_msg = nullptr;
	}
	if (th->block_reason & ThreadBlock::BlockReason::BR_RecvMsg) {
		th->recv_fo_whom = nullptr;
		th->unsolved_msg = nullptr;
	}

	// Case 3: Clean up all pending async messages for this thread
	th->async_messages.Remove(0, th->async_messages.Count());

	#if _MCCA == 0x8632
	guard.~SpinlockLocal();
	while (!wake_list.isEmpty()) {
		ThreadBlock* wake = nullptr;
		wake_list.Dequeue(wake);
		if (wake) {
			wake->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
		}
	}
	#endif
}

#endif
