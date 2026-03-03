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
void _Comment(R0) serv_sysmsg() {
	#if _MCCA == 0x8664 && defined(_UEFI)
	while (true) {
		IC.enAble(false);
		// auto crt_tick = tick;
		if (!message_queue.Count()) {
			IC.enAble(true);
			// SwitchTaskContext(&task_b_ctx, &task_kernel_ctx);
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
			if (msg.args.timer.iden == 0)
			{
				IC.enAble(false);
				SysTimer::Append(100, 0);
				IC.enAble(true);
			}
			break;
		case SysMessage::RUPT_KBD:
			// ploginfo("Kbd %[32H]", msg.args.kbd_event);
			sysmsg_kbd(msg.args.kbd_event);
			break;
		default:
			plogerro("Unknown message type: %d", msg.type);
			break;
		}
	}
	#endif
}

#ifdef _ARC_x86 // x86:

#define HARDRUPT 1

void rupt_proc(stduint pid, stduint rupt_no)
{
	auto task = TaskGet(pid);
	if ((_IMM(task->block_reason) & _IMM(ProcessBlock::BlockReason::BR_RecvMsg)) &&
		((stduint)task->recv_fo_whom == ANYPROC || (stduint)task->recv_fo_whom == INTRUPT)) {
		// ploginfo("INT-MSG: RUPT-PROC");
		CommMsg tmp_msg{ 0 };
		tmp_msg.type = HARDRUPT;
		MemCopyP(task->unsolved_msg, task->paging, &tmp_msg, kernel_paging, sizeof(tmp_msg));
		task->wait_rupt_no = nil;
		task->unsolved_msg = NULL;
		task->Unblock(ProcessBlock::BlockReason::BR_RecvMsg);
	}
	else {
		task->wait_rupt_no = rupt_no;
	}
}


static bool msg_send_will_deadlock(ProcessBlock* fo, ProcessBlock* to)
{
	// e.g. A->B->C->A
	ProcessBlock* crt = to;
	while (true) {
		if (crt->block_reason == ProcessBlock::BR_SendMsg) {
			if (crt->send_to_whom == fo) {
				plogerro("deadlock %[32H]<-...->%[32H]", fo->getID(), crt->getID());
				return true;
			}
			if (!crt->send_to_whom) break;
			crt = crt->send_to_whom;
		}
		else break;
	}
	return false;
}

