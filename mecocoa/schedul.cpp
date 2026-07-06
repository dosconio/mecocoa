// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <c/format/ELF.h>
#include <c/driver/keyboard.h>
#if _MCCA == 0x8632
#include <c/proctrl/IAx86_64.msr.h>
#endif


#if (_MCCA & 0xFF00) == 0x8600
PERCORE* Taskman::PCU_CORES_PERCORE[PCU_CORES_MAX] = {};

extern "C" PERCORE* C_PCU_CORES_PERCORE[PCU_CORES_MAX]; // exported for assembly use
PERCORE* C_PCU_CORES_PERCORE[PCU_CORES_MAX] = {};       // exported for assembly use
extern "C" uint32 C_SegTSS0;                            // exported for assembly use
uint32 C_SegTSS0 = SegTSS0;

uint32 g_lapicid_to_coreid[LAPIC_ID_MAP_SIZE] = {};
#if _MCCA == 0x8632
uint32 ap_lapicid_to_coreid[LAPIC_ID_MAP_SIZE] = {};
stduint ap_higher_stack_tops[PCU_CORES_MAX] = {};
stduint ap_ring3_iret_stack_tops[PCU_CORES_MAX] = {};
volatile uint32 ap_resched_ipi_hits[PCU_CORES_MAX] = {};
volatile uint32 ap_ring3_iret_guard_hits = 0;
volatile uint32 ap_ring3_iret_last_lapicid = 0;
volatile uint32 ap_ring3_iret_last_coreid = 0;
static bool g_enable_ap_scheduling = true;
#endif
#else
ThreadBlock* PCU_CORES_current_thread[PCU_CORES_MAX] = {};
ThreadBlock* PCU_CORES_idle_thread[PCU_CORES_MAX] = {};
ThreadBlock* volatile PCU_CORES_switching_out_threads[PCU_CORES_MAX] = {};
#endif
#ifdef _ARC_x86 // x86:
#endif

Dchain Taskman::chain = {nullptr}; // { DnodeHeapFreeSimple };// ordered by pid
stduint Taskman::min_available_pid;// in chain
Dnode* Taskman::min_available_left;;

stduint Taskman::PCU_CORES = 0;
stduint Taskman::min_available_tid = 0;
Dchain Taskman::thchain = { nullptr };
// Dnode* Taskman::min_available_thleft = nullptr;


Taskman::ReadyQueue Taskman::priority_queues[32] = {};
Taskman::ReadyQueue Taskman::expired_queues[32] = {};
unsigned int Taskman::ready_bitmap = 0;
unsigned int Taskman::expired_bitmap = 0;
Spinlock scheduler_lock;

static void ReleaseSchedulerLockForSwitch() {
	scheduler_lock.cpu_id = -1;
	__atomic_store_n(&scheduler_lock.locked, 0, __ATOMIC_RELEASE);
}


#if _MCCA == 0x8632
static void BindCurrentKernelEntryStack(ThreadBlock* th, stduint cpuid) {
	if (!th) return;
	auto percore = Taskman::PCU_CORES_PERCORE[cpuid];
	if (!percore) return;
	percore->current_thread = th;

	if (!th->stack_levladdr || !th->stack_size) {
		plogerro("[CPU%u] BindCurrentKernelEntryStack skip tid=%u stack=%[x] size=%u keep_kstack=%[x]",
			cpuid, th->tid, th->stack_levladdr, th->stack_size,
			percore->kernel_stack);
		return;
	}
	
	percore->kernel_stack = _IMM(th->stack_levladdr) + th->stack_size - 0x10;
	percore->tss.ESP0 = GetCoreTransitionStackTop(cpuid);
	percore->tss.SS0 = SegData;
}

static void InitializeLocalApicOnAp() {
	constexpr uint64 IA32_APIC_BASE__APIC_GLOBAL_ENABLE = (1ull << 11);
	constexpr uint64 IA32_APIC_BASE__X2APIC_ENABLE = (1ull << 10);
	uint64 apic_base = getMSR(x86MSR::APIC_BASE);
	apic_base |= IA32_APIC_BASE__APIC_GLOBAL_ENABLE;
	if (IC.getType() == 2) apic_base |= IA32_APIC_BASE__X2APIC_ENABLE;
	setMSR(x86MSR::APIC_BASE, apic_base);

	IC.WriteLAPIC(0xF0, 0x1FF);
	const uint32 mask = 0x10000;
	IC.WriteLAPIC(0x320, mask);
	IC.WriteLAPIC(0x350, mask);
	IC.WriteLAPIC(0x360, mask);
	IC.WriteLAPIC(0x370, mask);
	IC.SendEOI();
}

