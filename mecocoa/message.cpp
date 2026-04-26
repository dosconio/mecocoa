// ASCII g++ TAB4 LF
// Codifiers: @dosconio, @ArinaMgk
// Docutitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <c/task.h>
#include <cpp/Witch/Control/Control-Label.hpp>
#include <cpp/Witch/Control/Control-TextBox.hpp>

#if _MCCA == 0x8664 && defined(_UEFI)
extern byte _BUF_xhc[];
extern uni::witch::control::TextBox* ptext_1;
#endif


extern void sysmsg_kbd(keyboard_event_t kbd_event);
extern uni::Dchain TimerManager;
extern uni::VideoControlInterface* real_pvci;
void _Comment(R0) serv_sysmsg() {
	#if _MCCA == 0x8664 && defined(_UEFI)
	global_layman.lazy_update = _GUI_DOUBLE_BUFFER;// Only enable lazy mode if double buffering is enabled
	while (true) {
		IC.enAble(false);
		// auto crt_tick = tick;
		if (!message_queue.Count()) {
			IC.enAble(true);
			HALT();
			continue;
		}
		SysMessage msg;
		message_queue.Dequeue(msg);
		IC.enAble(true);
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
		case SysMessage::RUPT_KBD:
			// ploginfo("Kbd %[32H]", msg.args.kbd_event);
			sysmsg_kbd(msg.args.kbd_event);
			break;
		case SysMessage::RUPT_FLUSH:
			if (ento_gui && real_pvci && global_layman.sheet_buffer) {
				if (msg.args.rect.w > 0 && msg.args.rect.h > 0) {
					// Perform delayed composition (Layer Blending)
					global_layman.UpdateForce(nullptr, msg.args.rect.toRectangle());
					// Flush back-buffer to physical screen
					real_pvci->DrawPoints(msg.args.rect.toRectangle(), global_layman.sheet_buffer);
				}
			}
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

static Spinlock comm_lock;
int msg_send(ThreadBlock* fo_th, stduint too, _Comment(vaddr) CommMsg* msg)
{
	SpinlockLocal guard(&comm_lock);
	if (!too) return 2;
	ThreadBlock* to_th = Taskman::LocateThread(too);
	if (!to_th) {
		if (auto to_pb = Taskman::Locate(too)) to_th = to_pb->main_thread;
	}
	if (!to_th) return 1;

	if (msg_send_will_deadlock(fo_th, to_th)) {
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
		if (leng) MccaMemCopyP(addr_to, to, addr_fo, fo, leng);
		if (msg_to && msg_fo) msg_to->type = msg_fo->type;
		if (msg_to) msg_to->src = fo_th->tid;
		to_th->unsolved_msg = NULL;
		to_th->recv_fo_whom = nullptr;
		to_th->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
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
		CommMsg tmp_msg{ 0 };
		tmp_msg.type = HARDRUPT;
		tmp_msg.src = to_th->wait_rupt_no;
		MccaMemCopyP(msg, to_th->parent_process, &tmp_msg, 0, sizeof(tmp_msg));
		to_th->wait_rupt_no = nil;
		return 0;
	}
	bool determined = false;
	ThreadBlock* prev = nullptr;
	ThreadBlock* fo_th = nullptr;
	if (foo == ANYPROC) {
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
		if (!fo_th) return 1;

		if (fo_th->block_reason == ThreadBlock::BlockReason::BR_SendMsg &&
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
		//
		auto msg_fo = (CommMsg*)SeekAddress(fo_th->parent_process, (stduint)fo_th->unsolved_msg);
		stduint leng0 = msg_fo ? msg_fo->data.length : 0;
		void* addr_fo = msg_fo ? (void*)msg_fo->data.address : nullptr;

		auto msg_to = (CommMsg*)SeekAddress(to_th->parent_process, _IMM(msg));
		stduint leng1 = msg_to ? msg_to->data.length : 0;
		void* addr_to = msg_to ? (void*)msg_to->data.address : nullptr;
		//
		stduint leng = minof(leng0, leng1);
		if (leng) MccaMemCopyP(addr_to, to_th->parent_process, addr_fo, fo_th->parent_process, leng);
		if (msg_to && msg_fo) msg_to->type = msg_fo->type;
		if (msg_to) msg_to->src = foo;
		fo_th->unsolved_msg = NULL;
		fo_th->send_to_whom = nullptr;
		fo_th->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
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
			if (!fo_th_tgt) return -1;
		}

		to_th->Block(ThreadBlock::BlockReason::BR_RecvMsg);
		if (to_th->unsolved_msg) plogwarn("T%u, unsolved_msg when recv(%u)", to_th->tid, foo);
		to_th->unsolved_msg = msg;
		to_th->recv_fo_whom = fo_th_tgt;
		guard.~SpinlockLocal();
		#if (_MCCA & 0xFF00) == 0x8600
		Taskman::Schedule(true);
		#endif
	}
	return 0;
}


#endif
