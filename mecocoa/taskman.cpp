// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include "../include/filesys.hpp"

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

// ---- . ----

auto Taskman::AllocateTask() -> ProcessBlock* {
	// auto ppb = zalcof(ProcessBlock);
	auto ppb = (ProcessBlock*)mempool.allocate(sizeof(ProcessBlock), 4);
	MemSet(ppb, 0, sizeof(ProcessBlock));
	if (!ppb) {
		plogerro("Taskman::AllocateTask() failed");
		return nullptr;
	}
	return ppb;
}

void Taskman::DumpTask(ProcessBlock* pb) {
	auto ctx = &pb->context;
	ploginfo("=== Context [%d] at %[x] ===", pb->getID(), pb);
	ploginfo(" (%u:%u) head %u, next %u, send_to_whom",
		pb->state, pb->block_reason, pb->queue_send_queuehead, pb->queue_send_queuenext);
	#if (_MCCA & 0xFF00) == 0x1000
    ploginfo("  ra : %[x]  sp : %[x]  gp : %[x]  tp : %[x]", ctx->ra, ctx->sp, ctx->gp, ctx->tp);
    ploginfo("  a0 : %[x]  a1 : %[x]  a7 : %[x]  s0 : %[x]", ctx->a0, ctx->a1, ctx->a7, ctx->s0);
    ploginfo("  mepc: %[x]  mstatus: %[x]  satp: %[x]", ctx->mepc, ctx->mstatus, ctx->satp);
    ploginfo("  ksp : %[x]", ctx->kernel_sp);
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
// - heaps, stacks
// - paging
static void _Exit_Cleanup(stduint pid)
{
	#if defined(_DEBUG) && defined(_MCCA) && (_MCCA & 0xFF00) == 0x8600
	auto flag = getFlags();
	if (flag & 0x200) InterruptDisable();
	#endif
	auto ppb = Taskman::Locate(pid);
	ploginfo("cleaning %u", pid);
	Taskman::DequeueReady(ppb);
	// Msg: Clear IPC
	ProcessBlock* sender = ppb->queue_send_queuehead;
	while (sender) {
		ProcessBlock* next = sender->queue_send_queuenext;
		sender->Unblock(ProcessBlock::BlockReason::BR_SendMsg);
		sender = next;
	}// wakeup senders
	ppb->queue_send_queuehead = nullptr;
	// Release User and Kernel Stack
	if (ppb->stack_lineaddr == ppb->stack_levladdr) {// Ring_M App use one stack (R0 for x86)
		delete (byte*)ppb->stack_levladdr;
	}
	else {
		delete (byte*)ppb->paging[_IMM(ppb->stack_lineaddr)];
		delete (byte*)ppb->stack_levladdr;
	}
	// Release Segments
	for0a(i, ppb->load_slices) {
		if (!ppb->load_slices[i].address) break;
		if (ppb->load_slices[i].length >= PAGE_SIZE&&
			(byte*)ppb->paging[(ppb->load_slices[i].address) & ~0xFFF] + PAGE_SIZE ==
			(byte*)ppb->paging[(ppb->load_slices[i].address + PAGE_SIZE) & ~0xFFF]) {// once allocated
			// ploginfo("freeing segment: %[x] %[x]", ppb->load_slices[i].address, ppb->load_slices[i].length);
			delete (byte*)ppb->paging[(ppb->load_slices[i].address) & ~0xFFF];
		}
		else while (ppb->load_slices[i].length) {
			// ploginfo("freeing segment: %[x] %[x]", ppb->load_slices[i].address, ppb->load_slices[i].length);
			delete (byte*)ppb->paging[(ppb->load_slices[i].address) & ~0xFFF];
			ppb->load_slices[i].address += 0x1000;
			ppb->load_slices[i].length -= minof(0x1000, ppb->load_slices[i].length);
		}
	}
	// Heap
	if (1) {

	}
	ppb->paging.~Paging();
	ppb->state = ProcessBlock::State::Invalid;
	Taskman::chain.Remove(ppb);
	#if defined(_DEBUG) && defined(_MCCA) && (_MCCA & 0xFF00) == 0x8600
	setFlags(flag);
	#endif
}

bool Taskman::Exit(ProcessBlock* p, stdsint exit_code)
{
	const auto pid = p->getID();
	auto parent_pid = p->parent_id;
	ProcessBlock* pparent = Taskman::Locate(parent_pid);
	stduint args[2] = { pid, _IMM(exit_code) };

	// Release Files First
	for0(i, numsof(p->pfiles)) if (p->pfiles[i]) {
		p->Close(i);// p->pfiles[i]->vfile->f_inode->ref_count--;
		p->pfiles[i] = 0;
	}

	p->exit_status = exit_code;

	// Handle Children (Reparenting to Task_Init)
	// Must do this BEFORE cleaning up 'p' to ensure memory safety
	auto nod = Taskman::chain.Root();
	while (nod) {
		auto taski = (ProcessBlock*)nod->offs;
		auto next_nod = nod->next;

		if (taski->pid && taski->parent_id == pid) {
			taski->parent_id = Task_Init; // Reparent

			auto init_pb = Taskman::Locate(Task_Init);
			using PBS = ProcessBlock::State;

			// If Init is waiting and this child is already a zombie
			if (init_pb->isWaiting() && taski->state == PBS::Hanging) {
				init_pb->Unblock(ProcessBlock::BlockReason::BR_Waiting);

				stduint child_args[2] = { taski->pid, taski->exit_status };
				syssend(Task_Init, &child_args, sizeof(child_args));

				_Exit_Cleanup(taski->pid);
			}
		}
		nod = next_nod; // Safely move to the next node
	}

	// Handle Self (Notify Parent or become Zombie)
	using PBS = ProcessBlock::State;
	if (pparent && pparent->isWaiting()) {
		pparent->Unblock(ProcessBlock::BlockReason::BR_Waiting);
		syssend(parent_pid, &args, sizeof(args));
		_Exit_Cleanup(pid); // Die completely
	}
	else if (!parent_pid) {
		_Exit_Cleanup(pid); // No parent (usually only Init or Kernel), die completely
	}
	else {
		Taskman::DequeueReady(p);
		p->state = PBS::Hanging; // Become a Zombie and wait for parent to call Wait()
	}

	return true;
}

stdsint Taskman::Wait(ProcessBlock* pb)
{
	const auto pid = pb->getID();
	stduint children = 0;
	for (auto nod = Taskman::chain.Root(); nod; nod = nod->next) {
		auto taski = (ProcessBlock*)nod->offs;
		if (taski->pid && taski->parent_id == pid) {
			children++;
			if (taski->state == ProcessBlock::State::Hanging) {
				stduint args[2] = { pid, taski->exit_status };
				stduint child_pid = taski->pid;
				_Exit_Cleanup(child_pid);
				children = child_pid;
				syssend(pid, args, sizeof(args));// return 0
				return child_pid;
			}
		}
	}
	if (children) {// no child is HANGING
		pb->Block(ProcessBlock::BlockReason::BR_Waiting);
	}
	else { // no any child
		syssend(pid, &children, sizeof(children));// return 0
	}
	return ~_IMM0;
}

ProcessBlock* Taskman::Exec(stduint parent, rostr usr_fullpath, void* usr_argstack, stduint stacklen)
{
	char buf_fullpath[_TEMP 32];
	auto parent_pb = Locate(parent);
	StrCopyP(buf_fullpath, kernel_paging, usr_fullpath, parent_pb->paging, sizeof(buf_fullpath));
	ploginfo("%s: %u \"%s\" %x %x", __FUNCIDEN__, parent, buf_fullpath, usr_argstack, stacklen);

	auto new_pb = Taskman::CreateFile(buf_fullpath, RING_U, parent);
	if (!new_pb) {
		plogwarn("%s: failed to load file", __FUNCIDEN__);
		return nullptr;
	}

	stduint argc = 0;
	stduint argv_ptr = 0;
	stduint new_sp = new_pb->context.SP;

	if (stacklen > 0 && usr_argstack != nullptr) {
		byte* temp_stack = new byte[stacklen];
		if (!temp_stack) {
			plogerro("%s: alloc temp_stack fail", __FUNCIDEN__);
		}
		else {
			MemCopyP(temp_stack, kernel_paging, usr_argstack, parent_pb->paging, stacklen);
			new_sp = (new_sp - stacklen) & ~_IMM(0xFlu);
			stduint delta = new_sp - _IMM(usr_argstack);

			char** q = (char**)temp_stack;
			for (; *q != nullptr; q++, argc++) {
				*q = (char*)(_IMM(*q) + delta);
			}
			MemCopyP((void*)new_sp, new_pb->paging, temp_stack, kernel_paging, stacklen);
			argv_ptr = new_sp;
			delete[] temp_stack;
		}
	}
	else {
		new_sp = (new_sp - sizeof(stduint)) & ~_IMM(0xFlu);
		stduint null_ptr = 0;
		MemCopyP((void*)new_sp, new_pb->paging, &null_ptr, kernel_paging, sizeof(stduint));
		argv_ptr = new_sp;
	}

	// [!] Unconditionally construct the C standard call frame for _start
	#if (_MCCA & 0xFF00) == 0x8600

	// Construct standard C call frame for x86: [Dummy Return IP, argc, argv]
	struct C_CallFrame {
		stduint dummy_return_ip;
		stduint argc;
		stduint argv;
	};

	C_CallFrame frame = { 0, argc, argv_ptr };
	new_sp = new_sp & ~_IMM(0xFlu);
	// System V ABI : ESP % 16 == 12
	new_sp = new_sp - 20;// new_sp -= sizeof(call_frame);
	MemCopyP((void*)new_sp, new_pb->paging, (void*)&frame, kernel_paging, sizeof(frame));
	new_pb->context.SP = new_sp;	// esp = new stack pointer

	#elif (_MCCA & 0xFF00) == 0x1000

	new_pb->context.a0 = argc; // a0 = argc
	new_pb->context.a1 = argv_ptr; // a1 = argv
	new_pb->context.sp = new_sp; // sp = new stack pointer

	#endif
	new_pb->focus_tty = parent_pb->focus_tty;
	Taskman::Append(new_pb);
	return new_pb;
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
			Taskman::Exit(Taskman::Locate(to_args[0]), to_args[1]);
			break;
		#ifdef _ARC_x86
		case TaskmanMsg::FORK: // (pid, cframe)
			// ploginfo("Taskman fork: %u", to_args[0]);
			if (1) {
				auto child_ppb = Taskman::CreateFork(Taskman::Locate(to_args[0]), (CallgateFrame*)to_args[1]);
				ret = child_ppb ? child_ppb->getID() : ~_IMM0;
			}
			syssend(sig_src, (void*)&ret, sizeof(ret));
			break;
		case TaskmanMsg::WAIT: // (pid) -> (*)
			// ploginfo("Taskman wait: %u %x", to_args[0], to_args[1]);
			Taskman::Wait(Taskman::Locate(to_args[0]));// send back { pid, taski->exit_status }
			break;
		case TaskmanMsg::EXEC:// (pid, &usr:fullpath, &usr:argstack, stacklen) -> 0(success)
			pb = Taskman::Exec(to_args[0], (rostr)to_args[1], (void*)to_args[2], to_args[3]);
			ret = pb ? pb->getID() : 0;
			syssend(sig_src, (void*)&ret, sizeof(ret));
			break;
		#endif

		default:
			plogerro("Bad TYPE %u in %s %s", sig_type, __FILE__, __FUNCIDEN__);
			break;
		}
		sysrecv(ANYPROC, (void*)to_args, byteof(to_args), (stduint *)&sig_type, (stduint *)&sig_src);
	}
}

#endif
