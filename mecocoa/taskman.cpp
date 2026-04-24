// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include "../include/filesys.hpp"

#include <c/driver/keyboard.h>

// #pragma GCC push_options
// #pragma GCC optimize("O2")

namespace uni {
	template <>
	bool Queue<SysMessage>::Enqueue(const SysMessage& item) {
		if (item.type == SysMessage::RUPT_MOUSE && !isEmpty()) {
			// Locate the tail pointer (the last written element, handling ring buffer wrap-around)
			SysMessage* tail = (p == offss) ? (offss + limit - 1) : (p - 1);

			if (tail->type == SysMessage::RUPT_MOUSE) {
				// Check if button states are identical (ensures correct logic for dragging operations)
				if (tail->args.mou_event.BtnLeft == item.args.mou_event.BtnLeft &&
					tail->args.mou_event.BtnRight == item.args.mou_event.BtnRight &&
					tail->args.mou_event.BtnMiddle == item.args.mou_event.BtnMiddle)
				{
					int new_x = tail->args.mou_event.X + item.args.mou_event.X;
					int new_y = tail->args.mou_event.Y + item.args.mou_event.Y;
					// Prevent overflow/wrap-around if the accumulated value exceeds the int8 range (-128 to 127)
					if (new_x >= -128 && new_x <= 127 && new_y >= -128 && new_y <= 127) {
						tail->args.mou_event.X = (int8)new_x;
						tail->args.mou_event.Y = (int8)new_y;
						tail->args.mou_event.DirX = (new_x < 0) ? 1 : 0;
						tail->args.mou_event.DirY = (new_y < 0) ? 1 : 0;
						return true;
					}
				}
			}
		}
		if (isFull()) return false;
		if (q == nullptr) q = p;
		*p++ = item;
		if (p >= offss + limit) p = offss;
		return true;
	}
}
static SysMessage _BUF_Message[64];
Queue<SysMessage> message_queue(_BUF_Message, numsof(_BUF_Message));

// ---- Lock ----


bool Spinlock::Acquire() {
	byte state_rupt = 0;
	#if (_MCCA & 0xFF00) == 0x8600
	stduint flags = getFlags();
	if (state_rupt = cast<REG_FLAG_t>(flags).IF) {
		IC.enAble(false);
	}
	#endif
	while (__atomic_exchange_n(&this->locked, 1, __ATOMIC_ACQUIRE) != 0) {
		#if (_MCCA & 0xFF00) == 0x8600
		asm volatile("pause" ::: "memory");
		#endif
	}
	this->cpu_id = Taskman::getID();
	return state_rupt;
}
void Spinlock::Release(bool old_if) {
	this->cpu_id = -1;
	__atomic_store_n(&this->locked, 0, __ATOMIC_RELEASE);
	if (old_if) IC.enAble();
}
void Mutex::Acquire() {
	bool old_if = this->guard.Acquire();

	if (this->locked) {
		ThreadBlock* crt = Taskman::current_thread[Taskman::getID()];
		if (!this->wait_queue.isFull()) {
			crt->state = ThreadBlock::State::Pended;
			crt->block_reason = ThreadBlock::BlockReason::BR_Waiting;
			this->wait_queue.Enqueue(crt);
			Taskman::SleepAndRelease(&this->guard);
		}
		if (old_if) IC.enAble();
	}
	else {
		this->locked = 1;
		this->guard.Release(old_if);
	}
}
void Mutex::Release() {
	bool old_if = this->guard.Acquire();
	if (!this->wait_queue.isEmpty()) {
		ThreadBlock* wakeup_tb = nullptr;
		this->wait_queue.Dequeue(wakeup_tb);
		if (wakeup_tb) {
			wakeup_tb->Unblock(ThreadBlock::BlockReason::BR_Waiting);
		}
	}
	else {
		this->locked = 0;
	}
	this->guard.Release(old_if);
}

// ---- . ----

auto Taskman::AllocateTask() -> ProcessBlock* {
	// auto ppb = zalcof(ProcessBlock);
	auto ppb = (ProcessBlock*)mempool.allocate(sizeof(ProcessBlock), 4);
	if (!ppb) {
		plogerro("Taskman::AllocateTask() failed");
		return nullptr;
	}
	MemSet(ppb, 0, sizeof(ProcessBlock));
	new (ppb) ProcessBlock();
	return ppb;
}

