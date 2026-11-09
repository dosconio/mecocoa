// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include "../include/filesys.hpp"

#if CONFIG_ENABLE_MMU
namespace uni { void UnpinExecutableImage(ProcessBlock* process); }
#endif

extern Spinlock scheduler_lock;

#include <c/driver/keyboard.h>

#if (_MCCA & 0xFF00) == 0x8600
#include <c/proctrl/IAx86_64.msr.h>

#endif

// #pragma GCC push_options
// #pragma GCC optimize("O2")

#if _MCCA == 0x8632 || _MCCA == 0x8664
namespace {
	enum class CurrentCoreIdMethod : byte { Cpuid, Rdtscp, Rdpid };
	CurrentCoreIdMethod current_core_id_method = CurrentCoreIdMethod::Cpuid;
	uint32 current_lapic_id_cpuid_leaf = 0;
	// APs publish this only after binding their local ID; until then only BSP runs kernel code.
	uni::Atomic<byte> current_core_id_smp = 0;
	constexpr uint32 Ia32TscAuxMsr = 0xC0000103u; // Intel SDM Vol. 4, IA32_TSC_AUX; bits 63:32 are reserved.

	uint32 ReadCurrentLapicId(uint32 leaf) {
		uint32 a = 0, b = 0, c = 0, d = 0;
		_IO_CPUID(leaf, 0, &a, &b, &c, &d);
		if (leaf == 1) return (b >> 24) & 0xFF;
		return leaf == 0x8000001Eu ? a : d;
	}
}

uint32 GetCurrentLapicId() {
	// BSP selects the leaf once; later AP initialization and legacy CPUs execute only that leaf.
	if (current_lapic_id_cpuid_leaf) return ReadCurrentLapicId(current_lapic_id_cpuid_leaf);
	uint32 a = 0, b = 0, c = 0, d = 0;
	// Intel SDM Vol. 2A, CPUID leaves 1FH/0BH: EDX contains the full x2APIC ID.
	_IO_CPUID(0, 0, &a, &b, &c, &d);
	const uint32 maximum_leaf = a;
	if (maximum_leaf >= 0x1Fu) {
		_IO_CPUID(0x1F, 0, &a, &b, &c, &d);
		if ((b & 0xFFFFu) && (c & 0xFF00u)) {
			current_lapic_id_cpuid_leaf = 0x1F;
			return d;
		}
	}
	if (maximum_leaf >= 0x0Bu) {
		_IO_CPUID(0x0B, 0, &a, &b, &c, &d);
		if ((b & 0xFFFFu) && (c & 0xFF00u)) {
			current_lapic_id_cpuid_leaf = 0x0B;
			return d;
		}
	}
	// AMD APM Vol. 3, CPUID: Fn8000_0026 EDX / Fn8000_001E EAX report the extended APIC ID.
	_IO_CPUID(0x80000000u, 0, &a, &b, &c, &d);
	const uint32 maximum_extended_leaf = a;
	if (maximum_extended_leaf >= 0x80000026u) {
		_IO_CPUID(0x80000026u, 0, &a, &b, &c, &d);
		if ((b & 0xFFFFu) && (c & 0xFF00u)) {
			current_lapic_id_cpuid_leaf = 0x80000026u;
			return d;
		}
	}
	if (maximum_extended_leaf >= 0x8000001Eu) {
		_IO_CPUID(0x80000001u, 0, &a, &b, &c, &d);
		if (c & (1u << 22)) { // AMD TopologyExtensions gates Fn8000_001E.
			_IO_CPUID(0x8000001Eu, 0, &a, &b, &c, &d);
			current_lapic_id_cpuid_leaf = 0x8000001Eu;
			return a;
		}
	}
	_IO_CPUID(1, 0, &a, &b, &c, &d);
	current_lapic_id_cpuid_leaf = 1;
	return (b >> 24) & 0xFF; // Legacy Initial APIC ID is only eight bits wide.
}

[[noreturn]] static void FailCurrentCoreId(uint32 lapic_id) {
	asm volatile("cli" ::: "memory");
	printlog(_LOG_FATAL, "[COREMAN] Unmapped current LAPIC ID %[x]", stduint(lapic_id));
	loop HALT();
}