static void HandleRescheduleIPI() {
	stduint cpuid = Taskman::getID();
	if (cpuid < PCU_CORES_MAX) ap_resched_ipi_hits[cpuid]++;
	IC.SendEOI();
	if (g_enable_ap_scheduling) {
		Taskman::Schedule(true);
	}
}

static void HandleWakeIPI() {
	IC.SendEOI();
}

static bool WakeThreadOnRecordedCpu(ThreadBlock* th) {
	if (!th) return false;
	const stduint target_cpu = th->processor_id;
	if (target_cpu == CORE_ID_INVALID || target_cpu >= Taskman::PCU_CORES) return false;
	const stduint current_cpu = Taskman::getID();
	if (target_cpu == current_cpu) return false;
	auto percore = Taskman::PCU_CORES_PERCORE[target_cpu];
	if (!percore || percore->state != CoreState::Online) return false;
	Taskman::SendWakeIPI(target_cpu);
	return true;
}

static stduint CountQueuedReadyTasks(stduint limit) {
	stduint count = 0;
	for (int idx = 0; idx < 32; ++idx) {
		ThreadBlock* node = Taskman::priority_queues[idx].head;
		stduint guard = 0;
		while (node) {
			if (node->state == ThreadBlock::State::Ready) {
				if (++count >= limit) return count;
			}
			node = node->queue_state_next;
			if (++guard > 4096) return count;
		}
		node = Taskman::expired_queues[idx].head;
		guard = 0;
		while (node) {
			if (node->state == ThreadBlock::State::Ready) {
				if (++count >= limit) return count;
			}
			node = node->queue_state_next;
			if (++guard > 4096) return count;
		}
	}
	return count;
}

bool WakeOneIdleApForReadyWork() {
	#if _SYS_MULTICORE
	if (!g_enable_ap_scheduling) return false;
	if (Taskman::PCU_CORES <= 1) return false;
	if (Taskman::getID() != 0) return false;

	SpinlockLocal guard(&scheduler_lock);
	// if (CountQueuedReadyTasks(2) < 2) return false;

	static stduint next_ap = 1;
	for0(try_i, PCU_CORES_MAX) {
		if (next_ap >= Taskman::PCU_CORES) {
			next_ap = 1;
		}
		auto percore = Taskman::PCU_CORES_PERCORE[next_ap];
		if (percore &&
			percore->state == CoreState::Online &&
			Taskman::current_thread(next_ap) == Taskman::idle_thread(next_ap) &&
			(Taskman::switching_out_threads(next_ap) == nullptr ||
				!Taskman::switching_out_threads(next_ap)->just_schedule)) {
			Taskman::SendRescheduleIPI(next_ap);
			next_ap++;
			return true;
		}
		next_ap++;
	}
	#endif
	return false;
}

