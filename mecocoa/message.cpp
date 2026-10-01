// ASCII g++ TAB4 LF
// Codifiers: @dosconio, @ArinaMgk
// Docutitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <c/task.h>

SpinlockBlock<uni::Queue<SysMessage>> message_queue_conv;

#if 1

static bool WriteDeviceEventMessage(CommMsg* target, ProcessBlock* process,
	bool target_in_kernel, const DeviceEvent& event) {
	if (!target) return false;
	CommMsg message = {};
	MccaMemCopyP(
		&message, nullptr, true,
		target, process, target_in_kernel,
		sizeof(message)
	);
	if (!message.data.address || message.data.length < sizeof(event)) return false;
	MccaMemCopyP(
		(void*)message.data.address, process, target_in_kernel,
		&event, nullptr, true,
		sizeof(event)
	);
	message.data.length = sizeof(event);
	message.type = _IMM(KernelMsg::DeviceEvent);
	message.src = event.device_handle;
	MccaMemCopyP(
		target, process, target_in_kernel,
		&message, nullptr, true,
		sizeof(message)
	);
	return true;
}

static bool SameDeviceEventSource(const DeviceEvent& left, const DeviceEvent& right) {
	return left.kind == right.kind && left.device_handle == right.device_handle &&
		left.source == right.source && left.generation == right.generation;
}

static void MergeDeviceEvent(DeviceEvent& target, const DeviceEvent& event, uint16 extra_flags) {
	const uint64 total = uint64(target.count) + uint64(event.count);
	target.count = total > 0xFFFFFFFFu ? 0xFFFFFFFFu : uint32(total);
	target.sequence = event.sequence;
	target.flags |= event.flags | DeviceEventFlag_Coalesced | extra_flags;
	if (total > 0xFFFFFFFFu) target.flags |= DeviceEventFlag_Overflow;
}

static bool QueueDeviceEvent(ThreadBlock* thread, const DeviceEvent& event) {
	if (!thread || !thread->device_events) return false;
	const stduint count = thread->device_events->pending.Count();
	bool coalesced = false;
	for0(i, count) {
		DeviceEvent pending = {};
		if (!thread->device_events->pending.Dequeue(pending)) return false;
		if (!SameDeviceEventSource(pending, event) || coalesced) {
			if (!thread->device_events->pending.Enqueue(pending)) return false;
			continue;
		}
		MergeDeviceEvent(pending, event, DeviceEventFlag_None);
		coalesced = true;
		if (!thread->device_events->pending.Enqueue(pending)) return false;
	}
	if (coalesced || thread->device_events->pending.Enqueue(event)) return true;

	for0(i, DeviceEventQueue::OverflowCapacity) {
		if (!thread->device_events->overflow_used[i] ||
			!SameDeviceEventSource(thread->device_events->overflow[i], event)) continue;
		MergeDeviceEvent(thread->device_events->overflow[i], event, DeviceEventFlag_Overflow);
		return true;
	}
	for0(i, DeviceEventQueue::OverflowCapacity) {
		if (thread->device_events->overflow_used[i]) continue;
		thread->device_events->overflow[i] = event;
		thread->device_events->overflow[i].flags |= DeviceEventFlag_Overflow;
		thread->device_events->overflow_used[i] = true;
		return true;
	}
	const stduint pending_count = thread->device_events->pending.Count();
	for0(i, pending_count) {
		DeviceEvent pending = {};
		if (!thread->device_events->pending.Dequeue(pending)) break;
		pending.flags |= DeviceEventFlag_Overflow;
		(void)thread->device_events->pending.Enqueue(pending);
	}
	for0(i, DeviceEventQueue::OverflowCapacity) {
		if (thread->device_events->overflow_used[i]) {
			thread->device_events->overflow[i].flags |= DeviceEventFlag_Overflow;
		}
	}
	return false;
}

static bool DequeueDeviceEvent(DeviceEventQueue* events, DeviceEvent& event) {
	if (!events) return false;
	if (events->pending.Dequeue(event)) return true;
	for0(i, DeviceEventQueue::OverflowCapacity) {
		if (!events->overflow_used[i]) continue;
		event = events->overflow[i];
		events->overflow[i] = {};
		events->overflow_used[i] = false;
		return true;
	}
	return false;
}