auto Taskman::AllocateThread() -> ThreadBlock* {
	auto tb = (ThreadBlock*)mempool.allocate(sizeof(ThreadBlock), 4);
	if (!tb) {
		plogerro("Taskman::AllocateThread() failed");
		return nullptr;
	}
	MemSet(tb, 0, sizeof(ThreadBlock));
	new (tb) ThreadBlock();
	return tb;
}

void Taskman::DumpTask(ProcessBlock* pb) {
	auto ctx = &pb->main_thread->context;
	ploginfo("=== Context [%d] at %[x] ===", pb->getID(), pb);
	ploginfo(" (%u:%u) head %u, next %u, send_to_whom",
		pb->state, pb->main_thread->block_reason, pb->main_thread->queue_send_queuehead, pb->main_thread->queue_send_queuenext);
	#if (_MCCA & 0xFF00) == 0x1000
	ploginfo("  ra : %[x]  sp : %[x]  gp : %[x]  tp : %[x]", ctx->ra, ctx->sp, ctx->gp, ctx->tp);
	ploginfo("  a0 : %[x]  a1 : %[x]  a7 : %[x]  s0 : %[x]", ctx->a0, ctx->a1, ctx->a7, ctx->s0);
	ploginfo("  mepc: %[x]  mstatus: %[x]  satp: %[x]", ctx->mepc, ctx->mstatus, ctx->satp);
	ploginfo("  ksp : %[x]", ctx->kernel_sp);

	#elif (_MCCA & 0xFF00) == 0x8600

	// x86 Context Dump (General Purpose Registers)
	ploginfo("  AX : %[x]  BX : %[x]  CX : %[x]  DX : %[x]", ctx->AX, ctx->BX, ctx->CX, ctx->DX);
	ploginfo("  SI : %[x]  DI : %[x]  BP : %[x]  SP : %[x]", ctx->SI, ctx->DI, ctx->BP, ctx->SP);

	// Instruction Pointer, Flags, and Paging
	// Note: Adjust 'FLAGS' to 'EFLAGS' or your specific struct member name if it differs
	ploginfo("  IP : %[x]  FLAGS: %[x]  CR3: %[x]", ctx->IP, ctx->FLAG, ctx->CR3);

	// Privilege Level
	ploginfo("  RING: %u", ctx->RING);
	#endif
	ploginfo("========================================");
}




bool Taskman::ExitCurrent(stduint code) {
	auto pid = CurrentPID();
	// ploginfo("AppExit: %[u] with code 0x%[x]", pid, code);

	// [r]
	// auto pb = Locate(pid);
	// pb->state = ProcessBlock::State::Hanging;
	// Schedule(true);
	//{} Add to zombie vector

	// [u]
	// __asm("mov %0, %%eax" : : "m"(para[0])); TaskReturn();
	stduint para[2] = { pid, code };
	syssend(Task_TaskMan, sliceof(para), _IMM(TaskmanMsg::EXIT));
	sysrecv(Task_TaskMan, sliceof(para));

	return true;// unreachable
}