extern "C" void AP_Main(stduint core_id) {
	auto percore = Taskman::PCU_CORES_PERCORE[core_id];
	if (!percore) {
		plogerro("[COREMAN] AP core%u missing PERCORE", core_id);
		loop HALT();
	}
	auto idle = Taskman::idle_thread(core_id);
	if (!idle) {
		plogerro("[COREMAN] AP core%u missing idle thread", core_id);
		loop HALT();
	}
	idle->processor_id = core_id;
	idle->state = ThreadBlock::State::Running;
	Taskman::current_thread(core_id) = idle;
	BindCurrentKernelEntryStack(idle, core_id);
	InitializeLocalApicOnAp();
	loadIDT(mglb(0x800), 256 * sizeof(gate_t) - 1);
	IC[IRQ_RESCHED_IPI].setRange(mglb(Handint_RESCHED_Entry), SegCo32);
	IC[IRQ_WAKE_IPI].setRange(mglb(Handint_WAKE_Entry), SegCo32);
	if (percore->tss_selector) {
		loadTask(percore->tss_selector);
	}
	_ASM("LLDT %w0" : : "r"(SegGLDT) : "memory");
	percore->state = CoreState::Online;
	register_interrupt_handler(IRQ_RESCHED_IPI, HandleRescheduleIPI);
	register_interrupt_handler(IRQ_WAKE_IPI, HandleWakeIPI);
	EnableSSE();
	IC.enInterrupt();
	ploginfo("[COREMAN] AP core%u online, lapic=%[x], tss=%[x]", core_id, percore->lapic_id, percore->tss_selector);
	loop HALT();
}
#elif _MCCA == 0x8664
extern "C" void* higher_stacks[];
static void BindCurrentKernelEntryStack(ThreadBlock* th, stduint cpuid) {
	if (!th) return;
	auto percore = Taskman::PCU_CORES_PERCORE[cpuid];
	if (!percore) return;
	percore->current_thread = th;
	// Transition stack (jump board) used for ring3 entry and SYSRET
	percore->kernel_rsp = 0xFFFFFFFFFFFFF000ull - cpuid * 0x1000ull + 0x1000 - 0x10;
	percore->tss.RSP0 = percore->kernel_rsp;
	
	// Real kernel stack for the thread
	if (th->stack_levladdr && th->stack_size) {
		percore->kernel_stack = _IMM(th->stack_levladdr) + th->stack_size - 0x10;
	} else {
		percore->kernel_stack = percore->kernel_rsp;
	}
	if (!percore->current_thread) {
		printlog(_LOG_FATAL, "x64 sched assert: null current_thread on CPU%u", cpuid);
	}
	if (!percore->kernel_rsp) {
		printlog(_LOG_FATAL, "x64 sched assert: zero kernel_rsp on CPU%u TID%u", cpuid, th->tid);
	}
	if (percore->tss.RSP0 != percore->kernel_rsp) {
		printlog(_LOG_FATAL,
			"x64 sched assert: TSS.RSP0 mismatch on CPU%u TID%u rsp0=%[x] krsp=%[x]",
			cpuid, th->tid, percore->tss.RSP0, percore->kernel_rsp);
	}
}
#endif

void Taskman::EnqueueReady(ThreadBlock* pb, bool lock) {
	stduint old_if = 0;
	if (lock) old_if = scheduler_lock.Acquire();
	if (pb->state != ThreadBlock::State::Ready && pb->state != ThreadBlock::State::Running) {
		if (lock) scheduler_lock.Release(old_if);
		return;
	}
	int idx = pb->priority + 16;
	if (idx < 0) idx = 0;
	if (idx > 31) idx = 31;

	if (pb->queue_state_next != nullptr || pb->queue_state_prev != nullptr || priority_queues[idx].head == pb) {
		if (lock) scheduler_lock.Release(old_if);
		return;// duplicate check
	}

	
	pb->queue_state_next = nullptr;
	if (priority_queues[idx].tail) {
		priority_queues[idx].tail->queue_state_next = pb;
		pb->queue_state_prev = priority_queues[idx].tail;
		priority_queues[idx].tail = pb;
	} else {
		priority_queues[idx].head = priority_queues[idx].tail = pb;
		pb->queue_state_prev = nullptr;
	}
	ready_bitmap |= (1U << idx);
	if (lock) scheduler_lock.Release(old_if);
}

void Taskman::EnqueueExpired(ThreadBlock* pb, bool lock) {
	stduint old_if = 0;
	if (lock) old_if = scheduler_lock.Acquire();
	if (pb->state != ThreadBlock::State::Ready && pb->state != ThreadBlock::State::Running) {
		if (lock) scheduler_lock.Release(old_if);
		return;
	}
	int idx = pb->priority + 16;
	if (idx < 0) idx = 0;
	if (idx > 31) idx = 31;

	if (pb->queue_state_next != nullptr || pb->queue_state_prev != nullptr || expired_queues[idx].head == pb) {
		if (lock) scheduler_lock.Release(old_if);
		return; // duplicate check
	}
	
	pb->is_expired = true;
	
	pb->queue_state_next = nullptr;
	if (expired_queues[idx].tail) {
		expired_queues[idx].tail->queue_state_next = pb;
		pb->queue_state_prev = expired_queues[idx].tail;
		expired_queues[idx].tail = pb;
	} else {
		expired_queues[idx].head = expired_queues[idx].tail = pb;
		pb->queue_state_prev = nullptr;
	}
	expired_bitmap |= (1U << idx);
	if (lock) scheduler_lock.Release(old_if);
}