void Taskman::InitializeCurrentCoreId(stduint core_id) {
	const stduint core_count = core_id ?
		g_core_id_map_count.load(uni::MemoryOrder_Acquire) : PCU_CORES;
	const uint32 lapic_id = GetCurrentLapicId();
	if (core_id >= core_count || !PCU_CORES_PERCORE[core_id] ||
		PCU_CORES_PERCORE[core_id]->lapic_id != lapic_id) FailCurrentCoreId(lapic_id);
	if (!core_id) {
		uint32 a = 0, b = 0, c = 0, d = 0;
		// Intel SDM Vol. 2, CPUID: 07H.0:ECX[22] selects RDPID; 80000001H:EDX[27] selects RDTSCP.
		_IO_CPUID(0, 0, &a, &b, &c, &d);
		if (a >= 7) {
			_IO_CPUID(7, 0, &a, &b, &c, &d);
			if (c & (1u << 22)) current_core_id_method = CurrentCoreIdMethod::Rdpid;
		}
		if (current_core_id_method == CurrentCoreIdMethod::Cpuid) {
			_IO_CPUID(0x80000000u, 0, &a, &b, &c, &d);
			if (a >= 0x80000001u) {
				_IO_CPUID(0x80000001u, 0, &a, &b, &c, &d);
				if (d & (1u << 27)) current_core_id_method = CurrentCoreIdMethod::Rdtscp;
			}
		}
	}
	if (current_core_id_method != CurrentCoreIdMethod::Cpuid) {
		// Store ID + 1 so an uninitialized TSC_AUX value of zero cannot silently identify BSP.
		asm volatile("wrmsr" : : "c"(Ia32TscAuxMsr), "a"(uint32(core_id) + 1u), "d"(0u) : "memory");
	}
	// BSP published the immutable access method with the core map; APs initialize TSC_AUX before this release.
	if (core_id) current_core_id_smp.store(1, uni::MemoryOrder_Release);
}

stduint Taskman::getID() {
	if (!current_core_id_smp.load(uni::MemoryOrder_Acquire)) return 0;
	const uint32 core_count = g_core_id_map_count.load(uni::MemoryOrder_Acquire);
	if (current_core_id_method != CurrentCoreIdMethod::Cpuid) {
		uint32 core_id;
		if (current_core_id_method == CurrentCoreIdMethod::Rdpid) {
			stduint local_id;
			asm volatile("rdpid %0" : "=r"(local_id));
			core_id = uint32(local_id) - 1u;
		}
		else {
			uint32 low, high, local_id;
			asm volatile("rdtscp" : "=a"(low), "=d"(high), "=c"(local_id));
			core_id = local_id - 1u;
		}
		if (core_id < core_count) return core_id;
		FailCurrentCoreId(ReadCurrentLapicId(current_lapic_id_cpuid_leaf));
	}
	// Compatibility path for CPUs without TSC_AUX: one CPUID using the leaf cached during BSP initialization.
	const uint32 lapic_id = ReadCurrentLapicId(current_lapic_id_cpuid_leaf);
	if (lapic_id == CORE_ID_INVALID) FailCurrentCoreId(lapic_id);
	if (lapic_id < LAPIC_ID_MAP_SIZE) {
		const auto core_id = g_lapicid_to_coreid[lapic_id];
		if (core_id < core_count) return core_id;
	}
	else {
		for0(i, core_count) {
			const auto percore = PCU_CORES_PERCORE[i];
			if (percore && percore->lapic_id == lapic_id) return i;
		}
	}
	FailCurrentCoreId(lapic_id);
}
#else
stduint Taskman::getID() { return _TEMP 0; }
#endif

#if _MCCA == 0x8632
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