// Resources
// - message
// - files open
// - heaps, stacks
// - paging
// - threads?
static void _Exit_Cleanup(stduint pid)
{
	#if defined(_DEBUG) && defined(_MCCA) && (_MCCA & 0xFF00) == 0x8600
	auto flag = getFlags();
	if (flag & 0x200) InterruptDisable();
	#endif
	auto ppb = Taskman::Locate(pid);
	ploginfo("cleaning %u, now exist %u tasks", pid, Taskman::chain.Count());
	if (ppb->main_thread) Taskman::DequeueReady(ppb->main_thread);
	// Msg: Clear IPC
	ThreadBlock* sender = ppb->main_thread->queue_send_queuehead;
	while (sender) {
		ThreadBlock* next = sender->queue_send_queuenext;
		sender->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
		sender = next;
	}// wakeup senders
	ppb->main_thread->queue_send_queuehead = nullptr;
	// Release Threads
	ThreadBlock* th = ppb->thread_list_head;
	while (th) {
		ThreadBlock* next_th = th->process_thread_next;
		Taskman::DequeueReady(th);
		Taskman::thchain.Remove(th);
		if (th->stack_lineaddr == th->stack_levladdr) {
			delete (byte*)th->stack_levladdr;
		}
		else {
			delete (byte*)ppb->paging[_IMM(th->stack_lineaddr) & ~0xFFF];
			delete (byte*)th->stack_levladdr;
		}
		delete (byte*)th;
		th = next_th;
	}
	ppb->thread_list_head = ppb->main_thread = nullptr;
	// Release Segments
	for0a(i, ppb->load_slices) {
		if (!ppb->load_slices[i].address) break;
		if (ppb->load_slices[i].length >= PAGE_SIZE&&
			(byte*)ppb->paging[(ppb->load_slices[i].address) & ~0xFFF] + PAGE_SIZE ==
			(byte*)ppb->paging[(ppb->load_slices[i].address + PAGE_SIZE) & ~0xFFF]) {// once allocated
			// ploginfo("freeing segment: %[x] %[x]", ppb->load_slices[i].address, ppb->load_slices[i].length);
			delete (byte*)ppb->paging[(ppb->load_slices[i].address) & ~0xFFF];
		}
		else while (ppb->load_slices[i].length) {
			// ploginfo("freeing segment: %[x] %[x]", ppb->load_slices[i].address, ppb->load_slices[i].length);
			delete (byte*)ppb->paging[(ppb->load_slices[i].address) & ~0xFFF];
			ppb->load_slices[i].address += 0x1000;
			ppb->load_slices[i].length -= minof(0x1000, ppb->load_slices[i].length);
		}
	}
	// Heap
	if (1) {

	}
	ppb->paging.~Paging();
	ppb->state = ProcessBlock::State::Invalid;
	Taskman::chain.Remove(ppb);
	#if defined(_DEBUG) && defined(_MCCA) && (_MCCA & 0xFF00) == 0x8600
	setFlags(flag);
	#endif
}

void Taskman::Idle() {
	loop HALT();
}
bool Taskman::Exit(ProcessBlock* p, stdsint exit_code)
{
	const auto pid = p->getID();
	auto parent_pid = p->parent_id;
	ProcessBlock* pparent = Taskman::Locate(parent_pid);
	stduint args[2] = { pid, _IMM(exit_code) };

	// Release Files First
	for0(i, numsof(p->pfiles)) if (p->pfiles[i]) {
		p->Close(i);// p->pfiles[i]->vfile->f_inode->ref_count--;
		p->pfiles[i] = 0;
	}

	p->exit_status = exit_code;
	// ploginfo("Process %u exited with code %[x]", pid, exit_code);

	// Handle Children (Reparenting to Task_Init)
	// Must do this BEFORE cleaning up 'p' to ensure memory safety
	auto nod = Taskman::chain.Root();
	while (nod) {
		auto taski = (ProcessBlock*)nod->offs;
		auto next_nod = nod->next;

		if (taski->pid && taski->parent_id == pid) {
			taski->parent_id = Task_Init; // Reparent

			auto init_pb = Taskman::Locate(Task_Init);
			using PBS = ProcessBlock::State;

			// If Init is waiting and this child is already a zombie
			if (init_pb->isWaiting() && taski->state == PBS::Hanging) {
				init_pb->main_thread->Unblock(ThreadBlock::BlockReason::BR_Waiting);

				stduint child_args[2] = { taski->pid, taski->exit_status };
				syssend(Task_Init, &child_args, sizeof(child_args));

				_Exit_Cleanup(taski->pid);
			}
		}
		nod = next_nod; // Safely move to the next node
	}

	// Handle Self (Notify Parent or become Zombie)
	using PBS = ProcessBlock::State;
	if (pparent && pparent->isWaiting()) {
		pparent->main_thread->Unblock(ThreadBlock::BlockReason::BR_Waiting);
		syssend(parent_pid, &args, sizeof(args));
		_Exit_Cleanup(pid); // Die completely
	}
	else if (!parent_pid) {
		_Exit_Cleanup(pid); // No parent (usually only Init or Kernel), die completely
	}
	else {
		if (p->main_thread) Taskman::DequeueReady(p->main_thread);
		p->state = PBS::Hanging; // Become a Zombie and wait for parent to call Wait()
	}

	return true;
}