void Taskman::DequeueReady(ThreadBlock* pb, bool lock) {
	stduint old_if = 0;
	if (lock) old_if = scheduler_lock.Acquire();
	int idx = pb->priority + 16;
	if (idx < 0) idx = 0;
	if (idx > 31) idx = 31;

	bool in_active = false;
	bool in_expired = false;

	if (priority_queues[idx].head == pb) { 
		in_active = true; 
		priority_queues[idx].head = pb->queue_state_next; 
	} else if (expired_queues[idx].head == pb) { 
		in_expired = true; 
		expired_queues[idx].head = pb->queue_state_next; 
	} else {
		if (priority_queues[idx].tail == pb) in_active = true;
		else if (expired_queues[idx].tail == pb) in_expired = true;
	}

	if (pb->queue_state_prev) pb->queue_state_prev->queue_state_next = pb->queue_state_next;
	if (pb->queue_state_next) pb->queue_state_next->queue_state_prev = pb->queue_state_prev;

	if (in_active && priority_queues[idx].tail == pb) priority_queues[idx].tail = pb->queue_state_prev;
	if (in_expired && expired_queues[idx].tail == pb) expired_queues[idx].tail = pb->queue_state_prev;

	pb->queue_state_prev = pb->queue_state_next = nullptr;

	if (in_active && !priority_queues[idx].head) ready_bitmap &= ~(1U << idx);
	if (in_expired && !expired_queues[idx].head) expired_bitmap &= ~(1U << idx);
	if (lock) scheduler_lock.Release(old_if);
}

