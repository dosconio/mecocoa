// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Syscalls
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <cpp/Device/UART>
#include <c/driver/timer.h>
#include <c/driver/keyboard.h>
#include <c/proctrl/IAx86_64.msr.h>

#define DEFSYSC extern "C" stdsint

// Kernel Signal Functions
extern "C" stduint sys_sigaction(int, const struct _POSIX_sigaction*, struct _POSIX_sigaction*);
extern "C" stduint sys_kill(stduint, int, stduint);
extern "C" stdsint sysc_SIGR(void* context);
extern "C" void check_and_deliver_signals(void* context);

// Syscall Wrappers
extern stduint SYSCALL_TABLE[22];

void Syscall::Initialize() {
	#if _MCCA == 0x8632
	IC[IRQ_SYSCALL].setRange(mglb(Handint_INTCALL_Entry), SegCo32); IC[IRQ_SYSCALL].DPL = 3;

	#elif _MCCA == 0x8664
	setMSR(x86MSR::EFER, 0x0501);
	setMSR(x86MSR::LSTAR, mglb(Handint_SYSCALL_Entry));
	setMSR(x86MSR::STAR, (_IMM(SegCo64) << 32) | (_IMM(SegCo32 | _IMM(RING_U)) << 48));
	setMSR(x86MSR::FMASK, 0x200);

	#endif
}

#if (_MCCA & 0xFF00) == 0x8600 || (_MCCA & 0xFF00) == 0x1000

__attribute__((optimize("O0")))
stduint syscall(syscall_t callid, stduint para1, stduint para2, stduint para3) {
	stduint ret;
	#if   _MCCA == 0x8632
	__asm("mov  %0, %%ebx" : : "m"(para3));
	__asm("mov  %0, %%edx" : : "m"(para2));
	__asm("mov  %0, %%ecx" : : "m"(para1));
	__asm("mov  %0, %%eax" : : "m"(callid));
	CallFar(0, SegCall);
	__asm("mov  %%eax, %0" : "=m"(ret));
	#elif _MCCA == 0x8664
	auto pb = Taskman::current_thread[Taskman::getID()]->parent_process;
	auto old_dir = pb->paging_redirect;
	pb->paging_redirect = &kernel_paging;
	if (_IMM(callid) >= numsof(SYSCALL_TABLE)) {
		plogerro("syscall: callid out of range: %u", callid);
		loop HALT();
	}
	ret = reinterpret_cast<stdsint(*)(stduint, stduint, stduint)>(SYSCALL_TABLE[_IMM(callid)])(para1, para2, para3);
	pb->paging_redirect = 0;// old_dir;
	// if (old_dir) ploginfo(">>> %x", old_dir);
	return ret;
	#elif (_MCCA & 0xFF00) == 0x1000
	void syscall_body(NormalTaskContext * cxt);
	auto th = Taskman::current_thread[Taskman::getID()];
	th->context.a7 = _IMM(callid);
	th->context.a0 = para1;
	th->context.a1 = para2;
	th->context.a2 = para3;
	syscall_body(&th->context);
	ret = th->context.a0;
	#endif
	return ret;
}

Mutex outc_mutex;// for con-io
DEFSYSC sysc_OUTC(stduint ch, stduint len) {
	// ploginfo("sysc_OUTC: ch = %[x], len = %[x]", ch, len);
	MutexLocal mutex(&outc_mutex);
	auto th = Taskman::current_thread[Taskman::getID()];
	if (auto pid = th->parent_process) {
		if (auto con = pid->focus_tty ? (Console_t*)pid->focus_tty->offs : 0)
		{
			if (!len) con->OutChar(ch);
			else while (len) {
				stduint crt_len = 0x1000 - (ch & 0xFFF);
				if (len < crt_len) crt_len = len;
				auto pa = mglb(pid->paging[ch]);
				if (_IMM(pa) == ~_IMM0) {
					plogerro("OUTC");
					return -1;
				}
				con->out((rostr)pa, crt_len);
				ch += crt_len;
				len -= crt_len;
			}
			// con->OutChar(nil);
			return 0;
		}
		else plogerro("sysc_OUTC: pid = %u", pid->getID());
	}
	else plogerro("sysc_try: null task");
	return -1;
}

