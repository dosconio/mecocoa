// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include "../include/filesys.hpp"

extern Spinlock scheduler_lock;

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
	if ((state_rupt = (byte)cast<REG_FLAG_t>(flags).IF)) {
		IC.enInterrupt(false);
	}
	#endif
	while (__atomic_exchange_n(&this->locked, 1, __ATOMIC_ACQUIRE) != 0) {
		#if (_MCCA & 0xFF00) == 0x8600
		asm volatile("pause" ::: "memory");
		#endif
	}
	this->cpu_id = (stdsint)Taskman::getID();
	return (bool)state_rupt;
}
void Spinlock::Release(bool old_if) {
	this->cpu_id = -1;
	__atomic_store_n(&this->locked, 0, __ATOMIC_RELEASE);
	if (old_if) IC.enInterrupt();
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
		if (old_if) IC.enInterrupt();
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

void Semaphore::Acquire() {
	bool old_if = this->guard.Acquire(); // Disable interrupts and acquire lock

	if (this->value > 0) {
		this->value--;
		this->guard.Release(old_if); // Resource acquired, unlock and return
	}
	else {
		ThreadBlock* crt = Taskman::current_thread[Taskman::getID()];
		if (!this->wait_queue.isFull()) {
			// Transition thread to pending state with waiting block reason
			// State and BlockReason verified strictly via taskman.hpp
			crt->state = ThreadBlock::State::Pended;
			crt->block_reason = ThreadBlock::BlockReason::BR_Waiting;
			this->wait_queue.Enqueue(crt);

			// Yield CPU: puts thread to sleep, releases spinlock, and switches context
			Taskman::SleepAndRelease(&this->guard);
		}
		if (old_if) IC.enInterrupt(); // Re-enable interrupts if they were originally enabled
	}
}

void Semaphore::Release() {
	bool old_if = this->guard.Acquire(); // Disable interrupts and acquire lock

	if (!this->wait_queue.isEmpty()) {
		ThreadBlock* wakeup_tb = nullptr;
		this->wait_queue.Dequeue(wakeup_tb);
		if (wakeup_tb) {
			// Wake up the waiting thread by moving it back to ready queue
			wakeup_tb->Unblock(ThreadBlock::BlockReason::BR_Waiting);
		}
	}
	else {
		this->value++; // Increment resource counter if no threads are pending
	}

	this->guard.Release(old_if); // Release lock and restore interrupt state
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

void ProcessBlock::Release(ProcessBlock* pb) {
	if (!pb) return;
	if (__atomic_sub_fetch(&pb->ref_count, 1, __ATOMIC_SEQ_CST) == 0) {
		ploginfo("ProcessBlock::Release() [%d] [%p]", pb->getID(), pb);
		pb->~ProcessBlock();
		mempool.deallocate(pb);
	}
}

ProcessBlock* ProcessBlock::Acquire(stduint tid) {
	SpinlockLocal guard(&scheduler_lock);
	for (auto nod = Taskman::thchain.Root(); nod; nod = nod->next) {
		auto th = cast<ThreadBlock*>(nod->offs);
		if (th->tid == tid) {
			if (th->parent_process && th->parent_process->state != ProcessBlock::State::Invalid) {
				ProcessBlock* pb = th->parent_process;
				__atomic_add_fetch(&pb->ref_count, 1, __ATOMIC_SEQ_CST);
				return pb;
			}
			break;
		}
	}
	return nullptr;
}

ProcessBlock* ProcessBlock::AcquireByPID(stduint pid) {
	SpinlockLocal guard(&scheduler_lock);
	for (auto nod = Taskman::chain.Root(); nod; nod = nod->next) {
		auto pb = cast<ProcessBlock*>(nod->offs);
		if (pb->pid == pid) {
			if (pb->state != ProcessBlock::State::Invalid) {
				__atomic_add_fetch(&pb->ref_count, 1, __ATOMIC_SEQ_CST);
				return pb;
			}
			break;
		}
	}
	return nullptr;
}

extern void free_async_msg(pureptr_t ptr);
auto Taskman::AllocateThread() -> ThreadBlock* {
	auto tb = (ThreadBlock*)mempool.allocate(sizeof(ThreadBlock), 4);
	if (!tb) {
		plogerro("Taskman::AllocateThread() failed");
		return nullptr;
	}
	MemSet(tb, 0, sizeof(ThreadBlock));
	new (tb) ThreadBlock();
	tb->async_messages.func_free = free_async_msg;
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
// - pforms
// - heaps, stacks
// - paging
// - threads?
static void _Exit_Cleanup(stduint pid)
{
	auto ppb = Taskman::Locate(pid);
	if (!ppb || ppb->state == ProcessBlock::State::Invalid) return;
	ppb->state = ProcessBlock::State::Invalid;

	ppb->sys_lock.Acquire();
    ppb->sys_lock.Release();

	// Hierarchical Process Tree: Unlink from parent
	extern Spinlock scheduler_lock;
	{
		SpinlockLocal guard(&scheduler_lock);
		ProcessBlock* pparent = nullptr;
		for (auto nod = Taskman::chain.Root(); nod; nod = nod->next) {
			auto p = cast<ProcessBlock*>(nod->offs);
			if (p->pid == ppb->parent_id) { pparent = p; break; }
		}
		if (pparent) {
			if (pparent->child_list_head == ppb) {
				pparent->child_list_head = ppb->sibling_next;
			} else {
				ProcessBlock* prev = pparent->child_list_head;
				while (prev && prev->sibling_next != ppb) prev = prev->sibling_next;
				if (prev) prev->sibling_next = ppb->sibling_next;
			}
		}
	}

	ploginfo("cleaning %u, now exist %u tasks", pid, Taskman::chain.Count());

	// Hierarchical Process Tree: Deep Reap children
	while (ppb->child_list_head) {
		ProcessBlock* child = ppb->child_list_head;
		
		// Unlink from current parent first to avoid infinite loop
		{
			SpinlockLocal guard(&scheduler_lock);
			ppb->child_list_head = child->sibling_next;
			child->sibling_next = nullptr;
		}

		if (child->state == ProcessBlock::State::Hanging) {
			_Exit_Cleanup(child->pid);
		}
		else {
			child->parent_id = Task_Init;
			ProcessBlock* pinit = Taskman::Locate(Task_Init);
			if (pinit) {
				SpinlockLocal guard(&scheduler_lock);
				child->sibling_next = pinit->child_list_head;
				pinit->child_list_head = child;
			}
		}
	}

	#if _GUI_ENABLE
	// 1. Release GUI Resources First (while threads are still valid for unblocking)
	Global_CleanProcessForms(ppb);

	// 2. Release TTY Binding
	if (ppb->focus_tty) {
		bool is_tty_valid = false;
		extern uni::Dchain vttys;
		for (auto nod = vttys.Root(); nod; nod = nod->next) {
			if (nod == ppb->focus_tty) {
				is_tty_valid = true;
				break;
			}
		}

		if (is_tty_valid && ppb->focus_tty->type) {
			auto pblock = (vtty_type_t*)ppb->focus_tty->type;
			for (stdsint i = pblock->proc_group.Count() - 1; i >= 0; --i) {
				if (pblock->proc_group[i] == pid) {
					pblock->proc_group.Remove(i);
					break;
				}
			}
			if (pblock->master_pid == pid || (pblock->proc_group.Count() == 0 && pblock->master_pid != 0)) {
				pblock->master_pid = 0; 
			}
		}
		ppb->focus_tty = nullptr;
	}
	#endif

	// 3. Unlink all threads from IPC queues (but don't delete them yet)
	ThreadBlock* th_ptr = ppb->thread_list_head;
	while (th_ptr) {
		msg_cleanup_thread(th_ptr);
		th_ptr = th_ptr->process_thread_next;
	}

	// 4. Release Files
	for (stduint i = 0; i < ppb->pfiles.Count(); i++) {
		if (ppb->pfiles[i]) {
			ppb->Close(i);
		}
	}
	ppb->pfiles.Clear();

	// 5. Final Destruction: Release Thread blocks and stacks
	ThreadBlock* th = ppb->thread_list_head;
	while (th) {
		ThreadBlock* next_th = th->process_thread_next;
		{
			SpinlockLocal guard(&scheduler_lock);
			Taskman::DequeueReady(th, false);
			Taskman::thchain.Remove(th);
		}
		if (th->stack_lineaddr == th->stack_levladdr) {
			free((byte*)th->stack_levladdr);
		}
		else {
			free((byte*)ppb->paging[_IMM(th->stack_lineaddr) & ~0xFFF]);
			free((byte*)th->stack_levladdr);
		}
		free((byte*)th);
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
			free((byte*)ppb->paging[(ppb->load_slices[i].address) & ~0xFFF]);
		}
		else while (ppb->load_slices[i].length) {
			// ploginfo("freeing segment: %[x] %[x]", ppb->load_slices[i].address, ppb->load_slices[i].length);
			free((byte*)ppb->paging[(ppb->load_slices[i].address) & ~0xFFF]);
			ppb->load_slices[i].address += 0x1000;
			ppb->load_slices[i].length -= minof(0x1000, ppb->load_slices[i].length);
		}
	}
	// Heap
	if (1) {
		for (stduint i = 0; i < ppb->vmas.Count(); i++) {
			const auto& vma = ppb->vmas[i];
			for (stduint addr = vma.vm_start; addr < vma.vm_end; addr += 0x1000) {
				void* phys_addr = ppb->paging[addr];
				if (phys_addr != (void*)~_IMM0) {
					free(phys_addr);
				}
			}
		}
		ppb->vmas.Clear();
	}
	// Release Heap/Paging
	if (!ppb->ring) {
		ppb->paging.root_level_page = nullptr;
	}

	{
		SpinlockLocal guard(&scheduler_lock);
		Taskman::chain.Remove(ppb);
	}

	if (ppb->ref_count > 1) {
		plogwarn("ref_count: %d, delay destruction", ppb->ref_count - 1);
	}
	ProcessBlock::Release(ppb);

}

void Taskman::Idle() {
	loop HALT();
}
bool Taskman::Exit(ProcessBlock* p, stdsint exit_code)
{
	extern Spinlock scheduler_lock;
	if (!p || p->state != ProcessBlock::State::Active) return false;
	p->state = ProcessBlock::State::Hanging; // Mark as terminating immediately

	const auto pid = p->getID();
	auto parent_pid = p->parent_id;
	ProcessBlock* pparent = Taskman::Locate(parent_pid);
	stduint args[2] = { pid, _IMM(exit_code) };

	ploginfo("Process %u exited with code %[x]", pid, exit_code);

	// [Canceled] Hierarchical Process Tree: Recursive Kill

	// Check all system tasks to see if they are blocked by the exiting process
	for (stduint i = 0; i < TaskCount; i++) {
		ProcessBlock* pb_sys = Taskman::Locate(i);
		if (pb_sys && pb_sys->main_thread) {
			ThreadBlock* th = pb_sys->main_thread;
			
			extern Spinlock comm_lock;
			SpinlockLocal guard(&comm_lock);

			// 1. If this system task is blocked while sending to us, unblock it and clear pointers
			if ((th->block_reason & ThreadBlock::BlockReason::BR_SendMsg) &&
				th->send_to_whom && th->send_to_whom->parent_process == p) {
				th->send_to_whom = nullptr;
				th->queue_send_queuenext = nullptr;
				th->unsolved_msg = nullptr;
				th->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
			}

			// 2. If this system task is blocked while receiving from us, unblock it
			if ((th->block_reason & ThreadBlock::BlockReason::BR_RecvMsg) &&
				th->recv_fo_whom && th->recv_fo_whom->parent_process == p) {
				th->recv_fo_whom = nullptr;
				th->unsolved_msg = nullptr;
				th->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
			}
		}
	}

	// Wakeup any senders blocked by this process's main thread queue
	if (p->main_thread) {
		extern Spinlock comm_lock;
		SpinlockLocal guard(&comm_lock);

		ThreadBlock* sender = p->main_thread->queue_send_queuehead;
		while (sender) {
			ThreadBlock* next = sender->queue_send_queuenext;
			
			// Sever dangling pointers
			sender->send_to_whom = nullptr;
			sender->queue_send_queuenext = nullptr;
			sender->unsolved_msg = nullptr;
			
			sender->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
			sender = next;
		}
		p->main_thread->queue_send_queuehead = nullptr;
	}

	p->exit_status = exit_code;

	// Handle Remaining Children (Reparent all children to Task_Init)
	while (p->child_list_head) {
		ProcessBlock* child = p->child_list_head;

		// Unlink the child from current parent's child list first
		{
			SpinlockLocal guard(&scheduler_lock);
			p->child_list_head = child->sibling_next;
			child->sibling_next = nullptr;
		}

		// Reparent the child to Task_Init (sole source of truth from taskman.hpp)
		child->parent_id = Task_Init;

		// Attach the child to Task_Init's child list
		ProcessBlock* pinit = Taskman::Locate(Task_Init);
		if (pinit) {
			SpinlockLocal guard(&scheduler_lock);
			child->sibling_next = pinit->child_list_head;
			pinit->child_list_head = child;
		}
	}

	// Handle Self (Notify Parent or become Zombie)
	using PBS = ProcessBlock::State;
	bool parent_active = pparent && pparent->state == PBS::Active;

	if (parent_active && pparent->isWaiting() && (pparent->wait_for_pid == 0 || pparent->wait_for_pid == pid)) {
		pparent->main_thread->Unblock(ThreadBlock::BlockReason::BR_Waiting);
		pparent->wait_for_pid = 0; // Reset after unblocking

		syssend(parent_pid, &args, sizeof(args));

		_Exit_Cleanup(pid); // Die completely
	}
	else if (!parent_active) {
		_Exit_Cleanup(pid); // Parent is gone or exiting, die completely
	}
	else {
		p->state = PBS::Hanging; // Become a Zombie and wait for parent to call Wait()
	}

	return true;
}

stdsint Taskman::Wait(ProcessBlock* pb, stduint target_pid)
{
	if (!pb) return ~_IMM0;
	const auto pid = pb->getID();
	stduint children = 0;
	for (auto nod = Taskman::chain.Root(); nod; nod = nod->next) {
		auto taski = (ProcessBlock*)nod->offs;
		if (taski->pid && taski->parent_id == pid) {
			// If target_pid is non-zero, skip other children
			if (target_pid != 0 && taski->pid != target_pid) {
				continue;
			}
			
			children++;
			if (taski->state == ProcessBlock::State::Hanging) {
				stduint args[2] = { taski->pid, taski->exit_status };
				stduint child_pid = taski->pid;
				_Exit_Cleanup(child_pid);
				// children = child_pid;
				
				syssend(pid, args, sizeof(args));

				return child_pid;
			}
		}
	}
	if (children) {// no child is HANGING
		pb->wait_for_pid = target_pid;
		pb->main_thread->Block(ThreadBlock::BlockReason::BR_Waiting);
	}
	else { // no any child
		syssend(pid, &children, sizeof(children));
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
			_Exit_Cleanup(to_args[0]); // Force immediate cleanup to satisfy synchronous IPC wait
			ret = 0;

			syssend(sig_src, (void*)&ret, sizeof(ret));
			break;
		case TaskmanMsg::FORK: // (pid, cframe)
			// ploginfo("Taskman fork: %u", to_args[0]);
			if (1) {
				auto child_ppb = Taskman::CreateFork(Taskman::Locate(to_args[0]), (CallgateFrame*)to_args[1]);
				ret = child_ppb ? child_ppb->getID() : ~_IMM0;
			}
			syssend(sig_src, (void*)&ret, sizeof(ret));
			break;
		case TaskmanMsg::WAIT: // (parent_pid, target_pid) -> (*)
			// ploginfo("Taskman wait: %u %x", to_args[0], to_args[1]);
			Taskman::Wait(Taskman::Locate(to_args[0]), to_args[1]);// send back { pid, taski->exit_status }
			break;
		case TaskmanMsg::EXEC:// (pid, &usr:fullpath, &usr:argv, &usr:envp) -> 0(success)
			pb = Taskman::Exec(to_args[0], (rostr)to_args[1], (char**)to_args[2], (char**)to_args[3]);
			ret = pb ? pb->getID() : 0;
			syssend(sig_src, (void*)&ret, sizeof(ret));
			break;
		case TaskmanMsg::EXET:// (pid, &usr:fullpath, &usr:argv, &usr:envp)
			pb = Taskman::Exet(to_args[0], (rostr)to_args[1], (char**)to_args[2], (char**)to_args[3]);
			if (!pb || sig_src != to_args[0]) {
				ret = pb ? 0 : ~_IMM0;
				syssend(sig_src, (void*)&ret, sizeof(ret));
			}
			break;

		default:
			plogerro("Bad TYPE %u in %s %s", sig_type, __FILE__, __FUNCIDEN__);
			break;
		}
		// plogwarn("TRY TO");
		sysrecv(ANYPROC, (void*)to_args, byteof(to_args), (stduint*)&sig_type, (stduint*)&sig_src);
		// ploginfo("Taskman recv: %u %u", sig_type, sig_src);
	}
}

#endif