bool device_event_prepare(ThreadBlock* thread) {
	if (!thread) return false;
	{
		extern Spinlock comm_lock;
		SpinlockLocal guard(&comm_lock);
		if (thread->device_events) return true;
	}
	auto* prepared = new DeviceEventQueue;
	if (!prepared) return false;
	{
		extern Spinlock comm_lock;
		SpinlockLocal guard(&comm_lock);
		if (!thread->device_events) {
			thread->device_events = prepared;
			prepared = nullptr;
		}
	}
	if (prepared) delete prepared;
	return true;
}

void device_event_release(ThreadBlock* thread) {
	if (!thread) return;
	DeviceEventQueue* events = nullptr;
	{
		extern Spinlock comm_lock;
		SpinlockLocal guard(&comm_lock);
		events = thread->device_events;
		thread->device_events = nullptr;
	}
	if (events) delete events;
}

bool device_event_proc(stduint tid, const DeviceEvent& event) {
	auto* thread = Taskman::LocateThread(tid);
	if (!thread) return false;
	bool do_unblock = false;
	bool delivered = false;
	{
		extern Spinlock comm_lock;
		SpinlockLocal guard(&comm_lock);
		if (!thread->device_events) return false;
		if ((_IMM(thread->block_reason) & _IMM(ThreadBlock::BlockReason::BR_RecvMsg)) &&
			thread->unsolved_msg &&
			((stduint)thread->recv_fo_whom == ANYPROC || (stduint)thread->recv_fo_whom == INTRUPT) &&
			WriteDeviceEventMessage(thread->unsolved_msg, thread->parent_process,
				thread->unsolved_msg_from_kernel, event)) {
			thread->unsolved_msg = nullptr;
			thread->recv_fo_whom = nullptr;
			do_unblock = true;
			delivered = true;
		}
		else {
			delivered = QueueDeviceEvent(thread, event);
		}
	}
	if (do_unblock) {
		thread->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
	}
	return delivered;
}

void device_event_cancel(stduint tid, stduint device_handle, uint32 generation) {
	auto* thread = Taskman::LocateThread(tid);
	if (!thread) return;
	extern Spinlock comm_lock;
	SpinlockLocal guard(&comm_lock);
	if (!thread->device_events) return;
	const stduint count = thread->device_events->pending.Count();
	for0(i, count) {
		DeviceEvent event = {};
		if (!thread->device_events->pending.Dequeue(event)) break;
		if (event.device_handle == device_handle && event.generation == generation) continue;
		(void)thread->device_events->pending.Enqueue(event);
	}
	for0(i, DeviceEventQueue::OverflowCapacity) {
		if (!thread->device_events->overflow_used[i]) continue;
		const auto& event = thread->device_events->overflow[i];
		if (event.device_handle != device_handle || event.generation != generation) continue;
		thread->device_events->overflow[i] = {};
		thread->device_events->overflow_used[i] = false;
	}
}