DEFSYSC sysc_INNC(stduint blocked) {
	usize ret = ~_IMM0;
	usize msgbuf[4];
	int ch = -1;
	// Read from its TTY, return -1 if the TTY is occupied
	auto th = Taskman::current_thread[Taskman::getID()];
	auto ppb = th->parent_process;
	if (blocked) {
		msgbuf[3] = ppb->getID();
		ppb->paging_redirect = nil;
		syssend(Task_Console, sliceof(msgbuf), _IMM(ConsoleMsg::INNC));//
		sysrecv(Task_Console, &ret, byteof(ret));// need Context
	}
	else if (ppb->focus_tty) {
		QueueLimited* q = VTTY_INNQ(ppb->focus_tty);
		if (q && -1 != (ch = q->inn())) {
			ret = ch;
		}
	}
	return ret;
}

DEFSYSC sysc_EXIT(stduint code) {
	// IC.enInterrupt(false);
	Taskman::ExitCurrent(code);
	printlog(_LOG_FATAL, "sysc_EXIT unreachable");// unreachable
	return -1;
}

DEFSYSC sysc_TIME(stduint unit) {
	switch (unit) {
	case 0:// second
		return tick / SysTickFreq;// mecocoa_global->system_time.sec;
	case 1:// ms
		return tick * 1000 / SysTickFreq;
	default:
		plogwarn("Invalid time unit: %u", unit);
		return -1;
	}
}

static void _TimerWakeUp(pureptr_t, stduint th_ptr) {
	if (auto th = (ThreadBlock*)th_ptr) {
		th->Unblock(ThreadBlock::BlockReason::BR_Resting);
	}
}

DEFSYSC sysc_REST(stduint unit, stduint time) {
	if (time == 0) {
		Taskman::Schedule(true);
		return 0;
	}
	
	stduint timeout = 0;
	switch (unit) {
	case 0: // second
		timeout = time * SysTickFreq;
		break;
	case 1: // ms
		timeout = time * SysTickFreq / 1000;
		if (!timeout) timeout = 1; // At least 1 tick
		break;
	default:
		plogwarn("Invalid time unit: %u", unit);
		return -1;
	}

	auto th = Taskman::current_thread[Taskman::getID()];
	th->Block(ThreadBlock::BlockReason::BR_Resting); // Block the thread to wait for timer
	SysTimer::Append(timeout, (stduint)th, (_tocall_ft)_TimerWakeUp);
	
	Taskman::Schedule(true);
	if (_sigset_raw(&th->pending_signals) & ~_sigset_raw(&th->blocked_signals)) {
		return -4; // -EINTR
	}
	return 0;
}

DEFSYSC sysc_COMM(stduint op, stduint to, stduint msg) {
	auto th = Taskman::current_thread[Taskman::getID()];
	usize ret = 0;
	// ploginfo("sysc_COMM:[th%u] op = %x, to = %x, msg = %x", th->getID(), op, to, msg);
	if (op & (COMM_SEND | COMM_SEND_ASYNC)) { // SEND or ASYNC_SEND
		// ploginfo("%d --> %d: 0x%[x]", pb->getID(), to, msg);
		if (ret = msg_send(th, to, (CommMsg*)msg, (op & COMM_SEND_ASYNC) != 0)) return ret;
	}
	if (op & COMM_RECV) { // RECV
		// if (to && to != ~_IMM0) ploginfo("%d <-- %d", pb->getID(), to);
		ret = msg_recv(th, to, (CommMsg*)msg);
	}
	if (op & (COMM_SEND | COMM_SEND_ASYNC | COMM_RECV)); else {
		plogerro("Invalid op %x", op);
		return 0xF0F0;
	}
	if (th->state == ThreadBlock::State::Pended) {
		Taskman::Schedule(true);
	}
	if (_sigset_raw(&th->pending_signals) & ~_sigset_raw(&th->blocked_signals)) {
		return -4; // -EINTR
	}
	return ret;
}

DEFSYSC sysc_OPEN(stduint usr_filepath, stduint flag)
{
	ThreadBlock* th = Taskman::current_thread[Taskman::getID()];
	stdsint open_buf[1];
	ProcessBlock* pb = th->parent_process;
	struct { stduint flag;stduint pid;rostr usr_filepath; } open_msg;
	open_msg.flag = flag;
	open_msg.pid = th->getID();
	open_msg.usr_filepath = (rostr)usr_filepath;
	syssend(Task_FileSys, &open_msg, sizeof(open_msg), _IMM(FilemanMsg::OPEN));
	sysrecv(Task_FileSys, open_buf, byteof(open_buf));
	return open_buf[0];
}

