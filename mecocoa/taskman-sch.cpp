// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <c/format/ELF.h>
#include <c/driver/keyboard.h>


#if (_MCCA & 0xFF00) == 0x8600
TSS_t* PCU_CORES_TSS[PCU_CORES_MAX] = {};
#endif

Dchain Taskman::chain = { DnodeHeapFreeSimple };;// ordered by pid
stduint Taskman::min_available_pid;// in chain
Dnode* Taskman::min_available_left;;

stduint Taskman::PCU_CORES = 0;
stduint Taskman::pcurrent[PCU_CORES_MAX];


Taskman::ReadyQueue Taskman::priority_queues[32] = {};
Taskman::ReadyQueue Taskman::expired_queues[32] = {};
unsigned int Taskman::ready_bitmap = 0;
unsigned int Taskman::expired_bitmap = 0;

void Taskman::EnqueueReady(ProcessBlock* pb) {
	if (pb->state != ProcessBlock::State::Ready && pb->state != ProcessBlock::State::Running) return;
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

void Taskman::EnqueueExpired(ProcessBlock* pb) {
	if (pb->state != ProcessBlock::State::Ready && pb->state != ProcessBlock::State::Running) return;
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

void Taskman::DequeueReady(ProcessBlock* pb) {
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

ProcessBlock* Taskman::PickNext() {
	// If the A-Type TS segment (bits 16-31) is empty in Ready, but populated in Expired, Swap!
	if ((ready_bitmap & 0xFFFF0000) == 0 && (expired_bitmap & 0xFFFF0000) != 0) {
		for (int i = 16; i < 32; i++) {
			priority_queues[i] = expired_queues[i];
			expired_queues[i].head = expired_queues[i].tail = nullptr;
			
			// Reset is_expired flag for all tasks in the new epoch
			ProcessBlock* node = priority_queues[i].head;
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

void ProcessBlock::Block(BlockReason reason) {
	if (state == State::Ready || state == State::Running) Taskman::DequeueReady(this);
	state = State::Pended;
	block_reason = BlockReason(block_reason | reason);
}

void ProcessBlock::Unblock(BlockReason reason) {
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
	task->pid = min_available_pid;
	auto nod = chain.Append(task, false, min_available_left);
	stduint las = min_available_pid;
	while (nod = nod->next) {
		stduint _new = ((ProcessBlock*)(nod->offs))->pid;
		if (_new == las + 1) {
			las = _new;
		}
		else break;
	}
	min_available_pid = las + 1;
	task->state = ProcessBlock::State::Ready;
	min_available_left = nod;
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

static auto
ifContinueProcess(ProcessBlock* old_pb) -> bool {
	using PBS = ProcessBlock::State;
	// Timeslice management (A-Type context)
	if (old_pb->state == PBS::Running) {
		if (old_pb->priority >= 0) {
			if (old_pb->time_slice > 0) {
				old_pb->time_slice--;
				return true; // Continue same task
			} else {
				Taskman::DequeueReady(old_pb);
				old_pb->state = PBS::Ready;
				
				// Enqueue to Expired Queue for Epoch A-Type Scheduling
				Taskman::EnqueueExpired(old_pb); 
				
				// Allocation mapping:
				// < 0 (RT)     => 4 slices
				// 0..3         => 4 slices
				// 4..7         => 3 slices
				// 8..11        => 2 slices
				// 12..15       => 1 slice
				if (old_pb->priority <= 3) old_pb->time_slice = 4;
				else if (old_pb->priority <= 7) old_pb->time_slice = 3;
				else if (old_pb->priority <= 11) old_pb->time_slice = 2;
				else old_pb->time_slice = 1;
			}
		} else {
			// RT management (B-Type)
			if (old_pb->time_slice > 0) {
				old_pb->time_slice--;
				return true; // Continue same task
			} else {
				Taskman::DequeueReady(old_pb);
				old_pb->state = PBS::Ready;
				Taskman::EnqueueReady(old_pb); // Re-queue at tail (Round-Robin for same RT priority)
				old_pb->time_slice = 4; // RT tasks also get 4 slices to avoid hogging forever if yielding is missed
			}
		}
	} else if (old_pb && old_pb->state != PBS::Ready && old_pb->state != PBS::Uninit) {
		// e.g. Pended or Hanging, already handled by DequeueReady elsewhere or below
	} else if (old_pb && old_pb->state == PBS::Ready) {
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
// auto Taskman::Schedule(void* timeout, ...)->decltype(Schedule(timeout))
#if (_MCCA & 0xFF00) == 0x8600
auto Taskman::Schedule(bool omit_slice)->decltype(Schedule())
{
	using PBS = ProcessBlock::State;
	#if _MCCA == 0x8632
	task_switch_enable = false;//{TODO} Lock, cpu0
	#endif
	stduint cpuid = getID();
	auto old_pb = Taskman::Locate(CurrentPID());
	if (!old_pb) {
		plogerro("Taskman::Schedule: No current task found for CPU %d (crtpid=%u)", cpuid, CurrentPID());
		HALT();
		return;
	}
	if (!omit_slice && ifContinueProcess(old_pb)) {
		#if _MCCA == 0x8632
		task_switch_enable = true;
		#endif
		return;
	}
	auto new_pb = PickNext();
	if (!new_pb) new_pb = Taskman::Locate(0);
	if (new_pb == old_pb) {
		if (old_pb->state == PBS::Ready) old_pb->state = PBS::Running;
		#if _MCCA == 0x8632
		task_switch_enable = true;
		#endif
		return;
	}
	CurrentPID() = new_pb->pid;
	if (new_pb->state == PBS::Uninit) new_pb->state = PBS::Ready;
	if (old_pb->state == PBS::Running && new_pb->state == PBS::Ready) {
		old_pb->state = PBS::Ready;
		new_pb->state = PBS::Running;
	} else if (new_pb->state == PBS::Ready) {
		new_pb->state = PBS::Running;
	} else {
		printlog(_LOG_FATAL, "task switch error (PID%u, State%u, PCnt%u).", new_pb->getID(), _IMM(new_pb->state), Taskman::chain.Count());
	}

	// ploginfo("switch %d->%d", old_pb->pid, new_pb->pid);
	#if _MCCA == 0x8632
	task_switch_enable = true;//{TODO} Unlock
	jmpTask(T_pid2tss(CurrentPID()));//[outdated]
	#else
	SwitchTaskContext(&new_pb->context, &old_pb->context);
	#endif
}
#else
auto Taskman::Schedule(bool omit_slice)->decltype(Schedule()) { }
#endif


