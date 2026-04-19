// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <c/format/ELF.h>
#include <c/driver/keyboard.h>


#if (_MCCA & 0xFF00) == 0x8600
TSS_t* PCU_CORES_TSS[PCU_CORES_MAX] = {};
#endif
#ifdef _ARC_x86 // x86:
bool task_switch_enable = true;//{} spinlk
#endif

Dchain Taskman::chain = { DnodeHeapFreeSimple };;// ordered by pid
stduint Taskman::min_available_pid;// in chain
Dnode* Taskman::min_available_left;;

stduint Taskman::PCU_CORES = 0;
stduint Taskman::pcurrent[PCU_CORES_MAX];
ThreadBlock* Taskman::current_thread[PCU_CORES_MAX];
stduint Taskman::min_available_tid = 0;


Taskman::ReadyQueue Taskman::priority_queues[32] = {};
Taskman::ReadyQueue Taskman::expired_queues[32] = {};
unsigned int Taskman::ready_bitmap = 0;
unsigned int Taskman::expired_bitmap = 0;

void Taskman::EnqueueReady(ThreadBlock* pb) {
	if (pb->state != ThreadBlock::State::Ready && pb->state != ThreadBlock::State::Running) return;
	int idx = pb->priority + 16;
	if (idx < 0) idx = 0;
	if (idx > 31) idx = 31;

	if (pb->queue_state_next != nullptr || pb->queue_state_prev != nullptr || priority_queues[idx].head == pb) {
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
}

void Taskman::EnqueueExpired(ThreadBlock* pb) {
	if (pb->state != ThreadBlock::State::Ready && pb->state != ThreadBlock::State::Running) return;
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
}

void Taskman::DequeueReady(ThreadBlock* pb) {
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
	uint32 idx = __builtin_ctz(ready_bitmap);
	return priority_queues[idx].head;
}

void ThreadBlock::Block(BlockReason reason) {
	if (state == State::Ready || state == State::Running) Taskman::DequeueReady(this);
	state = State::Pended;
	block_reason = BlockReason(block_reason | reason);
}

void ThreadBlock::Unblock(BlockReason reason) {
	block_reason = BlockReason(block_reason & ~reason);
	if (block_reason == BlockReason::BR_None) {
		state = State::Ready;//{} else panic...
		if (this->is_expired) {
			Taskman::EnqueueExpired(this);
		} else {
			Taskman::EnqueueReady(this);
		}
	}
}

bool Taskman::Append(ProcessBlock* task) {
	// Find available PID (Find smallest gap)
	stduint target_pid = 1;
	Dnode* insert_after = nullptr;
	for (auto nod = chain.Root(); nod; nod = nod->next) {
		stduint crt = cast<ProcessBlock*>(nod->offs)->pid;
		if (crt == target_pid) target_pid++;
		else if (crt > target_pid) break;
		insert_after = nod;
	}
	task->pid = target_pid;
	chain.Append(task, false, insert_after);
	task->state = ProcessBlock::State::Active;
	return true;
}

bool Taskman::AppendThread(ThreadBlock* task) {
	stduint target_tid = 1;
	while (LocateThread(target_tid) != nullptr) {
		target_tid++;
	}
	task->tid = target_tid;
	
	task->state = ThreadBlock::State::Ready;
	EnqueueReady(task);
	return true;
}

ProcessBlock* Taskman::Locate(stduint taskid) {
	for (auto nod = chain.Root(); nod; nod = nod->next) {
		auto task = cast<ProcessBlock*>(nod->offs);
		if (task->pid == taskid) return task;
	}
	return nullptr;
}

ThreadBlock* Taskman::LocateThread(stduint tid) {
	for (auto nod = chain.Root(); nod; nod = nod->next) {
		auto ptask = cast<ProcessBlock*>(nod->offs);
		for (auto th = ptask->thread_list_head; th; th = th->process_thread_next) {
			if (th->tid == tid) return th;
		}
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
				Taskman::DequeueReady(old_pb);
				old_pb->state = TBS::Ready;
				
				// Enqueue to Expired Queue for Epoch A-Type Scheduling
				Taskman::EnqueueExpired(old_pb); 
				
				// Allocation mapping:
				old_pb->time_slice = 3 - (old_pb->priority >> 2);
			}
		} else {
			// RT management (B-Type)
			if (old_pb->time_slice > 0) {
				old_pb->time_slice--;
				return true; // Continue same task
			} else {
				Taskman::DequeueReady(old_pb);
				old_pb->state = TBS::Ready;
				Taskman::EnqueueReady(old_pb); // Re-queue at tail (Round-Robin for same RT priority)
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
	using TBS = ThreadBlock::State;
	#if _MCCA == 0x8632
	task_switch_enable = false;//{TODO} Lock, cpu0
	#endif
	stduint cpuid = getID();
	auto old_tb = current_thread[cpuid];
	if (!old_tb) {
		plogerro("Taskman:Schedule: No current task found for CPU %d", cpuid);
		HALT();
		return;
	}
	if (!omit_slice) {
		if (ifContinueProcess(old_tb)) {
			#if _MCCA == 0x8632
			task_switch_enable = true;
			#endif
			return;
		}
	}
	else {
		auto s = old_tb->state;
		if (s == TBS::Pended) {
			// Already handled by Block(); don't touch state or queues
		} else {
			Taskman::DequeueReady(old_tb);
			old_tb->state = TBS::Ready;
			if (s == TBS::Running || s == TBS::Ready) Taskman::EnqueueExpired(old_tb);
		}
	}
	auto new_tb = PickNext();
	if (!new_tb) new_tb = current_thread[cpuid]; // At least run self or idle thread if nothing else
	if (new_tb == old_tb) {
		if (old_tb->state == TBS::Ready) old_tb->state = TBS::Running;
		#if _MCCA == 0x8632
		task_switch_enable = true;
		#endif
		return;
	}
	CurrentTID() = new_tb->tid;
	CurrentPID() = new_tb->parent_process->pid;
	
	if (new_tb->state == TBS::Uninit) new_tb->state = TBS::Ready;
	if (old_tb->state == TBS::Running && new_tb->state == TBS::Ready) {
		old_tb->state = TBS::Ready;
		new_tb->state = TBS::Running;
	} else if (new_tb->state == TBS::Ready) {
		new_tb->state = TBS::Running;
	} else {
		printlog(_LOG_FATAL, "task switch error state (TID%u, State%u)", new_tb->tid, _IMM(new_tb->state));
	}

	current_thread[cpuid] = new_tb;

	#if _MCCA == 0x8632
	task_switch_enable = true;//{TODO} X86 Unlock
	#elif (_MCCA & 0xFF00) == 0x1000
	#endif

	SwitchTaskContext(&new_tb->context, &old_tb->context);
}
#else
auto Taskman::Schedule(bool omit_slice)->decltype(Schedule()) { }
#endif


