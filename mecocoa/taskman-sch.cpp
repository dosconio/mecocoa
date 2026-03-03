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


bool Taskman::Append(ProcessBlock* task) {
	task->pid = min_available_pid;
	auto nod = chain.Append(task, false, min_available_left);
	stduint las = min_available_pid;
	Dnode* lasn = nod;
	while (nod = nod->next) {
		stduint _new = ((ProcessBlock*)(nod->offs))->pid;
		if (_new == las + 1) {
			lasn = nod;
			las = _new;
		}
		else break;
	}
	min_available_pid = las + 1;
	task->state = ProcessBlock::State::Ready;
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

#if _MCCA == 0x8664
extern NormalTaskContext task_b_ctx, task_kernel_ctx;
// auto Taskman::Schedule(void* timeout, ...)->decltype(Schedule(timeout))
auto Taskman::Schedule()->decltype(Schedule())
{
	stduint cpuid = _TEMP 0;
	auto old_pb = PickNext();
	if (!old_pb) return; // No task ready
	
	// Timeslice management (A-Type context)
	if (old_pb->priority >= 0) {
		if (old_pb->time_slice > 0) {
			old_pb->time_slice--;
			return; // Continue same task
		} else {
			Taskman::DequeueReady(old_pb);
			
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
			return; // Continue same task
		} else {
			Taskman::DequeueReady(old_pb);
			Taskman::EnqueueReady(old_pb); // Re-queue at tail (Round-Robin for same RT priority)
			old_pb->time_slice = 4; // RT tasks also get 4 slices to avoid hogging forever if yielding is missed
		}
	}

	auto new_pb = PickNext();
	if (!new_pb || new_pb == old_pb) return;
	// ploginfo("switch %d->%d", old_pb->pid, new_pb->pid);
	SwitchTaskContext(&new_pb->context, &old_pb->context);
}
#endif

#ifdef _ARC_x86 // x86:

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
void switch_task() {
	using PBS = ProcessBlock::State;

	task_switch_enable = false;//{TODO} Lock

	auto pb_src = TaskGet(ProcessBlock::cpu0_task);
	if (!pb_src) pb_src = TaskGet(0); // fallback

	// Timeslice management for current task
	if (pb_src->state == PBS::Running) {
		if (pb_src->priority >= 0) {
			if (pb_src->time_slice > 0) {
				pb_src->time_slice--;
				task_switch_enable = true;
				return; // Continue same task
			} else {
				Taskman::DequeueReady(pb_src);
				pb_src->state = PBS::Ready;
				
				// Enqueue to Expired Queue for Epoch A-Type Scheduling
				Taskman::EnqueueExpired(pb_src); 
				
				// Allocation mapping:
				if (pb_src->priority <= 3) pb_src->time_slice = 4;
				else if (pb_src->priority <= 7) pb_src->time_slice = 3;
				else if (pb_src->priority <= 11) pb_src->time_slice = 2;
				else pb_src->time_slice = 1;
			}
		} else {
			// RT management (B-Type)
			if (pb_src->time_slice > 0) {
				pb_src->time_slice--;
				task_switch_enable = true;
				return; // Continue same task
			} else {
				Taskman::DequeueReady(pb_src);
				pb_src->state = PBS::Ready;
				Taskman::EnqueueReady(pb_src); // Re-queue at tail (Round-Robin for same RT priority)
				pb_src->time_slice = 4; // RT tasks also get 4 slices
			}
		}
	} else if (pb_src->state != PBS::Ready && pb_src->state != PBS::Uninit) {
		// e.g. Pended or Hanging, already handled by DequeueReady elsewhere or below
	} else if (pb_src->state == PBS::Ready) {
		// Nothing special, already in queue
	}

	auto pb_des = Taskman::PickNext();
	
	// If no task is ready, fallback to idle/kernel
	if (!pb_des) pb_des = TaskGet(0);
	
	if (pb_des == pb_src) {
		if (pb_src->state == PBS::Ready) pb_src->state = PBS::Running;
		task_switch_enable = true;
		return;
	}

	ProcessBlock::cpu0_task = pb_des->pid;

	if (pb_des->state == PBS::Uninit) pb_des->state = PBS::Ready;
	if ((pb_src->state == PBS::Running) && (pb_des->state == PBS::Ready)) {
		pb_src->state = PBS::Ready;
		pb_des->state = PBS::Running;
	} else if (pb_des->state == PBS::Ready) {
		pb_des->state = PBS::Running;
	}
	else {
		printlog(_LOG_FATAL, "task switch error (PID%u, State%u, PCnt%u).", pb_des->getID(), _IMM(pb_des->state), Taskman::chain.Count());
	}
	task_switch_enable = true;//{TODO} Unlock
	// ploginfo("switch task %d->%d", pb_src->pid, pb_des->pid);
	jmpTask(T_pid2tss(ProcessBlock::cpu0_task));
}

#endif
