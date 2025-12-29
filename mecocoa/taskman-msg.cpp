// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#undef _DEBUG
#define _DEBUG
#include <c/task.h>
#include <c/consio.h>

use crate uni;
#ifdef _ARC_x86 // x86:
#include "../include/atx-x86-flap32.hpp"

#define HARDRUPT 1

void rupt_proc(stduint pid, stduint rupt_no)
{
	auto task = TaskGet(pid);
	if ((_IMM(task->block_reason) & _IMM(ProcessBlock::BlockReason::BR_RecvMsg)) &&
		(task->recv_fo_whom == ANYPROC || task->recv_fo_whom == INTRUPT)) {
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
			if (crt->send_to_whom == fo->getID()) {
				plogerro("deadlock %[32H]<-...->%[32H]", fo->getID(), crt->getID());
				return true;
			}
			if (!crt->send_to_whom) break;
			crt = TaskGet(crt->send_to_whom);
		}
		else break;
	}
	return false;
}

int msg_send(ProcessBlock* fo, stduint too, _Comment(vaddr) CommMsg* msg)
{
	if (!too) return 2;
	auto to = TaskGet(too);
	if (msg_send_will_deadlock(fo, to)) {
		plogerro("msg_send_will_deadlock");
		return -1;
	}

	if ((_IMM(to->block_reason) & _IMM(ProcessBlock::BR_RecvMsg)) &&
		(to->recv_fo_whom == fo->getID() || to->recv_fo_whom == ANYPROC)) {
		// assert to.unsolved_msg && msg
		stduint leng = *(stduint*)fo->paging[_IMM(&msg->data.length)];
		CommMsg* msg_fo = (CommMsg*)fo->paging[_IMM(msg)];
		void* addr_fo = (void*)msg_fo->data.address;
		CommMsg* msg_to = (CommMsg*)to->paging[_IMM(to->unsolved_msg)];
		void* addr_to = (void*)msg_to->data.address;
		MIN(leng, msg_to->data.length);
		// ploginfo("SEND-MemCopyP %[32H], ..., %[32H], ..., %d)", addr_to, addr_fo, leng);/////
		if (leng) MemCopyP(addr_to, to->paging, addr_fo, fo->paging, leng);
		msg_to->type = msg_fo->type;
		msg_to->src = fo->getID();
		to->unsolved_msg = NULL;
		to->recv_fo_whom = nil;
		to->Unblock(ProcessBlock::BR_RecvMsg);
		// assert fo & to: block_reason, unsolved_msg, recv_fo_whom, send_to_whom
	}
	else {
		fo->Block(ProcessBlock::BR_SendMsg);
		fo->send_to_whom = too;// to->getID();
		if (fo->unsolved_msg) plogwarn("unsolved_msg");
		fo->unsolved_msg = msg;
		// proc sending queue
		if (!to->queue_send_queuehead) to->queue_send_queuehead = fo->getID(); else {
			ProcessBlock* crt = TaskGet(to->queue_send_queuehead);
			while (crt->queue_send_queuenext) {
				crt = TaskGet(crt->queue_send_queuenext);
				if (!crt) { plogerro("Loss of ProcessBlock since qsend %[32H]", too); return 1; }
			}
			crt->queue_send_queuenext = fo->getID();
			//plogwarn(">>> %u (+=) %u->%u", crt->getID(), fo->getID(), too);
			//{
			//	for0(i, pnumber) {
			//		Console.OutFormat("-- %u: (%u:%u) head %u, next %u, send_to_whom\n\r",
			//			i, pblocks[i]->state, pblocks[i]->block_reason,
			//			pblocks[i]->queue_send_queuehead, pblocks[i]->queue_send_queuenext);
			//	}
			//}
		}
		fo->queue_send_queuenext = nil;// keep this at tail
	}
	return 0;
}
int msg_recv(ProcessBlock* to, stduint foo, _Comment(vaddr) CommMsg* msg)
{
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
	ProcessBlock* prev;
	if (foo == ANYPROC) {
		if (to->queue_send_queuehead) {
			foo = to->queue_send_queuehead;
			determined = true;
		}
	}
	else {
		// ploginfo("%u -> %u", foo, to->getID());
		ProcessBlock* fo = TaskGet(foo);
		if (fo->block_reason == ProcessBlock::BR_SendMsg &&
			fo->send_to_whom == to->getID()) {
			ProcessBlock* crt = TaskGet(to->queue_send_queuehead);
			while (crt->getID()) {
				if (crt->getID() == foo) {
					// foo = crt->getID();
					determined = true; break;
				}
				prev = crt;
				if (!crt->queue_send_queuenext) break;
				crt = TaskGet(crt->queue_send_queuenext);
			}
		}
	}

	if (determined) {
		ProcessBlock* fo = TaskGet(foo);
		if (foo == to->queue_send_queuehead) {
			to->queue_send_queuehead = fo->queue_send_queuenext;
			fo->queue_send_queuenext = nil;
		}
		else {
			if (!prev) { plogerro("!prev in %s", __FUNCIDEN__); return 1; }
			prev->queue_send_queuenext = fo->queue_send_queuenext;// A->[B]->C => A->C
			fo->queue_send_queuenext = nil;
		}
		//
		CommMsg* msg_fo = (CommMsg*)fo->paging[_IMM(fo->unsolved_msg)];
		stduint leng0 = msg_fo->data.length;
		void* addr_fo = (void*)msg_fo->data.address;
		CommMsg* msg_to = (CommMsg*)to->paging[_IMM(msg)];
		stduint leng1 = msg_to->data.length;
		void* addr_to = (void*)msg_to->data.address;
		//
		// ploginfo("RECV-MemCopyP %[32H], ..., %[32H], ..., minof(%d,%d)", addr_to, addr_fo, leng0, leng1);/////
		stduint leng = minof(leng0, leng1);
		if (leng) MemCopyP(addr_to, to->paging, addr_fo, fo->paging, leng);
		msg_to->type = msg_fo->type;
		msg_to->src = foo;
		fo->unsolved_msg = NULL;
		fo->send_to_whom = nil;
		fo->Unblock(ProcessBlock::BR_SendMsg);
	}
	else { // block self to wait for msg
		// ploginfo("PID%u: BLOC[RECV]", to->getID());
		to->Block(ProcessBlock::BR_RecvMsg);
		if (to->unsolved_msg) plogwarn("unsolved_msg");
		to->unsolved_msg = msg;
		to->recv_fo_whom = foo;
	}
	return 0;
}


#endif
