// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include "../include/filesys.hpp"

extern Spinlock scheduler_lock;

#include <c/driver/keyboard.h>

#if (_MCCA & 0xFF00) == 0x8600
#include <c/proctrl/IAx86_64.msr.h>

#endif

// #pragma GCC push_options
// #pragma GCC optimize("O2")

#if _MCCA == 0x8632
stduint Taskman::getID() {
	uint32 a = 0, b = 0, c = 0, d = 0;
	_IO_CPUID(1, 0, &a, &b, &c, &d);
	const uint32 lapic_id = (b >> 24) & 0xFF; // xAPIC LAPIC ID (Initial APIC ID), consistent with table keys
	if (lapic_id < LAPIC_ID_MAP_SIZE) {
		const auto core_id = g_lapicid_to_coreid[lapic_id];
		if (core_id != CORE_ID_INVALID) return core_id;
	}
	return 0;
}




static void SendFixedIPIByCore(stduint core_id, uint8 vector) {
	if (core_id >= Taskman::PCU_CORES) return;
	auto percore = Taskman::PCU_CORES_PERCORE[core_id];
	if (!percore) return;
	const uint32 lapic_id = percore->lapic_id;
	if (lapic_id == CORE_ID_INVALID) return;

	if (IC.getType() == 2) {
		const uint64 icr = (uint64(lapic_id) << 32) | vector;
		setMSR(x86MSR::APIC_ICR_LOW, icr);
	}
	else if (IC.getType() == 1) {
		IC.WriteLAPIC(0x310, lapic_id << 24);
		IC.WriteLAPIC(0x300, vector);
	}
}

void Taskman::SendRescheduleIPI(stduint core_id) {
	SendFixedIPIByCore(core_id, IRQ_RESCHED_IPI);
}

void Taskman::SendWakeIPI(stduint core_id) {
	SendFixedIPIByCore(core_id, IRQ_WAKE_IPI);
}

void SendWakeAllApsIPI() {
	#if _SYS_MULTICORE
	if (Taskman::PCU_CORES <= 1) return;
	if (Taskman::getID() != 0) return;
	if (IC.getType() == 2) {
		setMSR(x86MSR::APIC_ICR_LOW, 0xC0000u | IRQ_WAKE_IPI);
	}
	else if (IC.getType() == 1) {
		IC.WriteLAPIC(0x300, 0xC0000u | IRQ_WAKE_IPI);
	}
	#endif
}
#else
stduint Taskman::getID() { return _TEMP 0; }
#endif

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
	KASSERT(pb->ref_count > 0);
	if (--pb->ref_count == 0) {
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
				++pb->ref_count;
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
				++pb->ref_count;
				return pb;
			}
			break;
		}
	}
	return nullptr;
}

ProcessBlock* ProcessBlock::AcquireActive(stduint tid) {
	SpinlockLocal guard(&scheduler_lock);
	for (auto nod = Taskman::thchain.Root(); nod; nod = nod->next) {
		auto th = cast<ThreadBlock*>(nod->offs);
		if (th->tid == tid) {
			if (th->parent_process && th->parent_process->state == ProcessBlock::State::Active) {
				ProcessBlock* pb = th->parent_process;
				++pb->ref_count;
				return pb;
			}
			break;
		}
	}
	return nullptr;
}

