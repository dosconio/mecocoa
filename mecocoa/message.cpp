// ASCII g++ TAB4 LF
// Codifiers: @dosconio, @ArinaMgk
// Docutitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <c/task.h>

#if _MCCA == 0x8664 && defined(_UEFI)
extern byte _BUF_xhc[];
#endif
#if (_MCCA & 0xFF00) == 0x1000
#include <c/driver/timer.h>
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
	bool do_unblock = false;
	{
		extern Spinlock comm_lock;
		SpinlockLocal guard(&comm_lock);
		if (th->unsolved_msg &&
			((stduint)th->recv_fo_whom == ANYPROC || (stduint)th->recv_fo_whom == INTRUPT)) {
			// ploginfo("INT-MSG: RUPT-PROC");
			CommMsg tmp_msg = { };
			tmp_msg.type = HARDRUPT;
			MccaMemCopyP(
				th->unsolved_msg, th->parent_process, th->unsolved_msg_from_kernel,
				&tmp_msg, 0, true,
				sizeof(tmp_msg)
			);
			th->wait_rupt_no = nil;
			th->unsolved_msg = NULL;
			th->recv_fo_whom = nullptr;
			do_unblock = true;
		}
		else {
			th->wait_rupt_no = rupt_no;
		}
	}
	if (do_unblock) {
		th->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
	}
}