DEFSYSC sysc_CLOS(stduint fd) {
	ThreadBlock* th = Taskman::current_thread[Taskman::getID()];
	stdsint open_buf[1];
	struct { stduint fd;stduint pid; } open_msg = { fd , th->parent_process->pid };
	syssend(Task_FileSys, &open_msg, sizeof(open_msg), 0x03);
	sysrecv(Task_FileSys, open_buf, byteof(open_buf));
	return open_buf[0];// 0 for success
}

DEFSYSC sysc_READ(stduint fd, stduint addr, stduint len) {
	ThreadBlock* th = Taskman::current_thread[Taskman::getID()];
	ProcessBlock* pb = th->parent_process;

	// TTY device
	if (asrtand(pb->pfiles[fd])->vfile && 
	    asrtand(pb->pfiles[fd]->vfile->f_dentry)->d_inode &&
		pb->pfiles[fd]->vfile->f_dentry->d_inode->i_mode == I_CHAR_SPECIAL) {		
		// char device, read blockedly
		stduint total_read = 0;
		while (total_read < len) {
			stduint open_buf[4]{ fd, addr + total_read, len - total_read, th->getID() };
			syssend(Task_FileSys, open_buf, byteof(open_buf), _IMM(FilemanMsg::READ));
			stduint r1 = sysrecv(Task_FileSys, open_buf, byteof(open_buf[0]));
			if ((stdsint)r1 < 0) return r1; // Return -EINTR if interrupted

			stduint bytes_read = open_buf[0];
			if (bytes_read == 0) {
				syssend(Task_Console, 0, 0, _IMM(ConsoleMsg::INNC));
				stduint ret;
				stduint r2 = sysrecv(Task_Console, &ret, byteof(ret));
				if ((stdsint)r2 < 0) return r2; // Return -EINTR if interrupted
				if (ret == ~_IMM0) break;

				// Identify the specific vtty device for echo
				stduint tty_idx = (stduint)pb->pfiles[fd]->vfile->f_inode->internal_handler;
				Console_t* con = nullptr;
				if (tty_idx == (stduint)~0) {
					if (pb->focus_tty) con = (Console_t*)pb->focus_tty->offs;
				} else if (tty_idx < vttys.Count()) {
					con = (Console_t*)vttys[tty_idx]->offs;
				}

				// Handle the character "consumed" by Task_Console service
				byte ch = (byte)ret;
				if (ch == '\b' || ch == 0x7F) {
					if (total_read > 0) {
						total_read--;
						if (con) con->out("\b \b", 3);
					}
					continue;
				}
				// Store in user buffer and provide visual echo
				MccaMemCopyP((void*)(addr + total_read), pb, &ch, NULL, 1);
				total_read++;
				if (con) con->OutChar(ch);
				
				if (ch == '\n') {
					con->OutChar('\r');
					break;
				}
				continue;
			}

			total_read += bytes_read;

			if (total_read > 0) {
				byte last_char;
				MccaMemCopyP(&last_char, NULL, (void*)(addr + total_read - 1), pb, 1);
				if (last_char == '\n') {
					break;
				}
			}
		}
		return total_read;
	}
	else {
		stduint open_buf[4]{ fd, addr, len, th->getID() };
		syssend(Task_FileSys, open_buf, byteof(open_buf), _IMM(FilemanMsg::READ));
		sysrecv(Task_FileSys, open_buf, byteof(open_buf[0]));
		return open_buf[0];
	}
}
DEFSYSC sysc_WRIT(stduint fd, stduint addr, stduint len) {
	stduint open_buf[4]{ fd, addr,len,Taskman::current_thread[Taskman::getID()]->getID() };
	syssend(Task_FileSys, open_buf, byteof(open_buf), _IMM(FilemanMsg::WRITE));
	sysrecv(Task_FileSys, open_buf, byteof(open_buf[0]));
	return open_buf[0];
}

DEFSYSC sysc_DELF(stduint usr_filepath) {
	stduint open_buf[2]{ usr_filepath,Taskman::current_thread[Taskman::getID()]->getID() };
	syssend(Task_FileSys, open_buf, byteof(open_buf), _IMM(FilemanMsg::REMOVE));
	sysrecv(Task_FileSys, open_buf, byteof(open_buf[0]));
	return open_buf[0];// 0 for success
}