ProcessBlock* ProcessBlock::AcquireActiveByPID(stduint pid) {
	SpinlockLocal guard(&scheduler_lock);
	for (auto nod = Taskman::chain.Root(); nod; nod = nod->next) {
		auto pb = cast<ProcessBlock*>(nod->offs);
		if (pb->pid == pid) {
			if (pb->state == ProcessBlock::State::Active) {
				++pb->ref_count;
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
	tb->processor_id = CORE_ID_INVALID;
	tb->async_messages.func_free = free_async_msg;
	return tb;
}

void Taskman::DestroyThread(ThreadBlock* th) {
	if (!th) return;
	// [HYP-C] Verify thread is not still running on any CPU before freeing its stack
	while (true) {
		bool active = (th->state == ThreadBlock::State::Running);
		if (!active) {
			for (stduint _i = 0; _i < Taskman::PCU_CORES && _i < PCU_CORES_MAX; _i++) {
				if (current_thread(_i) == th || switching_out_threads(_i) == th) {
					active = true;
					break;
				}
			}
		}
		if (active) {
			Taskman::Schedule(true);
		} else {
			break;
		}
	}
	ProcessBlock* ppb = th->parent_process;
	if (ppb) {
		SpinlockLocal guard(&scheduler_lock);
		if (ppb->thread_list_head == th) {
			ppb->thread_list_head = th->process_thread_next;
		}
		else {
			ThreadBlock* prev = ppb->thread_list_head;
			while (prev && prev->process_thread_next != th) {
				prev = prev->process_thread_next;
			}
			if (prev) {
				prev->process_thread_next = th->process_thread_next;
			}
		}
		if (ppb->main_thread == th) {
			ppb->main_thread = ppb->thread_list_head;
		}
	}
	{
		SpinlockLocal guard(&scheduler_lock);
		Taskman::DequeueReady(th, false);
		Taskman::thchain.Remove(th);
	}

	if (th->stack_lineaddr == th->stack_levladdr) {
		free((byte*)th->stack_levladdr);
	}
	else if (ppb) {
		if (th->stack_lineaddr) {
			free((byte*)ppb->paging[_IMM(th->stack_lineaddr) & ~0xFFF]);
		}
		free((byte*)th->stack_levladdr);
	}
	free((byte*)th);
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
	syssend_async(Task_TaskMan, sliceof(para), _IMM(TaskmanMsg::EXIT));

	if (auto th = CurrentTB()) {
		th->Block(ThreadBlock::BlockReason::BR_Exiting);
	}
	Taskman::Schedule(true);
	printlog(_LOG_FATAL, "Taskman::ExitCurrent unreachable");
	return false;
}

// Resources
// - message
// - files open
// - pforms
// - heaps, stacks
// - paging
// - threads?
static void DetachProcessTTYMembership(ProcessBlock* ppb, stduint pid)
{
	if (!ppb) return;
	#if _GUI_ENABLE
	auto focus_tty = ppb->focus_tty.Lock();
	if (*focus_tty) {
		bool is_tty_valid = false;
		extern uni::Dchain vttys;
		extern Mutex console_waiters_mutex;
		MutexLocal vttys_guard(&console_waiters_mutex);
		for (auto nod = vttys.Root(); nod; nod = nod->next) {
			if (nod == *focus_tty) {
				is_tty_valid = true;
				break;
			}
		}

		if (is_tty_valid && (*focus_tty)->type) {
			auto pblock = (vtty_type_t*)(*focus_tty)->type;
			for (stdsint i = pblock->proc_group.Count() - 1; i >= 0; --i) {
				if (pblock->proc_group[i] == pid) {
					pblock->proc_group.Remove(i);
					break;
				}
			}


			// [HYP-A] Remaining group members will NOT receive SIGHUP when master closes
			if (pblock->master_pid == pid && pblock->proc_group.Count() > 0) {
				plogwarn("[HYP-A] DetachTTY: master PID%u closing, %u proc(s) orphaned (no SIGHUP sent)",
					pid, pblock->proc_group.Count());
				for (stdsint _j = 0; _j < (stdsint)pblock->proc_group.Count(); _j++)
					plogwarn("[HYP-A]   orphaned PID: %u", pblock->proc_group[_j]);
			}


			if (pblock->master_pid == pid || (pblock->proc_group.Count() == 0 && pblock->master_pid != 0)) {
				pblock->master_pid = 0;
			}
		}
		*focus_tty = nullptr;
	}
	#endif
}

static void _Exit_Cleanup(stduint pid)
{
	extern Spinlock scheduler_lock;
	auto ppb = Taskman::Locate(pid);
	if (!ppb) return;
	{
		SpinlockLocal guard(&scheduler_lock);
		if (ppb->state == ProcessBlock::State::Invalid) return;
		ppb->state = ProcessBlock::State::Invalid;
	}
	// Force-release mutexes and wake any waiters so they don't deadlock.
	// Directly clearing locked=0 without waking waiters leaves them
	// permanently stuck in the Mutex wait_queue.
	// Must be done outside scheduler_lock because Mutex::Release() -> Unblock()
	// acquires scheduler_lock internally.
	ppb->fileman.raw_mutex().Release();
	#if _GUI_ENABLE
	ppb->focus_tty.raw_mutex().Release();
	#endif

	{
		auto files = ppb->fileman.Lock();
	}

	// Hierarchical Process Tree: Unlink from parent
	auto locate_nolock = [](stduint target_pid) -> ProcessBlock* {
		for (auto nod = Taskman::chain.Root(); nod; nod = nod->next) {
			auto p = cast<ProcessBlock*>(nod->offs);
			if (p->pid == target_pid) return p;
		}
		return nullptr;
	};
	{
		SpinlockLocal guard(&scheduler_lock);
		ProcessBlock* pparent = locate_nolock(ppb->parent_id);
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
			SpinlockLocal guard(&scheduler_lock);
			ProcessBlock* pinit = locate_nolock(Task_Init);
			if (pinit) {
				child->sibling_next = pinit->child_list_head;
				pinit->child_list_head = child;
			}
		}
	}

	#if _GUI_ENABLE
	// 1. Queue GUI cleanup work without retaining ProcessBlock lifetime.
	QueueGuiCleanupForProcess(ppb);
	#endif

	// 2. Release TTY Binding
	DetachProcessTTYMembership(ppb, pid);

	// 3. Unlink all threads from IPC queues (but don't delete them yet)
	ThreadBlock* th_ptr = ppb->thread_list_head;
	while (th_ptr) {
		msg_cleanup_thread(th_ptr);
		th_ptr = th_ptr->process_thread_next;
	}

	// 4. Release Files
	stduint file_count = 0;
	{
		auto files = ppb->fileman.Lock();
		file_count = files->pfiles.Count();
	}
	for (stduint i = 0; i < file_count; i++) {
		{
			auto files = ppb->fileman.Lock();
			if (i >= files->pfiles.Count() || !files->pfiles[i]) {
				continue;
			}
		}
		ppb->Close(i);
	}
	{
		auto files = ppb->fileman.Lock();
		files->pfiles.Clear();
	}

	// Wait until all external ProcessBlock holders have released their refs
	// before tearing down paging-backed resources.
	while (ppb->ref_count > 1) {
		Taskman::Schedule(true);
	}

	// 5. Final Destruction: Release Thread blocks and stacks
	while (ppb->thread_list_head) {
		Taskman::DestroyThread(ppb->thread_list_head);
	}
	ppb->main_thread = nullptr;

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
			if (vma.vm_type == VMA_FILE && vma.vfile && (vma.vm_flags & PGPROP_writable)) {
				for (stduint addr = vma.vm_start; addr < vma.vm_end; addr += 0x1000) {
					void* phys_addr = ppb->paging[addr];
					if (phys_addr != (void*)~_IMM0) {
						stduint file_offset = addr - vma.vm_start + vma.file_offset;
						if (file_offset < vma.vfile->f_inode->i_size) {
							stduint write_len = minof(_IMM(0x1000), vma.vfile->f_inode->i_size - file_offset);
							vma.vfile->f_pos = file_offset;
							Filesys::Write(vma.vfile, (const void*)mglb(phys_addr), write_len);
						}
					}
				}
			}
			for (stduint addr = vma.vm_start; addr < vma.vm_end; addr += 0x1000) {
				void* phys_addr = ppb->paging[addr];
				if (phys_addr != (void*)~_IMM0) {
					free(phys_addr);
				}
			}
			if (vma.vm_type == VMA_FILE && vma.vfile) {
				if (vma.vfile->f_inode) {
					vma.vfile->f_inode->ref_count--;
				}
				Filesys::Close(vma.vfile);
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

static bool DeliverWaitResultAtomically(ProcessBlock* pparent, stduint child_pid, stduint exit_status) {
	if (!pparent || !pparent->main_thread) return false;
	ProcessBlock* taskman_pb = Taskman::Locate(Task_TaskMan);
	if (!taskman_pb || !taskman_pb->main_thread) return false;

	ThreadBlock* parent_th = pparent->main_thread;
	ThreadBlock* taskman_th = taskman_pb->main_thread;

	extern Spinlock comm_lock;
	SpinlockLocal guard(&comm_lock);

	if (!(parent_th->block_reason & ThreadBlock::BlockReason::BR_Waiting)) {
		return false;
	}
	stduint args[2] = { child_pid, exit_status };

	if (parent_th->block_reason & ThreadBlock::BlockReason::BR_RecvMsg) {
		if (!(parent_th->recv_fo_whom == taskman_th ||
			(stduint)parent_th->recv_fo_whom == ANYPROC)) {
			return false;
		}

		auto msg_to = parent_th->unsolved_msg_from_kernel
			? parent_th->unsolved_msg
			: (CommMsg*)SeekAddress(parent_th->parent_process, _IMM(parent_th->unsolved_msg), false);
		if (!msg_to) return false;

		stduint leng = minof((stduint)sizeof(args), msg_to->data.length);
		if (leng) {
			MccaMemCopyP(
				(void*)msg_to->data.address, parent_th->parent_process, parent_th->unsolved_msg_from_kernel,
				args, nullptr, true,
				leng);
		}
		msg_to->type = 0;
		msg_to->src = taskman_th->tid;

		parent_th->unsolved_msg = nullptr;
		parent_th->recv_fo_whom = nullptr;
		pparent->wait_for_pid = 0;
		parent_th->Unblock(ThreadBlock::BlockReason::BR_Waiting);
		parent_th->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
		return true;
	}

	if (parent_th->async_messages.Count() >= LIMIT_THREAD_AMSG) return false;
	AsyncCommMsg* amsg = new AsyncCommMsg();
	if (!amsg) return false;
	byte* payload = new byte[sizeof(args)];
	if (!payload) {
		delete amsg;
		return false;
	}
	MemCopyN(payload, args, sizeof(args));
	amsg->msg.data.address = (stduint)payload;
	amsg->msg.data.length = sizeof(args);
	amsg->msg.type = 0;
	amsg->msg.src = taskman_th->tid;
	parent_th->async_messages.Append(amsg);
	pparent->wait_for_pid = 0;
	parent_th->Unblock(ThreadBlock::BlockReason::BR_Waiting);
	return true;
}

bool Taskman::Exit(ProcessBlock* p, stdsint exit_code)
{
	extern Spinlock scheduler_lock;
	if (!p || p->state != ProcessBlock::State::Active) return false;
	p->state = ProcessBlock::State::Expiring; // Mark as terminating immediately

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
			bool wake_send = false;
			bool wake_recv = false;
			
			{
				extern Spinlock comm_lock;
				SpinlockLocal guard(&comm_lock);

				// 1. If this system task is blocked while sending to us, sever pointers under comm_lock
				if ((th->block_reason & ThreadBlock::BlockReason::BR_SendMsg) &&
					th->send_to_whom && th->send_to_whom != (ThreadBlock*)INTRUPT && th->send_to_whom->parent_process == p) {
					th->send_to_whom = nullptr;
					th->queue_send_queuenext = nullptr;
					th->unsolved_msg = nullptr;
					wake_send = true;
				}

				// 2. If this system task is blocked while receiving from us, sever pointers under comm_lock
				if ((th->block_reason & ThreadBlock::BlockReason::BR_RecvMsg) &&
					th->recv_fo_whom && th->recv_fo_whom != (ThreadBlock*)INTRUPT && th->recv_fo_whom->parent_process == p) {
					th->recv_fo_whom = nullptr;
					th->unsolved_msg = nullptr;
					wake_recv = true;
				}
			}
			if (wake_send) {
				th->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
			}
			if (wake_recv) {
				th->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
			}
		}
	}

	// Wakeup any senders blocked by this process's main thread queue
	if (p->main_thread) {
		Queue<ThreadBlock*> wake_list;
		{
			extern Spinlock comm_lock;
			SpinlockLocal guard(&comm_lock);

			ThreadBlock* sender = p->main_thread->queue_send_queuehead;
			while (sender) {
				ThreadBlock* next = sender->queue_send_queuenext;
				
				// Sever dangling pointers under comm_lock, wake outside.
				sender->send_to_whom = nullptr;
				sender->queue_send_queuenext = nullptr;
				sender->unsolved_msg = nullptr;
				wake_list.Enqueue(sender);
				sender = next;
			}
			p->main_thread->queue_send_queuehead = nullptr;
		}
		while (!wake_list.isEmpty()) {
			ThreadBlock* sender = nullptr;
			wake_list.Dequeue(sender);
			if (sender) {
				sender->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
			}
		}
	}

	p->exit_status = exit_code;

	// Handle Remaining Children (Reparent all children to Task_Init)
	auto locate_nolock = [](stduint target_pid) -> ProcessBlock* {
		for (auto nod = Taskman::chain.Root(); nod; nod = nod->next) {
			auto task = cast<ProcessBlock*>(nod->offs);
			if (task->pid == target_pid) return task;
		}
		return nullptr;
	};
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
		SpinlockLocal guard(&scheduler_lock);
		ProcessBlock* pinit = locate_nolock(Task_Init);
		if (pinit) {
			child->sibling_next = pinit->child_list_head;
			pinit->child_list_head = child;
		}
	}

	// Handle Self (Notify Parent or become Zombie)
	using PBS = ProcessBlock::State;
	bool parent_active = pparent && pparent->state == PBS::Active;
	bool parent_is_nonwait_kernel_owner = parent_pid == Task_Kernel && pid != Task_Init;

	if (parent_active && pparent->isWaiting() && (pparent->wait_for_pid == 0 || pparent->wait_for_pid == pid)) {
		if (DeliverWaitResultAtomically(pparent, pid, _IMM(exit_code))) {
			_Exit_Cleanup(pid); // Die completely
		}
		else {
			p->state = PBS::Hanging; // Keep zombie if the WAIT reply cannot be delivered atomically
		}
	}
	else if (parent_is_nonwait_kernel_owner) {
		_Exit_Cleanup(pid); // Kernel-owned helper processes are not reaped through wait()
	}
	else if (!parent_active) {
		_Exit_Cleanup(pid); // Parent is gone or exiting, die completely
	}
	else {
		DetachProcessTTYMembership(p, pid);
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
				
				syssend_async(pid, args, sizeof(args));

				return child_pid;
			}
		}
	}
	if (children) {// no child is HANGING
		pb->wait_for_pid = target_pid;
		pb->main_thread->Block(ThreadBlock::BlockReason::BR_Waiting);
	}
	else { // no any child
		syssend_async(pid, &children, sizeof(children));
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
			syssend_async(sig_src, (void*)&ret, sizeof(ret));
			break;
		case TaskmanMsg::WAIT: // (parent_pid, target_pid) -> (*)
			// ploginfo("Taskman wait: %u %x", to_args[0], to_args[1]);
			Taskman::Wait(Taskman::Locate(to_args[0]), to_args[1]);// send back { pid, taski->exit_status }
			break;
		case TaskmanMsg::EXEC:// (pid, &usr:fullpath, &usr:argv, &usr:envp) -> 0(success)
			pb = Taskman::Exec(to_args[0], (rostr)to_args[1], (char**)to_args[2], (char**)to_args[3]);
			ret = pb ? pb->getID() : 0;
			syssend_async(sig_src, (void*)&ret, sizeof(ret));
			break;
		case TaskmanMsg::EXET:// (pid, &usr:fullpath, &usr:argv, &usr:envp)
			pb = Taskman::Exet(to_args[0], (rostr)to_args[1], (char**)to_args[2], (char**)to_args[3]);
			if (!pb || sig_src != to_args[0]) {
				ret = pb ? 0 : ~_IMM0;
				syssend_async(sig_src, (void*)&ret, sizeof(ret));
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

extern "C" void* kernel_prefault_page(ProcessBlock* pb, stduint addr) {
	if (!pb) return nullptr;
	addr &= ~_IMM(0xFFF);
	void* phy_page = mempool.allocate(0x1000, 12);
	if (phy_page == (void*)~_IMM0) return nullptr;
	MemSet((void*)mglb(phy_page), 0, 0x1000);
	SpinlockLocal guard(&pb->vma_lock);
	for (stduint i = 0; i < pb->vmas.Count(); i++) {
		const auto& vma = pb->vmas[i];
		if (addr >= vma.vm_start && addr < vma.vm_end) {
			if (vma.vm_type == VMA_FILE && vma.vfile) {
				stduint offset = addr - vma.vm_start + vma.file_offset;
				if (vma.vfile->f_inode && offset < vma.vfile->f_inode->i_size) {
					stduint read_len = minof(_IMM(0x1000), vma.vfile->f_inode->i_size - offset);
					vma.vfile->f_pos = offset;
					Filesys::Read(vma.vfile, (void*)mglb(phy_page), read_len);
				}
			}
			break;
		}
	}
	pb->paging.Map(addr, (stduint)phy_page, 0x1000, PAGESIZE_4KB, PGPROP_present | PGPROP_writable | PGPROP_user_access);
	RefreshVirtualAddress(addr);
	return phy_page;
}

#endif