static bool msg_send_will_deadlock(ThreadBlock* fo_th, ThreadBlock* to_th)
{
	// e.g. A->B->C->A
	KASSERT(to_th != nullptr);
	ThreadBlock* crt = to_th;
	while (true) {
		if (crt->send_to_whom && crt->unsolved_msg) {
			if (crt->send_to_whom == fo_th) {
				plogerro("deadlock T%u<-...->T%u", fo_th->tid, crt->tid);
				return true;
			}
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

static ThreadBlock* DropQueuedSender(ThreadBlock* to_th, ThreadBlock* prev, ThreadBlock* sender) {
	if (!to_th || !sender) return nullptr;
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
		return sender;
	}
	return nullptr;
}

struct MsgInfo {
	CommMsg* pmsg;
	void* addr;
	stduint leng;
};

inline static MsgInfo FetchMessage(ProcessBlock *pb, _Comment(vaddr) CommMsg* msg, bool msg_in_kernel) {
	MsgInfo msg_info = {};
	void* pg_leng = SeekAddress(pb, _IMM(&msg->data.length), msg_in_kernel);
	msg_info.leng = _IMM(pg_leng) ? *(stduint*)pg_leng : 0;
	msg_info.pmsg = (CommMsg*)SeekAddress(pb, _IMM(msg), msg_in_kernel);
	msg_info.addr = msg_info.pmsg ? (void*)msg_info.pmsg->data.address : nullptr;
	return msg_info;
}

int msg_send(ThreadBlock* fo_th, stduint too, _Comment(vaddr) CommMsg* msg, bool msg_in_kernel, bool is_async)
{
	KASSERT(fo_th != nullptr);
	if (!too) return 2;
	ThreadBlock* to_th = Taskman::LocateThread(too);
	if (!to_th) {
		if (auto to_pb = Taskman::Locate(too)) to_th = to_pb->main_thread;
	}
	if (!CanCommThread(fo_th) || !CanCommThread(to_th)) return 1;

	ThreadBlock* wake_th = nullptr;
	bool do_block = false;

	{
		SpinlockLocal guard(&comm_lock);
		if (!CanCommThread(fo_th) || !CanCommThread(to_th)) return 1;

		if (!is_async && msg_send_will_deadlock(fo_th, to_th)) {
			plogerro("msg_send_will_deadlock");
			return -1;
		}
		
		ProcessBlock* to = to_th->parent_process;
		ProcessBlock* fo = fo_th->parent_process;

		if (to_th->unsolved_msg &&
			(to_th->recv_fo_whom == fo_th || (stduint)to_th->recv_fo_whom == ANYPROC)) {
			auto [fo_msg, fo_addr, fo_leng] = FetchMessage(fo, msg, msg_in_kernel);
			auto [to_msg, to_addr, to_leng] = FetchMessage(to, to_th->unsolved_msg, to_th->unsolved_msg_from_kernel);
			// if (leng > 500) ploginfo("SEND: T%u->T%u, %u bytes, %p->%p", fo_th->tid, to_th->tid, leng, addr_fo, addr_to);
			if (stduint leng = minof(fo_leng, to_leng)) MccaMemCopyP(
				to_addr, to, to_th->unsolved_msg_from_kernel,
				fo_addr, fo, msg_in_kernel,
				leng
			);
			if (to_msg && fo_msg) to_msg->type = fo_msg->type;
			if (to_msg) to_msg->src = fo_th->tid;
			to_th->unsolved_msg = NULL;
			to_th->recv_fo_whom = nullptr;
			wake_th = to_th;
		}
		else if (is_async) {
			if (to_th->async_messages.Count() >= LIMIT_THREAD_AMSG) {
				plogerro("Too many async messages");
				return 3;
			}
			auto [fo_msg, fo_addr, fo_leng] = FetchMessage(fo, msg, msg_in_kernel);

			AsyncCommMsg* amsg = new AsyncCommMsg();
			if (!amsg) return 1;
			byte* payload = nullptr;
			if (fo_leng) {
				payload = new byte[fo_leng];
				if (!payload) {
					delete amsg;
					return 1;
				}
				MccaMemCopyP(
					payload, nullptr, true,
					fo_addr, fo, msg_in_kernel,
					fo_leng
				);
			}

			amsg->msg.data.address = (stduint)payload;
			amsg->msg.data.length = fo_leng;
			if (fo_msg) {
				amsg->msg.type = fo_msg->type;
			} else {
				amsg->msg.type = 0;
			}
			amsg->msg.src = fo_th->tid;

			to_th->async_messages.Append(amsg);
			if (to_th->unsolved_msg &&
				(to_th->recv_fo_whom == fo_th || (stduint)to_th->recv_fo_whom == ANYPROC)) {
				wake_th = to_th;
			}
		}
		else {
			if (_sigset_raw(&fo_th->pending_signals) & ~_sigset_raw(&fo_th->blocked_signals))
				return -4;
			fo_th->send_to_whom = to_th;
			if (fo_th->unsolved_msg) plogwarn("T%u, unsolved_msg when send(%u)", fo_th->tid, too);
			fo_th->unsolved_msg = msg; fo_th->unsolved_msg_from_kernel = msg_in_kernel;
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
			if (to_th->recv_fo_whom == fo_th || (stduint)to_th->recv_fo_whom == ANYPROC) {
				wake_th = to_th;
			}
			do_block = true;
		}
	}

	if (wake_th) {
		wake_th->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
	}
	if (do_block) {
		fo_th->Block(ThreadBlock::BlockReason::BR_SendMsg);
		bool need_schedule = true;
		{
			SpinlockLocal guard(&comm_lock);
			if (!fo_th->unsolved_msg && !fo_th->send_to_whom) {
				need_schedule = false;
			}
		}
		if (need_schedule) {
			Taskman::Schedule(true);
			if (fo_th->unsolved_msg == (CommMsg*)-1) {
				fo_th->unsolved_msg = nullptr;
				return -4;
			}
		}
		else {
			fo_th->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
		}
		if (!fo_th->unsolved_msg) {
			return 0;
		}
		fo_th->unsolved_msg = nullptr;
		fo_th->send_to_whom = nullptr;
		return -4;
	}
	return 0;
}
int msg_recv(ThreadBlock* to_th, stduint foo, _Comment(vaddr) CommMsg* msg, bool msg_in_kernel)
{
	if (!CanCommThread(to_th)) return -1;
	ThreadBlock* fo_th_lookup = nullptr;
	if (foo != ANYPROC && foo != INTRUPT) {
		fo_th_lookup = Taskman::LocateThread(foo);
		if (!fo_th_lookup) {
			if (auto fo_pb = Taskman::Locate(foo)) fo_th_lookup = fo_pb->main_thread;
		}
		if (!CanCommThread(fo_th_lookup)) return -1;
	}

	ThreadBlock* dropped_wakes[8];
	stduint dropped_count = 0;
	ThreadBlock* wake_fo = nullptr;
	bool do_block = false;
	ThreadBlock* fo_th_tgt = nullptr;

	{
		SpinlockLocal guard(&comm_lock);
		_Comment(Proc - Interrupt) if ((to_th->wait_rupt_no) && (foo == ANYPROC || foo == INTRUPT)) {
			CommMsg tmp_msg = { {0, 0}, HARDRUPT, to_th->wait_rupt_no };
			MccaMemCopyP(
				msg, to_th->parent_process, msg_in_kernel,
				&tmp_msg, 0, true,
				sizeof(tmp_msg)
			);
			to_th->wait_rupt_no = nil;
			return 0;
		}
		if (!CanCommThread(to_th)) return -1;
		bool determined = false;
		ThreadBlock* prev = nullptr;
		ThreadBlock* fo_th = nullptr;
		if (foo == ANYPROC) {
			while (to_th->queue_send_queuehead && !CanUseSyncMsgState(to_th->queue_send_queuehead)) {
				ThreadBlock* dropped = DropQueuedSender(to_th, nullptr, to_th->queue_send_queuehead);
				if (dropped && dropped_count < numsof(dropped_wakes)) {
					dropped_wakes[dropped_count++] = dropped;
				}
			}
			if (to_th->queue_send_queuehead) {
				fo_th = to_th->queue_send_queuehead;
				foo = fo_th->tid;
				determined = true;
			}
		}
		else if (foo != INTRUPT) {
			fo_th = fo_th_lookup;
			if (CanUseSyncMsgState(fo_th) &&
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

			auto [fo_msg, fo_addr, fo_leng] = FetchMessage(fo_th->parent_process, fo_th->unsolved_msg, fo_th->unsolved_msg_from_kernel);
			auto [to_msg, to_addr, to_leng] = FetchMessage(to_th->parent_process, msg, msg_in_kernel);
			//
			// if (leng > 500) ploginfo("RECV: %u->%u, %u bytes, %p->%p", fo_th->tid, to_th->tid, leng, addr_fo, addr_to);
			if (stduint leng = minof(fo_leng, to_leng)) MccaMemCopyP(
				to_addr, to_th->parent_process, msg_in_kernel,
				fo_addr, fo_th->parent_process, fo_th->unsolved_msg_from_kernel,
				leng
			);
			if (to_msg && fo_msg) to_msg->type = fo_msg->type;
			if (to_msg) to_msg->src = foo;
			fo_th->unsolved_msg = NULL;
			fo_th->send_to_whom = nullptr;
			wake_fo = fo_th;
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
				auto [to_msg, to_addr, to_leng] = FetchMessage(to_th->parent_process, msg, msg_in_kernel);

				stduint leng = minof(amsg->msg.data.length, to_leng);
				if (leng) {
					MccaMemCopyP(
						to_addr, to_th->parent_process, msg_in_kernel,
						(void*)amsg->msg.data.address, nullptr, true,
						leng
					);
				}
				if (to_msg) {
					to_msg->type = amsg->msg.type;
					to_msg->src = amsg->msg.src;
				}
				to_th->async_messages.Remove(target_node);
				return 0;
			}
			else { // block self to wait for msg
				if (_sigset_raw(&to_th->pending_signals) & ~_sigset_raw(&to_th->blocked_signals))
					return -4;
				if (foo == ANYPROC || foo == INTRUPT) {
					fo_th_tgt = (ThreadBlock*)foo;
				} else {
					fo_th_tgt = fo_th_lookup;
				}

				if (to_th->unsolved_msg) plogwarn("T%u, unsolved_msg when recv(%u)", to_th->tid, foo);
				to_th->unsolved_msg = msg; to_th->unsolved_msg_from_kernel = msg_in_kernel;
				to_th->recv_fo_whom = fo_th_tgt;
				do_block = true;
			}
		}
	}

	for0(i, dropped_count) {
		if (dropped_wakes[i]) dropped_wakes[i]->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
	}

	if (wake_fo) {
		wake_fo->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
		return 0;
	}
	if (do_block) {
		to_th->Block(ThreadBlock::BlockReason::BR_RecvMsg);
		bool need_schedule = true;
		{
			SpinlockLocal guard(&comm_lock);
			if (!to_th->unsolved_msg && !to_th->recv_fo_whom) {
				need_schedule = false;
			}
		}
		if (need_schedule) {
			Taskman::Schedule(true);
			if (to_th->unsolved_msg == (CommMsg*)-1) {
				to_th->unsolved_msg = nullptr;
				return -4;
			}
		}
		else {
			to_th->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
		}

		// If woke up or already delivered with unsolved_msg == nullptr, message (sync/interrupt) was already copied directly!
		if (!to_th->unsolved_msg) {
			return 0;
		}

		// Otherwise, if woken up with an async message queued in async_messages
		{
			SpinlockLocal guard(&comm_lock);
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
				auto [to_msg, to_addr, to_leng] = FetchMessage(to_th->parent_process, msg, msg_in_kernel);

				stduint leng = minof(amsg->msg.data.length, to_leng);
				if (leng) {
					MccaMemCopyP(
						to_addr, to_th->parent_process, msg_in_kernel,
						(void*)amsg->msg.data.address, nullptr, true,
						leng
					);
				}
				if (to_msg) {
					to_msg->type = amsg->msg.type;
					to_msg->src = amsg->msg.src;
				}
				to_th->async_messages.Remove(target_node);
				to_th->unsolved_msg = nullptr;
				to_th->recv_fo_whom = nullptr;
				return 0;
			}
			to_th->unsolved_msg = nullptr;
			to_th->recv_fo_whom = nullptr;
			return -4;
		}
	}
	return 0;
}


void msg_cleanup_thread(ThreadBlock* th) {
	#if _MCCA == 0x8632
	ThreadBlock* wake_list[16];
	stduint wake_count = 0;
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
		if (wake_count < numsof(wake_list)) {
			wake_list[wake_count++] = sender;
		}
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
		th->unsolved_msg = (CommMsg*)-1;
	}
	if (th->block_reason & ThreadBlock::BlockReason::BR_RecvMsg) {
		th->recv_fo_whom = nullptr;
		th->unsolved_msg = (CommMsg*)-1;
	}

	// Case 3: Clean up all pending async messages for this thread
	th->async_messages.Remove(0, th->async_messages.Count());

	#if _MCCA == 0x8632
	auto* sl = guard.spinlock;
	guard.spinlock = nullptr;
	sl->Release(guard.old_if);
	for0(i, wake_count) {
		if (wake_list[i]) {
			wake_list[i]->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
		}
	}
	#endif
}

#endif
