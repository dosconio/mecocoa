// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Runtime Thread Operations
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

extern Spinlock scheduler_lock;

#if (_MCCA & 0xFF00) == 0x8600
static stduint get_thread_cs(stduint ring) {
	if (!ring) return __BITS__ == 64 ? SegCo64 : SegCo32;
	if (ring == RING_U) return _IMM(SegCoR3) | _IMM(ring);
	return 8 * ring + 0b100 | ring;
}

static stduint get_thread_ss(stduint ring) {
	if (!ring) return SegData;
	if (ring == RING_U) return _IMM(SegDaR3) | _IMM(ring);
	return 8 * (4 + ring) + 0b100 + ring;
}
#endif

static void init_first_user_context(NormalTaskContext& c, ProcessBlock* pb, stduint entry) {
	c = {};
	#if (_MCCA & 0xFF00) == 0x8600
	c.RING = pb->ring;
	c.IP = entry;
	c.CR3 = _IMM(pb->paging.root_level_page);
	REG_FLAG_t flag = {};
	flag._r1 = 1;
	flag.IF = 1;
	flag.IOPL = (c.RING == 1 ? 0x1u : 0u);
	c.FLAG = cast<stduint>(flag);
	c.CS = get_thread_cs(c.RING);
	c.SS = get_thread_ss(c.RING);
	#if _MCCA == 0x8632
	c.DS = c.SS;
	c.ES = c.SS;
	c.FS = c.SS;
	c.GS = c.SS;
	#elif _MCCA == 0x8664
	c.DS = 0;
	c.ES = 0;
	c.FS = 0;
	c.GS = 0;
	#endif
	treat<uint16>(&c.floating_point_context[0]) = 0x037F;
	treat<uint32>(&c.floating_point_context[24]) = 0x1F80;
	#elif _MCCA == 0x1032 || _MCCA == 0x1064
	constexpr stduint floating_support = (1 << 13);
	c.IP = entry;
	c.mstatus = (pb->ring << 11) | floating_support | _MSTATUS_MPIE;
	c.satp = _IMM(pb->paging.root_level_page);
	#endif
}

stdsint Taskman::CreateThread(ProcessBlock* pb, stduint entry, stduint arg, stduint stack_top) {
	if (!pb || !entry || !stack_top) return -1;
	auto current_th = CurrentTB();
	if (!current_th || current_th->parent_process != pb) return -1;
	ploginfo("[TNEW] pid=%u entry=%[x] arg=%[x] stack_top=%[x] heap=[%[x], %[x])",
		pb->pid, entry, arg, stack_top, pb->heapbtm, pb->heaptop);

	auto ensure_user_stack_page = [&](stduint addr) -> bool {
		if (_IMM(pb->paging[addr]) != ~_IMM0) return true;
		return kernel_prefault_page(pb, addr) != nullptr;
		};

	// User-space thread stacks are allocated lazily by mmap-backed malloc().
	// Prefault the top stack page before we write arguments to it (x86) or
	// let the new thread start using it (x64).
	if (!ensure_user_stack_page(stack_top - 1)) {
		plogerro("[TNEW] prefault failed at %[x]", stack_top - 1);
		return -1;
	}
	#if _MCCA == 0x8632
	if (!ensure_user_stack_page(stack_top - 2 * sizeof(stduint))) {
		plogerro("[TNEW] prefault failed at %[x]", stack_top - 2 * sizeof(stduint));
		return -1;
	}
	#endif

	auto tb = AllocateThread();
	if (!tb) return -1;

	init_first_user_context(tb->context, pb, entry);

	#if _MCCA == 0x8664
	tb->context.SP = stack_top;
	tb->context.DI = arg;
	// Enter the thread with the same stack layout as a normal function call:
	// keep the stack 16-byte aligned before the synthetic RET address push and
	// leave one dummy return slot for the wrapper function.
	stduint call_sp = (stack_top & ~_IMM(0xFul)) - sizeof(stduint);
	if (!ensure_user_stack_page(call_sp)) {
		plogerro("[TNEW] prefault failed at synthetic call_sp %[x]", call_sp);
		free((byte*)tb);
		return -1;
	}
	stduint zero = 0;
	MccaMemCopyP(
		(void*)call_sp, pb, false,
		&zero, nullptr, true,
		sizeof(stduint));
	tb->context.SP = call_sp;

	#elif _MCCA == 0x8632
	tb->context.SP = stack_top;
	if (stack_top) {
		stduint* sp = (stduint*)stack_top;
		sp--;
		MccaMemCopyP(
			sp, pb, false,
			& arg, nullptr, true,
			sizeof(stduint));
		sp--;
		stduint zero = 0;
		MccaMemCopyP(
			sp, pb, false,
			&zero, nullptr, true,
			sizeof(stduint));
		tb->context.SP = (stduint)sp;
	}
	#elif _MCCA == 0x1032 || _MCCA == 0x1064
	tb->context.a0 = arg;
	tb->context.sp = (stack_top & ~0xFul) - 0x10;
	#endif
	#if (_MCCA & 0xFF00) == 0x8600
	ploginfo("[TNEW_CTX] tid=%u ring=%u ip=%[x] sp=%[x] cs=%[x] ss=%[x] cr3=%[x]",
		tb->tid, tb->context.RING, tb->context.IP, tb->context.SP,
		tb->context.CS, tb->context.SS, tb->context.CR3);
	#elif _MCCA == 0x1032 || _MCCA == 0x1064
	ploginfo("[TNEW_CTX] tid=%u ring=%u ip=%[x] sp=%[x] mstatus=%[x] satp=%[x]",
		tb->tid, pb->ring, tb->context.IP, tb->context.sp,
		tb->context.mstatus, tb->context.satp);
	#endif

	tb->stack_size = HIGHER_STACK_SIZE;
	tb->stack_lineaddr = nullptr; // user-space stack is owned by user library
	tb->stack_levladdr = (byte*)mempool.allocate(tb->stack_size, 12);
	if (!tb->stack_levladdr) {
		free((byte*)tb);
		return -1;
	}
	// [DIAG] Write stack canary at the bottom of the kernel stack
	*(stduint*)tb->stack_levladdr = 0xDEADBEEF;
	#if _MCCA == 0x1032 || _MCCA == 0x1064
	tb->context.kernel_sp = _IMM(tb->stack_levladdr) + tb->stack_size - 0x10;
	#endif

	tb->priority = current_th->priority;
	tb->time_slice = current_th->time_slice;
	tb->parent_process = pb;

	{
		SpinlockLocal guard(&scheduler_lock);
		tb->process_thread_next = pb->thread_list_head;
		pb->thread_list_head = tb;
	}

	AppendThread(tb);
	return tb->tid;
}