stdsint Taskman::Wait(ProcessBlock* pb)
{
	const auto pid = pb->getID();
	stduint children = 0;
	for (auto nod = Taskman::chain.Root(); nod; nod = nod->next) {
		auto taski = (ProcessBlock*)nod->offs;
		if (taski->pid && taski->parent_id == pid) {
			children++;
			if (taski->state == ProcessBlock::State::Hanging) {
				stduint args[2] = { pid, taski->exit_status };
				stduint child_pid = taski->pid;
				_Exit_Cleanup(child_pid);
				children = child_pid;
				syssend(pid, args, sizeof(args));// return 0
				return child_pid;
			}
		}
	}
	if (children) {// no child is HANGING
		pb->main_thread->Block(ThreadBlock::BlockReason::BR_Waiting);
	}
	else { // no any child
		syssend(pid, &children, sizeof(children));// return 0
	}
	return ~_IMM0;
}

// #pragma GCC pop_options


//// ---- ---- SERVICE ---- ---- ////
#if (_MCCA & 0xFF00) == 0x8600 || (_MCCA & 0xFF00) == 0x1000
#if _MCCA == 0x8600
__attribute__((optimize("O0")))
#endif
void _Comment(R0) serv_task_loop()
{
	volatile stduint to_args[8] = {};// 8*4=32 bytes
	volatile stduint sig_type = 0, sig_src = 0, ret = 0;
	ProcessBlock* pb;
	ploginfo("Taskman Service Start");
	while (true) {
		switch (static_cast<TaskmanMsg>(sig_type))
		{
		case TaskmanMsg::TEST:
			// Nothing
			break;
		case TaskmanMsg::EXIT: // (pid, state)
			// ploginfo("Taskman exit: %u %u", to_args[0], to_args[1]);
			if (!to_args[0]) {
				plogerro("Try to exit Task_Kernel");
				break;
			}
			Taskman::Exit(Taskman::Locate(to_args[0]), to_args[1]);
			break;
		case TaskmanMsg::FORK: // (pid, cframe)
			// ploginfo("Taskman fork: %u", to_args[0]);
			if (1) {
				auto child_ppb = Taskman::CreateFork(Taskman::Locate(to_args[0]), (CallgateFrame*)to_args[1]);
				ret = child_ppb ? child_ppb->getID() : ~_IMM0;
			}
			syssend(sig_src, (void*)&ret, sizeof(ret));
			break;
		case TaskmanMsg::WAIT: // (pid) -> (*)
			// ploginfo("Taskman wait: %u %x", to_args[0], to_args[1]);
			Taskman::Wait(Taskman::Locate(to_args[0]));// send back { pid, taski->exit_status }
			break;
		case TaskmanMsg::EXEC:// (pid, &usr:fullpath, &usr:argstack, stacklen) -> 0(success)
			pb = Taskman::Exec(to_args[0], (rostr)to_args[1], (void*)to_args[2], to_args[3]);
			ret = pb ? pb->getID() : 0;
			syssend(sig_src, (void*)&ret, sizeof(ret));
			break;
		case TaskmanMsg::EXET:// (pid, &usr:fullpath, &usr:argstack, stacklen)
			pb = Taskman::Exet(to_args[0], (rostr)to_args[1], (void*)to_args[2], to_args[3]);
			if (!pb) {
				ret = ~_IMM0;
				syssend(sig_src, (void*)&ret, sizeof(ret));
			}
			break;

		default:
			plogerro("Bad TYPE %u in %s %s", sig_type, __FILE__, __FUNCIDEN__);
			break;
		}
		sysrecv(ANYPROC, (void*)to_args, byteof(to_args), (stduint*)&sig_type, (stduint*)&sig_src);
		// ploginfo("Taskman recv: %u %u", sig_type, sig_src);
	}
}

#endif