ThreadBlock* Taskman::PickNext() {
	// If the A-Type TS segment (bits 16-31) is empty in Ready, but populated in Expired, Swap!
	if ((ready_bitmap & 0xFFFF0000) == 0 && (expired_bitmap & 0xFFFF0000) != 0) {
		for (int i = 16; i < 32; i++) {
			priority_queues[i] = expired_queues[i];
			expired_queues[i].head = expired_queues[i].tail = nullptr;
			
			// Reset is_expired flag for all tasks in the new epoch
			ThreadBlock* node = priority_queues[i].head;
			while (node) {
				node->is_expired = false;
				node = node->queue_state_next;
			}
		}
		ready_bitmap |= (expired_bitmap & 0xFFFF0000);
		expired_bitmap &= 0x0000FFFF;
	}

	if (!ready_bitmap) return nullptr;
	uint32 original_bitmap = ready_bitmap;
	while (original_bitmap) {
		uint32 idx = __builtin_ctz(original_bitmap);
		ThreadBlock* node = priority_queues[idx].head;
		while (node) {
			if (!node->just_schedule && node->state != ThreadBlock::State::Running) {
				#if _MCCA == 0x8632
				if (node->ring_coreid == CORE_ID_INVALID || node->ring_coreid == getID()) {
				#else
				if (true) {
				#endif
					// Check whether node is in the array: 
					for (int i = 0; i < PCU_CORES_MAX; i++) {
						if (switching_out_threads(i) == node) {
							switching_out_threads(i) = nullptr;
							break;
						}
					}
					return node;
				}
			}
			node = node->queue_state_next;
		}
		original_bitmap &= ~(1U << idx);
	}
	return nullptr;
}

void ThreadBlock::Block(BlockReason reason) {
	SpinlockLocal guard(&scheduler_lock);
	Taskman::DequeueReady(this, false);
	state = State::Pended;
	block_reason = BlockReason(block_reason | reason);
}

void ThreadBlock::Unblock(BlockReason reason) {
	SpinlockLocal guard(&scheduler_lock);
	block_reason = BlockReason(block_reason & ~reason);
	if (block_reason == BlockReason::BR_None) {
		state = State::Ready;//{} else panic...
		if (this->is_expired) {
			Taskman::EnqueueExpired(this, false);
		} else {
			Taskman::EnqueueReady(this, false);
		}
		#if _MCCA == 0x8632
		WakeThreadOnRecordedCpu(this);
		#endif
	}
}

static stduint next_global_id = 1;

bool Taskman::Append(ProcessBlock* task) {
	SpinlockLocal guard(&scheduler_lock);
	task->pid = __atomic_fetch_add(&next_global_id, 1, __ATOMIC_SEQ_CST);

	// Hierarchical Process Tree: Link to parent
	ProcessBlock* pparent = nullptr;
	for (auto nod = chain.Root(); nod; nod = nod->next) {
		auto task_node = cast<ProcessBlock*>(nod->offs);
		if (task_node->pid == task->parent_id) {
			pparent = task_node;
			break;
		}
	}

	if (pparent) {
		task->sibling_next = pparent->child_list_head;
		pparent->child_list_head = task;
	}

	Dnode* insert_after = chain.Last();
	// Dnode* insert_after = nullptr;
	// for (auto nod = chain.Root(); nod; nod = nod->next) {
	// 	if (cast<ProcessBlock*>(nod->offs)->pid > task->pid) break;
	// 	insert_after = nod;
	// }

	chain.Append(task, false, insert_after);
	task->state = ProcessBlock::State::Active;

	if (0 && task->pid >= 8) {
		ploginfo("--- Audit of all Threads (New PID %u) ---", task->pid);
		for (auto nod = thchain.Root(); nod; nod = nod->next) {
			auto th = cast<ThreadBlock*>(nod->offs);
			ploginfo("TID %u: StackPhys=%[x], PB=%[x]", th->tid, th->stack_levladdr, th->parent_process);
		}
		ploginfo("------------------------------------------");
	}
	return true;
}

bool Taskman::AppendThread(ThreadBlock* task) {
	SpinlockLocal guard(&scheduler_lock);
	if (task->parent_process && task->parent_process->main_thread == task) {
		task->tid = task->parent_process->pid;
	}
	else {
		task->tid = __atomic_fetch_add(&next_global_id, 1, __ATOMIC_SEQ_CST);
	}

	Dnode* insert_after = thchain.Last();
	// Dnode* insert_after = nullptr;
	// for (auto nod = thchain.Root(); nod; nod = nod->next) {
	// 	if (cast<ThreadBlock*>(nod->offs)->tid > task->tid) break;
	// 	insert_after = nod;
	// }
	thchain.Append(task, false, insert_after);
	
	task->state = ThreadBlock::State::Ready;
	EnqueueReady(task, false);
	return true;
}

ProcessBlock* Taskman::Locate(stduint taskid) {
	SpinlockLocal guard(&scheduler_lock);
	for (auto nod = chain.Root(); nod; nod = nod->next) {
		auto task = cast<ProcessBlock*>(nod->offs);
		if (task->pid == taskid) return task;
	}
	return nullptr;
}

ThreadBlock* Taskman::LocateThread(stduint tid) {
	SpinlockLocal guard(&scheduler_lock);
	for (auto nod = thchain.Root(); nod; nod = nod->next) {
		auto th = cast<ThreadBlock*>(nod->offs);
		if (th->tid == tid) return th;
	}
	return nullptr;
}

static auto
ifContinueProcess(ThreadBlock* old_pb) -> bool {
	using TBS = ThreadBlock::State;
	// Timeslice management (A-Type context)
	if (old_pb->state == TBS::Running) {
		if (old_pb->priority >= 0) {
			if (old_pb->time_slice > 0) {
				old_pb->time_slice--;
				return true; // Continue same task
			} else {
				Taskman::DequeueReady(old_pb, false);
				old_pb->state = TBS::Ready;
				
				// Enqueue to Expired Queue for Epoch A-Type Scheduling
				Taskman::EnqueueExpired(old_pb, false); 
				
				// Allocation mapping:
				old_pb->time_slice = 3 - (old_pb->priority >> 2);
			}
		} else {
			// RT management (B-Type)
			if (old_pb->time_slice > 0) {
				old_pb->time_slice--;
				return true; // Continue same task
			} else {
				Taskman::DequeueReady(old_pb, false);
				old_pb->state = TBS::Ready;
				Taskman::EnqueueReady(old_pb, false); // Re-queue at tail (Round-Robin for same RT priority)
				old_pb->time_slice = 4; // RT tasks also get 4 slices to avoid hogging forever if yielding is missed
			}
		}
	} else if (old_pb && old_pb->state != TBS::Ready && old_pb->state != TBS::Uninit) {
		// e.g. Pended or Hanging, already handled by DequeueReady elsewhere or below
	} else if (old_pb && old_pb->state == TBS::Ready) {
		// Nothing special, already in queue
	}
	return false;
}

static void SettleSwitchingOutThread(stduint cpuid) {
	if (Taskman::switching_out_threads(cpuid) != nullptr) {
		auto completed_th = Taskman::switching_out_threads(cpuid);
		if (completed_th->state == ThreadBlock::State::Hanging && completed_th->is_detached) {
			Taskman::DestroyThread(completed_th);
		}
		Taskman::switching_out_threads(cpuid) = nullptr;
	}
}


/**
 * Mecocoa O(1) Priority Scheduler
 * 
 * B-Type (Real-Time): priority -16 ~ -1
 *  - Enqueued strictly to the `priority_queues` (Active Queue).
 *  - When time-slice expires, it is re-enqueued to the Active Queue tail.
 *  - High priority tasks will infinitely starve lower priority tasks as long as they demand CPU.
 * 
 * A-Type (Time-Slice): priority 0 ~ 15
 *  - Implements an Epoch-based Active/Expired Queue Mechanism.
 *  - Enqueued initially to the `priority_queues` (Active Queue).
 *  - When time-slice expires, it is enqueued to the `expired_queues` (Expired Queue).
 *  - Expired tasks CANNOT be scheduled until the entire Active Queue for A-Type tasks is empty.
 *  - Once empty, PickNext() triggers an Epoch Swap (Active <-> Expired), restoring all A-Type tasks.
 *  - This guarantees macro-level CPU share without starvation while maintaining strict micro-level preemption.
 */

 // before calling, the task may be pended
#if 1
extern "C" void* ring3_iret_stacks[PCU_CORES_MAX];
auto Taskman::Schedule(bool omit_slice)->decltype(Schedule())
{
	bool old_if = scheduler_lock.Acquire();
	using TBS = ThreadBlock::State;
	stduint cpuid = getID();

	// Release the CPU's hold on switching output thread in clock cycle.
	SettleSwitchingOutThread(cpuid);

	auto old_tb = current_thread(cpuid);
	if (!old_tb) {
		plogerro("Taskman:Schedule: No current task found for CPU %d", cpuid);
		HALT();
		return;
	}
	bool is_idle = (old_tb == idle_thread(cpuid));
	if (!omit_slice) {
		if (!is_idle && ifContinueProcess(old_tb)) {
			scheduler_lock.Release(old_if);
			return;
		}
		else if (is_idle) {
			if (!ready_bitmap && !expired_bitmap) {
				scheduler_lock.Release(old_if);
				return;// continue idle
			}
		}
	}
	else {
		auto s = old_tb->state;
		if (s == TBS::Pended || s == TBS::Hanging || s == TBS::Invalid) {
			// Already handled by Block(); don't touch state or queues
		}
		else if (!is_idle) {
			Taskman::DequeueReady(old_tb, false);
			old_tb->state = TBS::Ready;
			if (s == TBS::Running || s == TBS::Ready) Taskman::EnqueueExpired(old_tb, false);
		}
		else {
			old_tb->state = TBS::Ready;
		}
	}
	auto new_tb = PickNext();
	if (!new_tb) {
		// Fallback to self if runnable, otherwise switch to Idle thread
		if (old_tb->state == TBS::Running || old_tb->state == TBS::Ready) {
			new_tb = old_tb;
		}
		else {
			new_tb = idle_thread(cpuid);
			// Just in case idle_thread isn't set up yet, spin locally
			if (!new_tb) {
				scheduler_lock.Release(old_if);
				do {
					IC.enInterrupt();
					HALT();
					IC.enInterrupt(false);
				} while (!(new_tb = PickNext()));
				old_if = scheduler_lock.Acquire();
			}
		}
	}
	if (new_tb == old_tb) {
		if (old_tb->state == TBS::Ready) old_tb->state = TBS::Running;
		Taskman::DequeueReady(old_tb, false);
		old_tb->processor_id = cpuid;
		scheduler_lock.Release(old_if);
		return;
	}

	if (new_tb != idle_thread(cpuid)) {
		Taskman::DequeueReady(new_tb, false);
	}

	if (new_tb->state == TBS::Uninit) new_tb->state = TBS::Ready;
	if (old_tb->state == TBS::Running && new_tb->state == TBS::Ready) {
		old_tb->state = TBS::Ready;
		new_tb->state = TBS::Running;
	}
	else if (new_tb->state == TBS::Ready) {
		new_tb->state = TBS::Running;
	}
	else {
		printlog(_LOG_FATAL, "task switch error state (TID%u, State%u)", new_tb->tid, _IMM(new_tb->state));
	}

	current_thread(cpuid) = new_tb;
	new_tb->processor_id = cpuid;
	#if (_MCCA & 0xFF00) == 0x8600
	BindCurrentKernelEntryStack(new_tb, cpuid);
	#endif

	// [DIAG] Check stack canary for old_tb
	#if (_MCCA & 0xFF00) == 0x8600 && defined(_DEBUG)
	if (old_tb->stack_levladdr) {
		uint32* p = (uint32*)old_tb->stack_levladdr;
		uint32 canary = *(uint32*)old_tb->stack_levladdr;
		if (canary != 0xDEADBEEF) {
			plogerro("[STACK] TID%x canary=%x [%x] %p", old_tb->tid, canary, p[0], p);
		}
	}
	#if _MCCA == 0x8632
	if (treat<uint32>(ring3_iret_stacks[cpuid]) != 0xdeadbeef) {
		plogerro("[STACK] TID%x iret stack canary=%x", old_tb->tid, treat<uint32>(ring3_iret_stacks[cpuid]));
	}
	#endif
	#endif

	old_tb->just_schedule = 1;
	switching_out_threads(cpuid) = old_tb;

	#if _MCCA == 0x8632
	(void)old_if;
	ReleaseSchedulerLockForSwitch();
	#else
	scheduler_lock.Release(old_if);
	#endif
	if (0 && cpuid) {
		ploginfo("[CPU%u]SCH: Th%u -> Th%u", cpuid, old_tb->tid, new_tb->tid);
	}
	#if _MCCA == 0x8664 || _MCCA == 0x8632
	((void(*)(NormalTaskContext*, NormalTaskContext*))mglb(SwitchTaskContext))((NormalTaskContext*)mglb(&new_tb->context), (NormalTaskContext*)mglb(&old_tb->context));
	#else
	SwitchTaskContext(&new_tb->context, &old_tb->context);
	#endif
}
#else
auto Taskman::Schedule(bool omit_slice)->decltype(Schedule()) { }
#endif
void Taskman::SleepAndRelease(Spinlock* lk) {
	stduint cpuid = getID();
	auto old_tb = current_thread(cpuid);
	bool old_if = scheduler_lock.Acquire();
	SettleSwitchingOutThread(cpuid);
	DequeueReady(old_tb, false);
	lk->Release(false);

	auto new_tb = PickNext();
	if (!new_tb) {
		new_tb = idle_thread(cpuid);
		if (!new_tb) {// idle is not ready
			scheduler_lock.Release(old_if);
			do {
				IC.enInterrupt();
				HALT();
				IC.enInterrupt(false);
			} while (!(new_tb = PickNext()));
			old_if = scheduler_lock.Acquire();
		}
	}

	current_thread(cpuid) = new_tb;
	new_tb->processor_id = cpuid;
	#if (_MCCA & 0xFF00) == 0x8600
	BindCurrentKernelEntryStack(new_tb, cpuid);
	#endif
	if (new_tb != idle_thread(cpuid)) {
		DequeueReady(new_tb, false);
	}
	new_tb->state = ThreadBlock::State::Running;

	old_tb->just_schedule = 1;
	switching_out_threads(cpuid) = old_tb;

	#if _MCCA == 0x8632
	(void)old_if;
	ReleaseSchedulerLockForSwitch();
	#else
	scheduler_lock.Release(old_if);
	#endif
	// No explicit unlock of scheduler_lock here, because earlier we unlock lk, but wait!
	#if _MCCA == 0x8664 || _MCCA == 0x8632
	((void(*)(NormalTaskContext*, NormalTaskContext*))mglb(SwitchTaskContext))((NormalTaskContext*)mglb(&new_tb->context), (NormalTaskContext*)mglb(&old_tb->context));
	#else
	SwitchTaskContext(&new_tb->context, &old_tb->context);
	#endif
}