stduint Taskman::CurrentThreadId() {
	return CurrentTID();
}

void Taskman::YieldThread() {
	Schedule(true);
}

bool Taskman::ExitThread(stduint code) {
	auto th = CurrentTB();
	if (!th) return false;
	auto pb = th->parent_process;
	if (!pb) return false;

	th->exit_status = code;
	if (th == pb->main_thread) {
		return ExitCurrent(code);
	}

	ThreadBlock* joiner = nullptr;
	{
		SpinlockLocal guard(&scheduler_lock);
		if (!th->join_wait_queue.isEmpty()) {
			th->join_wait_queue.Dequeue(joiner);
		}
	}
	if (joiner) {
		joiner->Unblock(ThreadBlock::BlockReason::BR_Lock);
	}

	// Remove the exiting worker thread from scheduler queues before marking it
	// non-runnable. Schedule(true) assumes Pended/Hanging threads have already
	// been dequeued by the caller.
	Taskman::DequeueReady(th);
	th->state = ThreadBlock::State::Hanging;
	th->block_reason = ThreadBlock::BlockReason::BR_None;
	Schedule(true);
	printlog(_LOG_FATAL, "Taskman::ExitThread unreachable");
	return false;
}

stdsint Taskman::JoinThread(stduint tid, stduint usr_status) {
	auto caller_th = CurrentTB();
	auto pb = CurrentPB();
	if (!caller_th || !pb) return -1;
	if (tid == caller_th->tid) return -1;

	auto target_th = LocateThread(tid);
	if (!target_th || target_th->parent_process != pb) return -1;
	if (target_th->is_detached) return -2;

	auto copy_exit_status = [&](ThreadBlock* th) {
		if (!usr_status) return;
		stduint code = th->exit_status;
		MccaMemCopyP(
			(void*)usr_status, pb, false,
			&code, nullptr, true,
			sizeof(stduint));
		};

	if (target_th->state == ThreadBlock::State::Hanging) {
		copy_exit_status(target_th);
		DestroyThread(target_th);
		return 0;
	}

	bool completed_while_locking = false;
	{
		SpinlockLocal guard(&scheduler_lock);
		if (target_th->state == ThreadBlock::State::Hanging) {
			completed_while_locking = true;
		}
		else {
		if (!target_th->join_wait_queue.isEmpty()) {
			return -3;
		}
		caller_th->state = ThreadBlock::State::Pended;
		caller_th->block_reason = ThreadBlock::BlockReason::BR_Lock;
		DequeueReady(caller_th, false);
		target_th->join_wait_queue.Enqueue(caller_th);
		}
	}
	if (completed_while_locking) {
		copy_exit_status(target_th);
		DestroyThread(target_th);
		return 0;
	}

	Schedule(true);

	target_th = LocateThread(tid);
	if (!target_th || target_th->parent_process != pb) {
		return -1;
	}
	copy_exit_status(target_th);
	DestroyThread(target_th);
	return 0;
}

