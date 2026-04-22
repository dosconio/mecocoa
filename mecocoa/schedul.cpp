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
#endif

Dchain Taskman::chain = { DnodeHeapFreeSimple };;// ordered by pid
stduint Taskman::min_available_pid;// in chain
Dnode* Taskman::min_available_left;;

stduint Taskman::PCU_CORES = 0;
ThreadBlock* Taskman::current_thread[PCU_CORES_MAX];
ThreadBlock* Taskman::idle_thread[PCU_CORES_MAX];
ThreadBlock* volatile Taskman::switching_out_threads[PCU_CORES_MAX];
stduint Taskman::min_available_tid = 0;
Dchain Taskman::thchain = { nullptr };
Dnode* Taskman::min_available_thleft = nullptr;


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
	uint32 original_bitmap = ready_bitmap;
	while (original_bitmap) {
		uint32 idx = __builtin_ctz(original_bitmap);
		ThreadBlock* node = priority_queues[idx].head;
		while (node) {
			if (!node->just_schedule) {
				// Check whether node is in the array: 
				for (int i = 0; i < PCU_CORES_MAX; i++) {
					if (switching_out_threads[i] == node) {
						switching_out_threads[i] = nullptr;
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

static stduint next_global_id = 1;

bool Taskman::Append(ProcessBlock* task) {
	task->pid = __atomic_fetch_add(&next_global_id, 1, __ATOMIC_SEQ_CST);

	Dnode* insert_after = chain.Last();
	// Dnode* insert_after = nullptr;
	// for (auto nod = chain.Root(); nod; nod = nod->next) {
	// 	if (cast<ProcessBlock*>(nod->offs)->pid > task->pid) break;
	// 	insert_after = nod;
	// }

	chain.Append(task, false, insert_after);
	task->state = ProcessBlock::State::Active;
	return true;
}

bool Taskman::AppendThread(ThreadBlock* task) {
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
static Spinlock scheduler_lock;
auto Taskman::Schedule(bool omit_slice)->decltype(Schedule())
{
	bool old_if = scheduler_lock.Acquire();
	using TBS = ThreadBlock::State;
	stduint cpuid = getID();
	
	// Release the CPU's hold on switching output thread in clock cycle.
	if (switching_out_threads[cpuid] != nullptr) {
		switching_out_threads[cpuid] = nullptr;
	}
	
	auto old_tb = current_thread[cpuid];
	if (!old_tb) {
		plogerro("Taskman:Schedule: No current task found for CPU %d", cpuid);
		HALT();
		return;
	}
	bool is_idle = (old_tb == idle_thread[cpuid]);
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
			Taskman::DequeueReady(old_tb);
			old_tb->state = TBS::Ready;
			if (s == TBS::Running || s == TBS::Ready) Taskman::EnqueueExpired(old_tb);
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
		} else {
			new_tb = idle_thread[cpuid];
			// Just in case idle_thread isn't set up yet, spin locally
			if (!new_tb) {
				scheduler_lock.Release(old_if);
				do {
					IC.enAble();
					HALT();
					IC.enAble(false);
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
	} else if (new_tb->state == TBS::Ready) {
		new_tb->state = TBS::Running;
	} else {
		printlog(_LOG_FATAL, "task switch error state (TID%u, State%u)", new_tb->tid, _IMM(new_tb->state));
	}

	current_thread[cpuid] = new_tb;

	old_tb->just_schedule = 1;
	switching_out_threads[cpuid] = old_tb;

	scheduler_lock.Release(old_if);
	SwitchTaskContext(&new_tb->context, &old_tb->context);
}
#else
auto Taskman::Schedule(bool omit_slice)->decltype(Schedule()) { }
#endif
void Taskman::SleepAndRelease(Spinlock* lk) {
	auto old_tb = current_thread[getID()];
	bool old_if = scheduler_lock.Acquire();
	DequeueReady(old_tb);
	lk->Release(false);

	auto new_tb = PickNext();
	if (!new_tb) {
		new_tb = idle_thread[getID()];
		if (!new_tb) {// idle is not ready
			scheduler_lock.Release(old_if);
			do {
				IC.enAble();
				HALT();
				IC.enAble(false);
			} while (!(new_tb = PickNext()));
			old_if = scheduler_lock.Acquire();
		}
	}

	current_thread[getID()] = new_tb;
	new_tb->state = ThreadBlock::State::Running;
	
	old_tb->just_schedule = 1;
	switching_out_threads[getID()] = old_tb;
	
	scheduler_lock.Release(old_if);
	// No explicit unlock of scheduler_lock here, because earlier we unlock lk, but wait!
	SwitchTaskContext(&new_tb->context, &old_tb->context);
}