DEFSYSC sysc_WAIT(stduint pid, stduint usr_status) {
	// ploginfo("syscall wait");
	auto tb = Taskman::current_thread[Taskman::getID()];
	stduint open_buf[3]{ tb->parent_process->getID(), pid, usr_status };
	syssend(Task_TaskMan, sliceof(open_buf), _IMM(TaskmanMsg::WAIT));
	stduint r = sysrecv(Task_TaskMan, sliceof(open_buf));// (pid, state)
	if ((stdsint)r < 0) return r; // Return -EINTR if interrupted
	stdsint ret = open_buf[0];// pid
	if (ret && usr_status) {
		auto pb = tb->parent_process;
		MccaMemCopyP((void*)usr_status, pb, &open_buf[1], NULL, byteof(stduint));
	}
	return ret;
}

DEFSYSC sysx_FORK(CallgateFrame* phyzik_frame) {
	stduint msgbuf[2] = { Taskman::current_thread[Taskman::getID()]->parent_process->getID(), _IMM(phyzik_frame) };
	syssend(Task_TaskMan, sliceof(msgbuf), _IMM(TaskmanMsg::FORK));
	sysrecv(Task_TaskMan, &msgbuf, byteof(msgbuf[0]));
	return msgbuf[0];
}
DEFSYSC sysc_TMSG() {
	auto th = Taskman::current_thread[Taskman::getID()];
	return _IMM(th->queue_send_queuehead);
}

DEFSYSC sysc_EXEC(stduint path, stduint argv, stduint envp) {
	stduint msgbuf[4] = { Taskman::current_thread[Taskman::getID()]->parent_process->getID(), path, argv, envp };
	syssend(Task_TaskMan, sliceof(msgbuf), _IMM(TaskmanMsg::EXEC));
	sysrecv(Task_TaskMan, &msgbuf, byteof(msgbuf[0]));
	return msgbuf[0];
}
DEFSYSC sysc_EXET(stduint path, stduint argv, stduint envp) {
	stduint msgbuf[4] = { Taskman::current_thread[Taskman::getID()]->parent_process->getID(), path, argv, envp };
	syssend(Task_TaskMan, sliceof(msgbuf), _IMM(TaskmanMsg::EXET));
	sysrecv(Task_TaskMan, &msgbuf, byteof(msgbuf[0]));
	return msgbuf[0];
}


DEFSYSC sysc_TEST(stduint t, stduint e, stduint s) {
	ThreadBlock* caller_th = Taskman::current_thread[Taskman::getID()];
	ProcessBlock* pb = caller_th->parent_process;
	if (t == 'T' && e == 'E' && s == 'S') {
		rostr test_msg = "Syscalls Test OK!";
		Console.OutFormat("[Mecocoa] PID");
		Console.OutInteger(pb->pid, +10);
		Console.OutFormat(": %s\n\r", test_msg);
	}
	else {
		rostr test_msg = "[Mecocoa] Syscalls Test FAIL!";
		Console.OutFormat("%s %x %x %x\n\r", test_msg, t, e, s);
	}
	return pb->pid;
}


DEFSYSC sysc_SIGA(stduint sig, stduint act, stduint oact) {
	auto pb = Taskman::current_thread[Taskman::getID()]->parent_process;
	
	stduint phys_act = act ? (stduint)pb->paging[act] : 0;
	stduint phys_oact = oact ? (stduint)pb->paging[oact] : 0;
	
	const struct _POSIX_sigaction* p_act = act ? (const struct _POSIX_sigaction*)mglb(phys_act) : nullptr;
	struct _POSIX_sigaction* p_oact = oact ? (struct _POSIX_sigaction*)mglb(phys_oact) : nullptr;
	
	if (oact && _IMM(phys_oact) == ~_IMM0) {
		plogerro("sysc_SIGA: oact address 0x%[x] not mapped in user page table!", oact);
		return -1;
	}
	
	return (stdsint)sys_sigaction((int)sig, p_act, p_oact);
}

DEFSYSC sysc_KILL(stduint pid, stduint sig, stduint tid) {
	return (stdsint)sys_kill(pid, (int)sig, tid);
}