stdsint Taskman::DetachThread(stduint tid) {
	auto caller_th = CurrentTB();
	auto pb = CurrentPB();
	if (!caller_th || !pb) return -1;

	auto target_th = LocateThread(tid);
	if (!target_th || target_th->parent_process != pb) return -1;
	if (target_th == pb->main_thread) return -1;
	if (tid == caller_th->tid) {
		target_th->is_detached = true;
		return 0;
	}

	{
		SpinlockLocal guard(&scheduler_lock);
		if (!target_th->join_wait_queue.isEmpty()) {
			return -2;
		}
		target_th->is_detached = true;
	}

	if (target_th->state == ThreadBlock::State::Hanging) {
		DestroyThread(target_th);
	}
	return 0;
}

stdsint Taskman::Futex(stduint addr, stduint op, stduint val) {
	auto th = CurrentTB();
	auto pb = CurrentPB();
	if (!th || !pb) return -1;

	if (op == 0) { // FUTEX_WAIT
		{
			SpinlockLocal guard(&scheduler_lock);
			uint32 current_val = 0;
			MccaMemCopyP(
				&current_val, nullptr, true,
				(void*)addr, pb, false,
				sizeof(current_val));
			if (current_val != (uint32)val) {
				return -1;
			}
			th->futex_wait_addr = addr;
			DequeueReady(th, false);
			th->state = ThreadBlock::State::Pended;
			th->block_reason = ThreadBlock::BlockReason::BR_Lock;
		}
		Schedule(true);
		return 0;
	}
	else if (op == 1) { // FUTEX_WAKE
		if (!val) return 0;
		stduint woken = 0;
		ThreadBlock* targets[64];
		stduint target_count = 0;
		{
			SpinlockLocal guard(&scheduler_lock);
			for (auto nod = Taskman::thchain.Root(); nod; nod = nod->next) {
				auto target_th = cast<ThreadBlock*>(nod->offs);
				if (target_th->parent_process != pb) continue;
				if (target_th->state != ThreadBlock::State::Pended) continue;
				if (target_th->block_reason != ThreadBlock::BlockReason::BR_Lock) continue;
				if (target_th->futex_wait_addr != addr) continue;
				target_th->futex_wait_addr = 0;
				if (target_count < numsof(targets)) {
					targets[target_count++] = target_th;
				}
				woken++;
				if (woken >= val) break;
			}
		}

		for0(i, target_count) {
			targets[i]->Unblock(ThreadBlock::BlockReason::BR_Lock);
		}
		return (stdsint)woken;
	}

	return -1;
}

extern "C" stdsint sysc_TNEW(stduint entry, stduint arg, stduint stack_top) {
	auto pb = Taskman::CurrentPB();
	if (!pb) return -1;
	return Taskman::CreateThread(pb, entry, arg, stack_top);
}

extern "C" stdsint sysc_TGET(stduint, stduint, stduint) {
	return (stdsint)Taskman::CurrentThreadId();
}

extern "C" stdsint sysc_TYLD(stduint, stduint, stduint) {
	Taskman::YieldThread();
	return 0;
}

extern "C" stdsint sysc_TEXI(stduint code, stduint, stduint) {
	Taskman::ExitThread(code);
	printlog(_LOG_FATAL, "sysc_TEXI unreachable");
	return -1;
}

extern "C" stdsint sysc_TJOI(stduint tid, stduint usr_status, stduint) {
	return Taskman::JoinThread(tid, usr_status);
}

extern "C" stdsint sysc_TDET(stduint tid, stduint, stduint) {
	return Taskman::DetachThread(tid);
}

extern "C" stdsint sysc_FUTX(stduint addr, stduint op, stduint val) {
	return Taskman::Futex(addr, op, val);
}