void Taskman::SendTLBShootdownIPI(stduint core_id) {
	SendFixedIPIByCore(core_id, IRQ_TLB_SHOOTDOWN_IPI);
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
#endif

namespace uni {
	template <>
	bool Queue<SysMessage>::Enqueue(const SysMessage& item) {
		if (item.type == SysMessage::RUPT_MOUSE && !isEmpty()) {
			// Locate the tail pointer (the last written element, handling ring buffer wrap-around)
			SysMessage* tail = (p == offss) ? (offss + limit - 1) : (p - 1);

			if (tail->type == SysMessage::RUPT_MOUSE) {
				// Check if button states are identical (ensures correct logic for dragging operations)
				if (tail->args.mou_event.btn_left == item.args.mou_event.btn_left &&
					tail->args.mou_event.btn_right == item.args.mou_event.btn_right &&
					tail->args.mou_event.btn_middle == item.args.mou_event.btn_middle)
				{
					stdsint new_x, new_y;
					if (!__builtin_add_overflow(tail->args.mou_event.movement.x, item.args.mou_event.movement.x, &new_x) &&
						!__builtin_add_overflow(tail->args.mou_event.movement.y, item.args.mou_event.movement.y, &new_y)) {
						tail->args.mou_event.movement = uni::Size2dif(new_x, new_y);
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
		#if CONFIG_ENABLE_MMU
		Memory::ReleasePagingMappings(pb->paging);
		#endif
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
	if (_IMM(tb) & 0xF) plogerro("Taskman::AllocateThread() [%p]", tb);
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
	extern void CleanupPwcallThreadInterrupts(stduint tid);
	// [HYP-C] Verify thread is not still running on any CPU before freeing its stack
	while (true) {
		bool active = (th->state == ThreadBlock::State::Running);
		if (!active) {
			for (stduint _i = 0; _i < Taskman::PCU_CORES; _i++) {
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
	Systimex::CancelThreadWake(th->tid);
	CleanupPwcallThreadInterrupts(th->tid);
	device_event_release(th);
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
		#if (_MCCA & 0xFF00) == 0x8600
		// Detach stack-owned storage wait nodes before destroying an abandoned kernel stack.
		CancelStorageWait(th->tid, false);
		#endif
		Taskman::DequeueReady(th, false);
		Taskman::thchain.Remove(th);
	}

	if (th->stack_lineaddr == th->stack_levladdr) {
		free((byte*)th->stack_levladdr);
	}
	else if (ppb) {
		#if CONFIG_ENABLE_MMU // assume only one stack in NoMMU
		if (th->stack_lineaddr) {
			free((byte*)ppb->paging[_IMM(th->stack_lineaddr) & ~0xFFF]);
		}
		#endif
		free((byte*)th->stack_levladdr);
	}
	th->~ThreadBlock();
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

	#elif (_MCCA & 0xFFFF) == 0x2032

	// ARM M-profile Context Dump (General Purpose Registers)
	ploginfo("  R0 : %[x]  R1 : %[x]  R2 : %[x]  R3 : %[x]", ctx->R0, ctx->R1, ctx->R2, ctx->R3);
	ploginfo("  R4 : %[x]  R5 : %[x]  R6 : %[x]  R7 : %[x]", ctx->R4, ctx->R5, ctx->R6, ctx->R7);
	ploginfo("  R8 : %[x]  R9 : %[x]  R10: %[x]  R11: %[x]", ctx->R8, ctx->R9, ctx->R10, ctx->R11);
	ploginfo("  R12: %[x]  SP : %[x]  PC : %[x]  PSR: %[x]", ctx->R12, ctx->SP, ctx->pc, ctx->SPSR);

	// Stack pointers and mode
	ploginfo("  EXC_RETURN: %[x]  CONTROL: %[x]", ctx->exc_return, ctx->CONTROL);
	ploginfo("  SP_svc: %[x]  SP_irq: %[x]", ctx->SP_svc, ctx->SP_irq);
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
}

static void _Exit_Cleanup(stduint pid)
{
	extern void CleanupPwcallProcessHandles(stduint pid);
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
	ppb->focus_tty.raw_mutex().Release();

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

	// 2. Release Power Device Handles
	CleanupPwcallProcessHandles(pid);

	// 3. Release TTY Binding
	DetachProcessTTYMembership(ppb, pid);

	// 4. Unlink all threads from IPC queues (but don't delete them yet)
	ThreadBlock* th_ptr = ppb->thread_list_head;
	while (th_ptr) {
		Filesys::CancelPipeWait(th_ptr, true);
		#if (_MCCA & 0xFF00) == 0x8600
		Devsman::CancelSocketWait(th_ptr);
		#endif
		msg_cleanup_thread(th_ptr, true);
		th_ptr = th_ptr->process_thread_next;
	}

	// 5. Release Files
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

	#if CONFIG_ENABLE_MMU
	{
		MutexLocal mapping_guard(&ppb->mutex_mapping);
		if (!ppb->ReleaseAllVmas())
			plogerro("Failed to release all VMAs for process %u", pid);
		ppb->ReleaseLoadslices();
	}
	#else
	ppb->ReleaseLoadslices();
	#endif
	// Release Heap/Paging
	#if CONFIG_ENABLE_MMU
	// Drop the ELF inode pin after releasing its VMAs and load slices.
	uni::UnpinExecutableImage(ppb);
	Memory::ReleasePagingMappings(ppb->paging);
	if (!ppb->ring) {
		ppb->paging.root_level_page = nullptr;
	}
	#endif

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

static bool DeliverWaitResultAtomically(ProcessBlock* pparent, stduint child_pid,
	stduint exit_status, bool& unblock_recv) {
	unblock_recv = false;
	if (!pparent || !pparent->main_thread) return false;
	ProcessBlock* taskman_pb = Taskman::Locate(Task_TaskMan);
	if (!taskman_pb || !taskman_pb->main_thread) return false;

	ThreadBlock* parent_th = pparent->main_thread;
	ThreadBlock* taskman_th = taskman_pb->main_thread;
	stduint args[2] = { child_pid, exit_status };
	bool direct_receive = false;
	bool message_in_kernel = false;
	CommMsg* pending_message = nullptr;

	{
		extern Spinlock comm_lock;
		SpinlockLocal guard(&comm_lock);
		if (!(parent_th->block_reason & ThreadBlock::BlockReason::BR_Waiting)) return false;
		direct_receive = parent_th->block_reason & ThreadBlock::BlockReason::BR_RecvMsg;
		if (direct_receive) {
			if (!(parent_th->recv_fo_whom == taskman_th ||
				(stduint)parent_th->recv_fo_whom == ANYPROC)) return false;
			pending_message = parent_th->unsolved_msg;
			message_in_kernel = parent_th->unsolved_msg_from_kernel;
			if (!pending_message) return false;
		}
	}

	if (direct_receive) {
		CommMsg prepared_message = {};
		#if CONFIG_ENABLE_MMU
		MutexLocal mapping_guard(&pparent->mutex_mapping);
		if (!message_in_kernel &&
			!PrepareUserBuffer(pparent, _IMM(pending_message),
				sizeof(prepared_message), true,
				true _Comment(caller_holds_pb_mutex_mapping))) return false;
		Paging& message_paging = message_in_kernel ? kernel_paging : pparent->paging;
		if (MemCopyP(&prepared_message, kernel_paging, pending_message,
			message_paging, sizeof(prepared_message)) != sizeof(prepared_message)) return false;
		const stduint prepared_length = minof((stduint)sizeof(args),
			prepared_message.data.length);
		if (prepared_length && !message_in_kernel &&
			!PrepareUserBuffer(pparent, prepared_message.data.address,
				prepared_length, true,
				true _Comment(caller_holds_pb_mutex_mapping))) return false;
		#else
		prepared_message = *pending_message;
		const stduint prepared_length = minof((stduint)sizeof(args),
			prepared_message.data.length);
		#endif

		// User buffers are resident before comm_lock; raw copies below cannot acquire vma_lock.
		{
			extern Spinlock comm_lock;
			SpinlockLocal guard(&comm_lock);
			if (!(parent_th->block_reason & ThreadBlock::BlockReason::BR_Waiting) ||
				!(parent_th->block_reason & ThreadBlock::BlockReason::BR_RecvMsg) ||
				!(parent_th->recv_fo_whom == taskman_th ||
					(stduint)parent_th->recv_fo_whom == ANYPROC) ||
				parent_th->unsolved_msg != pending_message ||
				parent_th->unsolved_msg_from_kernel != message_in_kernel) return false;

			CommMsg current_message = {};
			#if CONFIG_ENABLE_MMU
			if (MemCopyP(&current_message, kernel_paging, pending_message,
				message_paging, sizeof(current_message)) != sizeof(current_message)) return false;
			#else
			current_message = *pending_message;
			#endif
			if (current_message.data.address != prepared_message.data.address ||
				current_message.data.length != prepared_message.data.length) return false;
			if (prepared_length) {
				#if CONFIG_ENABLE_MMU
				if (MccaMemCopyPPrepared((void*)current_message.data.address,
					pparent, message_in_kernel, args, nullptr, true,
					prepared_length) != prepared_length) return false;
				#else
				MemCopyN((void*)current_message.data.address, args, prepared_length);
				#endif
			}
			current_message.type = 0;
			current_message.src = taskman_th->tid;
			#if CONFIG_ENABLE_MMU
			if (MccaMemCopyPPrepared(pending_message, pparent,
				message_in_kernel, &current_message, nullptr, true,
				sizeof(current_message)) != sizeof(current_message)) return false;
			#else
			*pending_message = current_message;
			#endif
			UnlinkWaitEntry(parent_th);
			parent_th->unsolved_msg = nullptr;
			parent_th->recv_fo_whom = nullptr;
			pparent->wait_for_pid = 0;
			unblock_recv = true;
		}
	}
	else {
		extern Spinlock comm_lock;
		SpinlockLocal guard(&comm_lock);
		if (!(parent_th->block_reason & ThreadBlock::BlockReason::BR_Waiting) ||
			(parent_th->block_reason & ThreadBlock::BlockReason::BR_RecvMsg)) return false;
		if (parent_th->async_messages.Count() >= LIMIT_THREAD_AMSG) {
			plogerro("Too many async messages");
			return false;
		}
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
	}
	return true;
}

static void NotifyTaskLifecycle(stduint parent_pid, stduint pid, stdsint exit_status) {
	TaskLifecycleEvent event = {};
	event.kind = _IMM(TaskLifecycleEventKind::Exited);
	event.pid = uint32(pid);
	event.parent_pid = uint32(parent_pid);
	event.exit_status = int32(exit_status);
	if (syssend_async(parent_pid, &event, sizeof(event), _IMM(KernelMsg::TaskLifecycle))) {
		plogwarn("Task lifecycle event dropped: parent=%u pid=%u", parent_pid, pid);
	}
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

	//? Optional ?
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
					th->send_to_whom && th->send_to_whom != (ThreadBlock*)INTRUPT &&
					th->send_to_whom != (ThreadBlock*)ANYPROC && th->send_to_whom->parent_process == p) {
					ThreadBlock* target_th = th->send_to_whom;
					if (target_th->queue_send_queuehead == th) {
						target_th->queue_send_queuehead = th->queue_send_queuenext;
					}
					else {
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
					wake_send = true;
				}

				// 2. If this system task is blocked while receiving from us, sever pointers under comm_lock
				if ((th->block_reason & ThreadBlock::BlockReason::BR_RecvMsg) &&
					th->recv_fo_whom && th->recv_fo_whom != (ThreadBlock*)INTRUPT &&
					th->recv_fo_whom != (ThreadBlock*)ANYPROC && th->recv_fo_whom->parent_process == p) {
					UnlinkWaitEntry(th);
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

		// Reap Hanging children directly instead of reparenting them to Task_Init
		if (child->state == ProcessBlock::State::Hanging) {
			_Exit_Cleanup(child->pid);
			continue;
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
	bool parent_is_nonwait_kernel_owner = (parent_pid == Task_Kernel && pid != Task_Init)
										|| parent_pid == Task_Init;

	if (parent_active && parent_pid == Task_Devsman) {
		_Exit_Cleanup(pid);
		NotifyTaskLifecycle(parent_pid, pid, exit_code);
	}
	else if (parent_active && pparent->isWaiting() && (pparent->wait_for_pid == 0 || pparent->wait_for_pid == pid)) {
		bool unblock_recv = false;
		if (DeliverWaitResultAtomically(pparent, pid, _IMM(exit_code), unblock_recv)) {
			ThreadBlock* parent_th = pparent->main_thread;
			_Exit_Cleanup(pid); // Die completely
			parent_th->Unblock(ThreadBlock::BlockReason::BR_Waiting);
			if (unblock_recv)
				parent_th->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
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
#if (_MCCA & 0xFF00) == 0x8600 || (_MCCA & 0xFF00) == 0x1000 || (_MCCA & 0xFFFF) == 0x2032
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
			#if CONFIG_ENABLE_MMU // NoMMU keeps the exit-only behaviour
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
			#endif

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