#if (_MCCA & 0xFF00) == 0x8600 || (_MCCA & 0xFF00) == 0x1000
stduint SYSCALL_TABLE[] = {
	mglb(sysc_OUTC),
	mglb(sysc_INNC),
	mglb(sysc_EXIT),
	mglb(sysc_TIME),
	mglb(sysc_REST),
	mglb(sysc_COMM),
	mglb(sysc_OPEN),
	mglb(sysc_CLOS),
	mglb(sysc_READ),
	mglb(sysc_WRIT),
	mglb(sysc_DELF),
	0,//B
	0,//C
	mglb(sysc_WAIT),
	mglb(sysx_FORK) & 0,// Fork need frame, process it other way
	mglb(sysc_TMSG),
	mglb(sysc_EXEC),
	mglb(sysc_EXET),
	0, //{} mglb(sysc_MALC), // 0x12 unchk
	mglb(sysc_SIGA), // 0x13 unchk
	mglb(sysc_KILL), // 0x14 unchk
	0, // 0x15 unchk (handled manually to pass context frame)
};
#endif

#endif

#if (_MCCA & 0xFF00) == 0x1000

static int sysc_GETCID(stduint ptr_hid)
{
	auto pid = Taskman::current_thread[Taskman::getID()];
	auto pa = (stduint*)pid->parent_process->paging[_IMM(ptr_hid)];
	ploginfo("[%s] arg0 = LA %[x] PA %[x]", __FUNCIDEN__, ptr_hid, pa);
	if (_IMM(pa) == ~_IMM0) return -1;
	if (ptr_hid == NULL) {
		return -1;
	} else {
		*pa = getMHARTID();
		return 0;
	}
}

void syscall_body(NormalTaskContext* cxt)
{
	auto syscall_num = (syscall_t)cxt->a7;

	switch (syscall_num) {
	case syscall_t::OUTC:
		cxt->a0 = sysc_OUTC(cxt->a0, cxt->a1);
		break;
	case syscall_t::INNC:
		cxt->a0 = sysc_INNC(cxt->a0);
		break;
	case syscall_t::EXIT:
		sysc_EXIT(cxt->a0); loop;
		break;
	case syscall_t::REST://{TEMP} (half)
		clint.MSIP(getMHARTID(), MSIP_Type::SofRupt);
		break;
	case syscall_t::COMM:
		cxt->a0 = sysc_COMM(cxt->a0, cxt->a1, cxt->a2);
		break;
	case syscall_t::TMSG:
		cxt->a0 = sysc_TMSG();
		break;
	case syscall_t::GET_CORE_ID:
		cxt->a0 = sysc_GETCID(cxt->a0);
		break;
	case syscall_t::SIGA:
		cxt->a0 = sysc_SIGA(cxt->a0, cxt->a1, cxt->a2);
		break;
	case syscall_t::KILL:
		cxt->a0 = sysc_KILL(cxt->a0, cxt->a1, cxt->a2);
		break;
	case syscall_t::SIGR:
		cxt->a0 = sysc_SIGR(cxt);
		break;
	default:
		plogerro("Unknown syscall no: %d", syscall_num);
		loop HALT();
		cxt->a0 = -1;
	}
	check_and_deliver_signals(cxt);
	return;
}

#endif
#if (_MCCA & 0xFF00) == 0x8600
extern "C" void check_and_deliver_signals_syscall(CallgateFrame* frame);
__attribute__((optimize("O0")))
stduint Handint_SYSCALL(CallgateFrame* frame) {
	#if _MCCA == 0x8632
	auto callid = (syscall_t)frame->ax;
	stduint para[4] = { frame->cx, frame->dx, frame->bx };
	#elif _MCCA == 0x8664
	auto pb = Taskman::current_thread[Taskman::getID()]->parent_process;
	if (&pb->paging != &kernel_paging) frame = (CallgateFrame*)(pb->paging[_IMM(frame)]);
	auto callid = (syscall_t)(frame->ax & 0x7FFFFFFFu);
	stduint para[4] = { frame->di, frame->si, frame->dx };
	#endif

	stduint ret_val = -1;
	if (_IMM(callid) < numsof(SYSCALL_TABLE) && SYSCALL_TABLE[_IMM(callid)]) {
		ret_val = (reinterpret_cast<stdsint(*)(stduint, stduint, stduint)>mglb(SYSCALL_TABLE[_IMM(callid)]))(para[0], para[1], para[2]);
	}
	else switch (callid) {
	case syscall_t::FORK: ret_val = sysx_FORK(frame); break;
	case syscall_t::SIGR: ret_val = sysc_SIGR(frame); break;
	case syscall_t::TEST: ret_val = sysc_TEST(para[0], para[1], para[2]); break;
	
	default:
		printlog(_LOG_ERROR, "Unimplemented syscall: 0x%[32H]", _IMM(callid));
		break;
	}

	frame->ax = ret_val;
	check_and_deliver_signals_syscall(frame);
	return frame->ax;
}
#endif