void rupt_proc(stduint tid, stduint rupt_no)
{
	auto th = Taskman::LocateThread(tid);
	if (!th) return;
	bool do_unblock = false;
	{
		extern Spinlock comm_lock;
		SpinlockLocal guard(&comm_lock);
		// must be an armed receiver: unsolved_msg alone is also a pending send message
		if ((_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_RecvMsg)) &&
			th->unsolved_msg &&
			((stduint)th->recv_fo_whom == ANYPROC || (stduint)th->recv_fo_whom == INTRUPT)) {
			// ploginfo("INT-MSG: RUPT-PROC");
			CommMsg tmp_msg = { };
			tmp_msg.type = _IMM(KernelMsg::Interrupt);
			tmp_msg.src = rupt_no;
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

// 255 = no parent process; ProcessBlock::State uses 0..3
static stduint DbgProcState(ProcessBlock* pb) {
	return pb ? (stduint)pb->state : 255u;
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

static void LinkWaitEntry(ThreadBlock* th, ThreadBlock* peer) {
	th->queue_wait_queuenext = peer->queue_wait_queuehead;
	peer->queue_wait_queuehead = th;
}

// Unlink th from the wait-chain of the peer it is currently waiting on (if any).
// Caller must hold comm_lock.
void UnlinkWaitEntry(ThreadBlock* th) {
	ThreadBlock* peer = th->recv_fo_whom;
	if (!peer || (stduint)peer == ANYPROC || (stduint)peer == INTRUPT) return;
	ThreadBlock* crt = peer->queue_wait_queuehead;
	ThreadBlock* prev = nullptr;
	while (crt && crt != th) { prev = crt; crt = crt->queue_wait_queuenext; }
	if (!crt) return;
	if (prev) prev->queue_wait_queuenext = crt->queue_wait_queuenext;
	else peer->queue_wait_queuehead = crt->queue_wait_queuenext;
	crt->queue_wait_queuenext = nullptr;
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
	if (_IMM(sender->block_reason) & _IMM(ThreadBlock::BlockReason::BR_SendMsg)) {
		sender->unsolved_msg = nullptr;
	}
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
	if (!too || too == ANYPROC) return 2;// 0 is the kernel thread; ANYPROC is recv-only
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
			UnlinkWaitEntry(to_th);
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
			if (to_th->send_to_whom == fo_th || to_th->recv_fo_whom == fo_th ||
				((stduint)to_th->recv_fo_whom == ANYPROC && !to_th->unsolved_msg &&
				(_IMM(to_th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_RecvMsg)))) {
				const stduint recv_tid =
					((stduint)to_th->recv_fo_whom == ANYPROC ||
					(stduint)to_th->recv_fo_whom == INTRUPT) ?
					(stduint)to_th->recv_fo_whom :
					(to_th->recv_fo_whom ? to_th->recv_fo_whom->tid : 0);
				plogwarn("IPC mutual wait: T%u(Send->T%u), T%u(Send=%u, Recv=%u) [peer umsg=%p st=%u br=%u pst=%u; self umsg=%p]",
					fo_th->tid, to_th->tid,
					to_th->tid,
					to_th->send_to_whom ? to_th->send_to_whom->tid : 0,
					recv_tid,
					(void*)to_th->unsolved_msg,
					(stduint)to_th->state, (stduint)to_th->block_reason,
					DbgProcState(to_th->parent_process),
					(void*)fo_th->unsolved_msg);
			}
			// proc sending queue
			if (!to_th->queue_send_queuehead) to_th->queue_send_queuehead = fo_th; else {
				ThreadBlock* crt = to_th->queue_send_queuehead;
				while (crt->queue_send_queuenext) {
					crt = crt->queue_send_queuenext;
					if (!crt) {
						plogerro("Loss of ThreadBlock since qsend T%u", too);
						fo_th->send_to_whom = nullptr;
						fo_th->unsolved_msg = nullptr;
						fo_th->queue_send_queuenext = nullptr;
						return 1;
					}
				}
				crt->queue_send_queuenext = fo_th;
			}
			fo_th->queue_send_queuenext = nullptr;// keep this at tail
			// no wake here: an armed receiver was already served by the branch above
			do_block = true;
		}
	}

	if (do_block) {
		while (true) {
			fo_th->Block(ThreadBlock::BlockReason::BR_SendMsg);
			if (wake_th) {
				// Block sender first: Unblock() is edge-triggered when the peer is still Running.
				wake_th->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
				wake_th = nullptr;
			}
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
					SpinlockLocal guard(&comm_lock);
					if (to_th && fo_th->send_to_whom == to_th) {
						if (to_th->queue_send_queuehead == fo_th) {
							to_th->queue_send_queuehead = fo_th->queue_send_queuenext;
						}
						else {
							ThreadBlock* prev = to_th->queue_send_queuehead;
							while (prev && prev->queue_send_queuenext != fo_th) {
								prev = prev->queue_send_queuenext;
							}
							if (prev) {
								prev->queue_send_queuenext = fo_th->queue_send_queuenext;
							}
						}
					}
					fo_th->send_to_whom = nullptr;
					fo_th->queue_send_queuenext = nullptr;
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

			if (_sigset_raw(&fo_th->pending_signals) & ~_sigset_raw(&fo_th->blocked_signals)) {
				// Interrupted by signal: unlink from to_th queue safely under comm_lock
				SpinlockLocal guard(&comm_lock);
				if (to_th && fo_th->send_to_whom == to_th) {
					if (to_th->queue_send_queuehead == fo_th) {
						to_th->queue_send_queuehead = fo_th->queue_send_queuenext;
					}
					else {
						ThreadBlock* prev = to_th->queue_send_queuehead;
						while (prev && prev->queue_send_queuenext != fo_th) {
							prev = prev->queue_send_queuenext;
						}
						if (prev) {
							prev->queue_send_queuenext = fo_th->queue_send_queuenext;
						}
					}
				}
				fo_th->send_to_whom = nullptr;
				fo_th->queue_send_queuenext = nullptr;
				fo_th->unsolved_msg = nullptr;
				return -4;
			}
		}
	}
	if (wake_th) {
		wake_th->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
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

	ThreadBlock* drop_wake_head = nullptr;
	ThreadBlock* wake_fo = nullptr;
	bool do_block = false;
	bool defer_ret = false;
	int ret_val = 0;
	ThreadBlock* fo_th_tgt = nullptr;

	{
		SpinlockLocal guard(&comm_lock);
		if (to_th->device_events && to_th->device_events->HasPending() &&
			(foo == ANYPROC || foo == INTRUPT)) {
			DeviceEvent event = {};
			if (!DequeueDeviceEvent(to_th->device_events, event)) return -1;
			if (!WriteDeviceEventMessage(msg, to_th->parent_process, msg_in_kernel, event)) {
				(void)QueueDeviceEvent(to_th, event);
				return -1;
			}
			return 0;
		}
		_Comment(Proc - Interrupt) if ((to_th->wait_rupt_no) && (foo == ANYPROC || foo == INTRUPT)) {
			CommMsg tmp_msg = { {0, 0}, _IMM(KernelMsg::Interrupt), to_th->wait_rupt_no };
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
				if (dropped) {
					dropped->queue_send_queuenext = drop_wake_head;
					drop_wake_head = dropped;
				}
			}
			if (to_th->queue_send_queuehead) {
				KASSERT(to_th->queue_send_queuehead->send_to_whom == to_th);
				fo_th = to_th->queue_send_queuehead;
				foo = fo_th->tid;
				determined = true;
			}
		}
		else if (foo != INTRUPT) {
			fo_th = fo_th_lookup;
			if (fo_th && fo_th->send_to_whom == to_th) {
				ThreadBlock* crt = to_th->queue_send_queuehead;
				ThreadBlock* prv = nullptr;
				while (crt && crt != fo_th) {
					prv = crt;
					crt = crt->queue_send_queuenext;
				}
				if (crt && CanUseSyncMsgState(fo_th)) {
					prev = prv;
					determined = true;
				}
				else if (crt) {// stale entry: message gone, drop it and wake the peer with -4
					ThreadBlock* dropped = DropQueuedSender(to_th, prv, fo_th);
					if (dropped) {
						if (_IMM(dropped->block_reason) & _IMM(ThreadBlock::BlockReason::BR_SendMsg))
							dropped->unsolved_msg = (CommMsg*)-1;
						dropped->queue_send_queuenext = drop_wake_head;
						drop_wake_head = dropped;
					}
					fo_th = nullptr;
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
			KASSERT(CanUseSyncMsgState(fo_th));

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
				defer_ret = true; ret_val = 0;
			}
			else { // block self to wait for msg
				if (!msg) {
					plogwarn("RECV T%u: null msg buffer, foo=%u (refusing to arm)", to_th->tid, foo);
					defer_ret = true; ret_val = -1;
				}
				else if (_sigset_raw(&to_th->pending_signals) & ~_sigset_raw(&to_th->blocked_signals)) {
					defer_ret = true; ret_val = -4;
				}
				else {
					if (foo == ANYPROC || foo == INTRUPT) {
						fo_th_tgt = (ThreadBlock*)foo;
					} else {
						fo_th_tgt = fo_th_lookup;
						LinkWaitEntry(to_th, fo_th_lookup);
					}

					if (to_th->unsolved_msg) plogwarn("T%u, unsolved_msg when recv(%u)", to_th->tid, foo);
					to_th->unsolved_msg = msg; to_th->unsolved_msg_from_kernel = msg_in_kernel;
					to_th->recv_fo_whom = fo_th_tgt;
					do_block = true;
				}
			}
		}
	}

	while (drop_wake_head) {
		ThreadBlock* dropped = drop_wake_head;
		drop_wake_head = dropped->queue_send_queuenext;
		dropped->queue_send_queuenext = nullptr;
		dropped->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
	}

	if (defer_ret) return ret_val;
	if (wake_fo) {
		wake_fo->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
		return 0;
	}
	if (do_block) {
		while (true) {
			to_th->Block(ThreadBlock::BlockReason::BR_RecvMsg);

			if (foo != ANYPROC && foo != INTRUPT &&
				fo_th_lookup && fo_th_lookup->send_to_whom == to_th) {
				const stduint recv_tid =
					((stduint)fo_th_lookup->recv_fo_whom == ANYPROC ||
					(stduint)fo_th_lookup->recv_fo_whom == INTRUPT) ?
					(stduint)fo_th_lookup->recv_fo_whom :
					(fo_th_lookup->recv_fo_whom ? fo_th_lookup->recv_fo_whom->tid : 0);
				bool in_queue = false;
				for (ThreadBlock* nod = to_th->queue_send_queuehead; nod; nod = nod->queue_send_queuenext) {
					if (nod == fo_th_lookup) { in_queue = true; break; }
				}
				plogwarn("IPC mutual wait: T%u(Recv<-T%u), T%u(Send=%u, Recv=%u) [peer umsg=%p st=%u br=%u pst=%u; qhead=%u inq=%u]",
					to_th->tid, fo_th_lookup->tid,
					fo_th_lookup->tid,
					fo_th_lookup->send_to_whom ? fo_th_lookup->send_to_whom->tid : 0,
					recv_tid,
					(void*)fo_th_lookup->unsolved_msg,
					(stduint)fo_th_lookup->state, (stduint)fo_th_lookup->block_reason,
					DbgProcState(fo_th_lookup->parent_process),
					to_th->queue_send_queuehead ? to_th->queue_send_queuehead->tid : 0,
					(stduint)in_queue);
			}
			bool need_schedule = true;
			{
				SpinlockLocal guard(&comm_lock);
				if (!to_th->unsolved_msg) {
					need_schedule = false;
				}
			}
			if (need_schedule) {
				Taskman::Schedule(true);
				if (to_th->unsolved_msg == (CommMsg*)-1) {
					SpinlockLocal guard(&comm_lock);
					UnlinkWaitEntry(to_th);
					to_th->unsolved_msg = nullptr;
					to_th->recv_fo_whom = nullptr;
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

			// Check for pending unblocked signals
			if (_sigset_raw(&to_th->pending_signals) & ~_sigset_raw(&to_th->blocked_signals)) {
				SpinlockLocal guard(&comm_lock);
				UnlinkWaitEntry(to_th);
				to_th->unsolved_msg = nullptr;
				to_th->recv_fo_whom = nullptr;
				return -4;
			}

			// Check for sync queued senders attached while waiting
			{
				ThreadBlock* wake_sync_fo = nullptr;
				ThreadBlock* wake_dropped_head = nullptr;
				{
					SpinlockLocal guard(&comm_lock);
					bool determined = false;
					ThreadBlock* prev = nullptr;
					ThreadBlock* fo_th = nullptr;

					if (foo == ANYPROC) {
						while (to_th->queue_send_queuehead && !CanUseSyncMsgState(to_th->queue_send_queuehead)) {
							ThreadBlock* dropped = DropQueuedSender(to_th, nullptr, to_th->queue_send_queuehead);
							if (dropped) {
								dropped->queue_send_queuenext = wake_dropped_head;
								wake_dropped_head = dropped;
							}
						}
						if (to_th->queue_send_queuehead) {
							KASSERT(to_th->queue_send_queuehead->send_to_whom == to_th);
							fo_th = to_th->queue_send_queuehead;
							foo = fo_th->tid;
							determined = true;
						}
					}
					else if (foo != INTRUPT) {
						fo_th = fo_th_lookup;
						if (fo_th && fo_th->send_to_whom == to_th) {
							ThreadBlock* crt = to_th->queue_send_queuehead;
							ThreadBlock* prv = nullptr;
							while (crt && crt != fo_th) {
								prv = crt;
								crt = crt->queue_send_queuenext;
							}
							if (crt && CanUseSyncMsgState(fo_th)) {
								prev = prv;
								determined = true;
							}
							else if (crt) {// stale entry: message gone, drop it and wake the peer with -4
								ThreadBlock* dropped = DropQueuedSender(to_th, prv, fo_th);
								if (dropped) {
									if (_IMM(dropped->block_reason) & _IMM(ThreadBlock::BlockReason::BR_SendMsg))
										dropped->unsolved_msg = (CommMsg*)-1;
									dropped->queue_send_queuenext = wake_dropped_head;
									wake_dropped_head = dropped;
								}
								fo_th = nullptr;
							}
						}
					}

					if (determined && fo_th) {
						if (fo_th == to_th->queue_send_queuehead) {
							to_th->queue_send_queuehead = fo_th->queue_send_queuenext;
							fo_th->queue_send_queuenext = nullptr;
						}
						else if (prev) {
							prev->queue_send_queuenext = fo_th->queue_send_queuenext;
							fo_th->queue_send_queuenext = nullptr;
						}

						auto [fo_msg, fo_addr, fo_leng] = FetchMessage(fo_th->parent_process, fo_th->unsolved_msg, fo_th->unsolved_msg_from_kernel);
						auto [to_msg, to_addr, to_leng] = FetchMessage(to_th->parent_process, msg, msg_in_kernel);

						if (stduint leng = minof(fo_leng, to_leng)) {
							MccaMemCopyP(
								to_addr, to_th->parent_process, msg_in_kernel,
								fo_addr, fo_th->parent_process, fo_th->unsolved_msg_from_kernel,
								leng
							);
						}
						if (to_msg && fo_msg) to_msg->type = fo_msg->type;
						if (to_msg) to_msg->src = foo;

						UnlinkWaitEntry(to_th);
						fo_th->unsolved_msg = nullptr;
						fo_th->send_to_whom = nullptr;
						to_th->unsolved_msg = nullptr;
						to_th->recv_fo_whom = nullptr;
						wake_sync_fo = fo_th;
					}
				}

				while (wake_dropped_head) {
					ThreadBlock* dropped = wake_dropped_head;
					wake_dropped_head = dropped->queue_send_queuenext;
					dropped->queue_send_queuenext = nullptr;
					dropped->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
				}
				if (wake_sync_fo) {
					wake_sync_fo->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
					return 0;
				}
			}

			// Check for async messages queued while waiting
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
					UnlinkWaitEntry(to_th);
					to_th->unsolved_msg = nullptr;
					to_th->recv_fo_whom = nullptr;
					return 0;
				}
			}
		}
	}
	return 0;
}


void msg_cleanup_thread(ThreadBlock* th, bool dying) {
	ThreadBlock* drop_wake_head = nullptr;
	ThreadBlock* recv_wake_head = nullptr;
	SpinlockLocal guard(&comm_lock);
	
	if (dying) {
		// Case 0: wake receivers waiting on this dying thread (revalidate each entry)
		ThreadBlock* waiter = th->queue_wait_queuehead;
		th->queue_wait_queuehead = nullptr;
		while (waiter) {
			ThreadBlock* w_next = waiter->queue_wait_queuenext;
			waiter->queue_wait_queuenext = nullptr;
			if (waiter->recv_fo_whom == th && waiter->unsolved_msg &&
				(_IMM(waiter->block_reason) & _IMM(ThreadBlock::BlockReason::BR_RecvMsg))) {
				waiter->recv_fo_whom = nullptr;
				waiter->unsolved_msg = (CommMsg*)-1;
				waiter->queue_wait_queuenext = recv_wake_head;
				recv_wake_head = waiter;
			}
			waiter = w_next;
		}
	}

	// Case 1: This thread (th) was a target (Receiver), unblock all its senders
	ThreadBlock* sender = th->queue_send_queuehead;
	while (sender) {
		ThreadBlock* s_next = sender->queue_send_queuenext;
		sender->send_to_whom = nullptr;
		sender->queue_send_queuenext = drop_wake_head;
		drop_wake_head = sender;
		if (_IMM(sender->block_reason) & _IMM(ThreadBlock::BlockReason::BR_SendMsg)) {
			sender->unsolved_msg = nullptr;
		}
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
		UnlinkWaitEntry(th);
		th->recv_fo_whom = nullptr;
		th->unsolved_msg = (CommMsg*)-1;
	}

	// Case 3: Clean up all pending async messages for this thread
	th->async_messages.Remove(0, th->async_messages.Count());

	auto* sl = guard.spinlock;
	guard.spinlock = nullptr;
	sl->Release(guard.old_if);
	while (drop_wake_head) {
		ThreadBlock* dropped = drop_wake_head;
		drop_wake_head = dropped->queue_send_queuenext;
		dropped->queue_send_queuenext = nullptr;
		dropped->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
	}
	while (recv_wake_head) {
		ThreadBlock* w = recv_wake_head;
		recv_wake_head = w->queue_wait_queuenext;
		w->queue_wait_queuenext = nullptr;
		w->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
	}
}

#endif