int msg_send(ProcessBlock* fo, stduint too, _Comment(vaddr) CommMsg* msg)
{
	_TEMP _ASM("cli");
	if (!too) return 2;
	auto to = TaskGet(too);
	if (msg_send_will_deadlock(fo, to)) {
		plogerro("msg_send_will_deadlock");
		return -1;
	}

	if ((_IMM(to->block_reason) & _IMM(ProcessBlock::BR_RecvMsg)) &&
		(to->recv_fo_whom == fo || (stduint)to->recv_fo_whom == ANYPROC)) {
		// assert to.unsolved_msg && msg
		stduint leng = *(stduint*)fo->paging[_IMM(&msg->data.length)];
		CommMsg* msg_fo = (CommMsg*)fo->paging[_IMM(msg)];
		void* addr_fo = (void*)msg_fo->data.address;
		CommMsg* msg_to = (CommMsg*)to->paging[_IMM(to->unsolved_msg)];
		void* addr_to = (void*)msg_to->data.address;
		MIN(leng, msg_to->data.length);
		// ploginfo("SEND-MemCopyP %[32H], ..., %[32H], ..., %d)", addr_to, addr_fo, leng);
		if (leng) MemCopyP(addr_to, to->paging, addr_fo, fo->paging, leng);
		msg_to->type = msg_fo->type;
		msg_to->src = fo->getID();
		to->unsolved_msg = NULL;
		to->recv_fo_whom = nullptr;
		to->Unblock(ProcessBlock::BR_RecvMsg);
		// assert fo & to: block_reason, unsolved_msg, recv_fo_whom, send_to_whom
	}
	else {
		fo->Block(ProcessBlock::BR_SendMsg);
		fo->send_to_whom = to;
		if (fo->unsolved_msg) plogwarn("pid%u, unsolved_msg when send(%u)(%u)", fo->getID(), too, 0);
		fo->unsolved_msg = msg;
		// proc sending queue
		if (!to->queue_send_queuehead) to->queue_send_queuehead = fo; else {
			ProcessBlock* crt = to->queue_send_queuehead;
			while (crt->queue_send_queuenext) {
				crt = crt->queue_send_queuenext;
				if (!crt) { plogerro("Loss of ProcessBlock since qsend %[32H]", too); return 1; }
			}
			crt->queue_send_queuenext = fo;
		}
		fo->queue_send_queuenext = nullptr;// keep this at tail
	}
	return 0;
}
int msg_recv(ProcessBlock* to, stduint foo, _Comment(vaddr) CommMsg* msg)
{
	_TEMP _ASM("cli");

	_Comment(Proc - Interrupt) if ((to->wait_rupt_no) && (foo == ANYPROC || foo == INTRUPT)) {
		CommMsg tmp_msg{ 0 };
		tmp_msg.type = HARDRUPT;
		tmp_msg.src = to->wait_rupt_no;
		MemCopyP(msg, to->paging, &tmp_msg, kernel_paging, sizeof(tmp_msg));
		to->wait_rupt_no = nil;
		// ploginfo("INT-MSG: RECV-MSG");
		return 0;
	}
	bool determined = false;
	ProcessBlock* prev = nullptr;
	ProcessBlock* fo = nullptr;
	if (foo == ANYPROC) {
		if (to->queue_send_queuehead) {
			fo = to->queue_send_queuehead;
			foo = fo->getID();
			determined = true;
		}
	}
	else {
		// ploginfo("%u -> %u", foo, to->getID());
		fo = TaskGet(foo);
		if (fo->block_reason == ProcessBlock::BR_SendMsg &&
			fo->send_to_whom == to) {
			ProcessBlock* crt = to->queue_send_queuehead;
			while (crt) {
				if (crt == fo) {
					determined = true; break;
				}
				prev = crt;
				crt = crt->queue_send_queuenext;
			}
		}
	}

	if (determined) {
		if (fo == to->queue_send_queuehead) {
			to->queue_send_queuehead = fo->queue_send_queuenext;
			fo->queue_send_queuenext = nullptr;
		}
		else {
			if (!prev) { plogerro("!prev in %s", __FUNCIDEN__); return 1; }
			asserv(prev)->queue_send_queuenext = fo->queue_send_queuenext;// A->[B]->C => A->C
			fo->queue_send_queuenext = nullptr;
		}
		//
		CommMsg* msg_fo = (CommMsg*)fo->paging[_IMM(fo->unsolved_msg)];
		stduint leng0 = msg_fo->data.length;
		void* addr_fo = (void*)msg_fo->data.address;
		CommMsg* msg_to = (CommMsg*)to->paging[_IMM(msg)];
		stduint leng1 = msg_to->data.length;
		void* addr_to = (void*)msg_to->data.address;
		//
		// ploginfo("RECV-MemCopyP %[32H], ..., %[32H], ..., minof(%d,%d)", addr_to, addr_fo, leng0, leng1);
		stduint leng = minof(leng0, leng1);
		if (leng) MemCopyP(addr_to, to->paging, addr_fo, fo->paging, leng);
		msg_to->type = msg_fo->type;
		msg_to->src = foo;
		fo->unsolved_msg = NULL;
		fo->send_to_whom = nullptr;
		fo->Unblock(ProcessBlock::BR_SendMsg);
	}
	else { // block self to wait for msg
		// ploginfo("PID%u: BLOC[RECV]", to->getID());
		to->Block(ProcessBlock::BR_RecvMsg);
		if (to->unsolved_msg) plogwarn("pid%u, unsolved_msg when recv(%u)(%u)", to->getID(), foo, 0);
		to->unsolved_msg = msg;
		to->recv_fo_whom = (foo == ANYPROC || foo == INTRUPT) ? (ProcessBlock*)foo : TaskGet(foo);
	}
	return 0;
}


#endif
