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
	//
	//{} msg
	//{} files (part II)
	//{} h&s
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

	// : fileman
	for0(i, numsof(p->pfiles)) if (p->pfiles[i]) {
		p->pfiles[i]->vfile->f_inode->ref_count--;
		p->pfiles[i] = 0;
	}

	p->exit_status = exit_code;

	using PBS = ProcessBlock::State;
	if (pparent->isWaiting()) {
		// cast<stduint>(pparent->block_reason) &= ~_IMM(ProcessBlock::BlockReason::BR_Waiting);
		pparent->Unblock(ProcessBlock::BlockReason::BR_Waiting);
		// ploginfo("sending to parent %u", parent_pid);
		syssend(parent_pid, &args, sizeof(args));
		_Exit_Cleanup(pid);
	}
	else if (!p->parent_id) {
		_Exit_Cleanup(pid);
	}
	else {
		Taskman::DequeueReady(p);
		p->state = PBS::Hanging;
	}
	// if the proc has any child, make INIT the new parent, (or kill all the children >_<)
	auto nod = Taskman::chain.Root();
	while (nod) {
		auto taski = (ProcessBlock*)nod->offs;
		if (taski->pid && taski->parent_id == pid) {
			taski->parent_id = (Task_Init);
			if (Taskman::Locate(Task_Init)->isWaiting() && taski->state == PBS::Hanging) {
				pparent->Unblock(ProcessBlock::BlockReason::BR_Waiting);
				syssend(Task_Init, &args, sizeof(args));
				_Exit_Cleanup(taski->pid);
			}
		}
		nod = nod->next;
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

#ifdef _ARC_x86
/// {} TODO
stduint task_exec(stduint pid, void* fullpath, void* argstack, stduint stacklen)// return nil if OK
{
	ploginfo("%s: %u %x %x %x", pid, fullpath, argstack, stacklen);
/*
	// get parameters from the message
	int name_len = mm_msg.NAME_LEN;	// length of filename
	int src = mm_msg.source;	// caller proc nr.
	assert(name_len < MAX_PATH);

	char pathname[MAX_PATH];
	phys_copy((void*)va2la(TASK_MM, pathname),
		(void*)va2la(src, mm_msg.PATHNAME),
		name_len);
	pathname[name_len] = 0;	// terminate the string

	// get the file size
	struct stat s;
	int ret = stat(pathname, &s);
	if (ret != 0) {
		printl("{MM} MM::do_exec()::stat() returns error. %s", pathname);
		return -1;
	}

	// read the file
	int fd = open(pathname, O_RDWR);
	if (fd == -1)
		return -1;
	assert(s.st_size < MMBUF_SIZE);
	read(fd, mmbuf, s.st_size);
	close(fd);

	// overwrite the current proc image with the new one
	Elf32_Ehdr* elf_hdr = (Elf32_Ehdr*)(mmbuf);
	int i;
	for (i = 0; i < elf_hdr->e_phnum; i++) {
		Elf32_Phdr* prog_hdr = (Elf32_Phdr*)(mmbuf + elf_hdr->e_phoff +
			(i * elf_hdr->e_phentsize));
		if (prog_hdr->p_type == PT_LOAD) {
			assert(prog_hdr->p_vaddr + prog_hdr->p_memsz <
				PROC_IMAGE_SIZE_DEFAULT);
			phys_copy((void*)va2la(src, (void*)prog_hdr->p_vaddr),
				(void*)va2la(TASK_MM,
					mmbuf + prog_hdr->p_offset),
				prog_hdr->p_filesz);
		}
	}

	// setup the arg stack
	int orig_stack_len = mm_msg.BUF_LEN;
	char stackcopy[PROC_ORIGIN_STACK];
	phys_copy((void*)va2la(TASK_MM, stackcopy),
		(void*)va2la(src, mm_msg.BUF),
		orig_stack_len);

	u8* orig_stack = (u8*)(PROC_IMAGE_SIZE_DEFAULT - PROC_ORIGIN_STACK);

	int delta = (int)orig_stack - (int)mm_msg.BUF;

	int argc = 0;
	if (orig_stack_len) {	// has args
		char** q = (char**)stackcopy;
		for (; *q != 0; q++, argc++)
			*q += delta;
	}

	phys_copy((void*)va2la(src, orig_stack),
		(void*)va2la(TASK_MM, stackcopy),
		orig_stack_len);

	proc_table[src].regs.ecx = argc;
	proc_table[src].regs.eax = (u32)orig_stack;

	// setup eip & esp
	proc_table[src].regs.eip = elf_hdr->e_entry; // @see _start.asm
	proc_table[src].regs.esp = PROC_IMAGE_SIZE_DEFAULT - PROC_ORIGIN_STACK;

	strcpy(proc_table[src].name, pathname);
*/
	return 0;
}

#endif


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
			to_args[0] = sig_src;
			ret = task_exec(to_args[0], (void*)to_args[1], (void*)to_args[2], to_args[3]);
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
