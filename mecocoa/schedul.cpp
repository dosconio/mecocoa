// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <c/format/ELF.h>
#include <c/driver/keyboard.h>


#if (_MCCA & 0xFF00) == 0x8600
PERCORE* Taskman::PCU_CORES_PERCORE[PCU_CORES_MAX] = {};
uint32 g_lapicid_to_coreid[LAPIC_ID_MAP_SIZE] = {};
extern "C" uint32 ap_lapicid_to_coreid[LAPIC_ID_MAP_SIZE] = {};
extern "C" stduint ap_higher_stack_tops[PCU_CORES_MAX] = {};
#else
ThreadBlock* PCU_CORES_current_thread[PCU_CORES_MAX] = {};
ThreadBlock* PCU_CORES_idle_thread[PCU_CORES_MAX] = {};
ThreadBlock* volatile PCU_CORES_switching_out_threads[PCU_CORES_MAX] = {};
#endif
#ifdef _ARC_x86 // x86:
#endif

Dchain Taskman::chain = { DnodeHeapFreeSimple };;// ordered by pid
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

#if   _MCCA == 0x8632
#define _NORMAL_RINGSTACK 0xFFC01000ull
#elif _MCCA == 0x8664
#define _NORMAL_RINGSTACK 0xFFFFFFFFC0001000ull
#endif

#if (_MCCA & 0xFF00) == 0x8600
static void RebindThreadKernelStackWindow(ThreadBlock* th) {
	if (!th || !th->parent_process || !th->stack_levladdr || !th->stack_size) return;

	auto remap_stack_window = [&](Paging& pg) {
		pg.Map(
			_NORMAL_RINGSTACK,
			_IMM(th->stack_levladdr),
			th->stack_size,
			PAGESIZE_4KB,
			PGPROP_present | PGPROP_writable
		);
	};

	// The currently active CR3 still needs this fixed VA window to point at the
	// incoming thread's ring0 stack because SwitchTaskContext restores the new
	// kernel continuation before it switches CR3.
	remap_stack_window(kernel_paging);
	remap_stack_window(th->parent_process->paging);
	Paging active_pg = {};
	active_pg.root_level_page = (PageEntry*)getCR3();
	if (active_pg.root_level_page != kernel_paging.root_level_page &&
		active_pg.root_level_page != th->parent_process->paging.root_level_page) {
		remap_stack_window(active_pg);
	}

	for (stduint off = 0; off < th->stack_size; off += 0x1000) {
		RefreshVirtualAddress(_NORMAL_RINGSTACK + off);
	}
}
#endif

#if _MCCA == 0x8632
static void BindCurrentKernelEntryStack(ThreadBlock* th, stduint cpuid) {
	if (!th) return;
	auto percore = Taskman::PCU_CORES_PERCORE[cpuid];
	if (!percore) return;
	if (!th->stack_levladdr || !th->stack_size) return;
	// 8632 enters ring0 through the fixed high virtual stack window. The
	// per-thread ring0 stack backing changes, but ESP0 must keep pointing at
	// the remapped window top rather than the backing allocation itself.
	RebindThreadKernelStackWindow(th);
	percore->current_thread = th;
	percore->kernel_stack = _NORMAL_RINGSTACK + th->stack_size - 0x10;
	percore->tss.ESP0 = percore->kernel_stack;
	percore->tss.SS0 = SegData;
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
	percore->state = CoreState::Online;
	ploginfo("[COREMAN] AP core%u online, lapic=%[x]", core_id, percore->lapic_id);
	loop HALT();
}
#elif _MCCA == 0x8664
static void BindCurrentKernelEntryStack(ThreadBlock* th, stduint cpuid) {
	if (!th) return;
	auto percore = Taskman::PCU_CORES_PERCORE[cpuid];
	if (!percore) return;
	RebindThreadKernelStackWindow(th);
	percore->current_thread = th;
	percore->kernel_rsp = _NORMAL_RINGSTACK + th->stack_size;
	percore->tss.RSP0 = percore->kernel_rsp;
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
			if (!node->just_schedule) {
				// Check whether node is in the array: 
				for (int i = 0; i < PCU_CORES_MAX; i++) {
					if (switching_out_threads(i) == node) {
						switching_out_threads(i) = nullptr;
						break;
					}
				}
				return node;
			}
			node = node->queue_state_next;
		}
		original_bitmap &= ~(1U << idx);
	}
	return nullptr;
}

void ThreadBlock::Block(BlockReason reason) {
	Taskman::DequeueReady(this);
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
auto Taskman::Schedule(bool omit_slice)->decltype(Schedule())
{
	bool old_if = scheduler_lock.Acquire();
	using TBS = ThreadBlock::State;
	stduint cpuid = getID();

	// Release the CPU's hold on switching output thread in clock cycle.
	if (switching_out_threads(cpuid) != nullptr) {
		auto completed_th = switching_out_threads(cpuid);
		if (completed_th->state == ThreadBlock::State::Hanging && completed_th->is_detached) {
			Taskman::DestroyThread(completed_th);
		}
		switching_out_threads(cpuid) = nullptr;
	}

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
		scheduler_lock.Release(old_if);
		return;
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
	#if (_MCCA & 0xFF00) == 0x8600
	BindCurrentKernelEntryStack(new_tb, cpuid);
	#endif

	old_tb->just_schedule = 1;
	switching_out_threads(cpuid) = old_tb;

	scheduler_lock.Release(old_if);
	// if (new_tb->tid > 8) ploginfo("[CPU%u]SCH: Th%u -> Th%u", cpuid, old_tb->tid, new_tb->tid);
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
	auto old_tb = current_thread(getID());
	bool old_if = scheduler_lock.Acquire();
	DequeueReady(old_tb, false);
	lk->Release(false);

	auto new_tb = PickNext();
	if (!new_tb) {
		new_tb = idle_thread(getID());
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

	current_thread(getID()) = new_tb;
	#if (_MCCA & 0xFF00) == 0x8600
	BindCurrentKernelEntryStack(new_tb, getID());
	#endif
	new_tb->state = ThreadBlock::State::Running;

	old_tb->just_schedule = 1;
	switching_out_threads(getID()) = old_tb;

	scheduler_lock.Release(old_if);
	// No explicit unlock of scheduler_lock here, because earlier we unlock lk, but wait!
	#if _MCCA == 0x8664 || _MCCA == 0x8632
	((void(*)(NormalTaskContext*, NormalTaskContext*))mglb(SwitchTaskContext))((NormalTaskContext*)mglb(&new_tb->context), (NormalTaskContext*)mglb(&old_tb->context));
	#else
	SwitchTaskContext(&new_tb->context, &old_tb->context);
	#endif
}
