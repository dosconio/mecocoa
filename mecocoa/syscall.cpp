// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Syscalls
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <cpp/Device/UART>
#include <c/driver/timer.h>
#include <c/driver/keyboard.h>

#define DEFSYSC extern "C" stdsint
extern stduint SYSCALL_TABLE[18];

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
	pb->paging_redirect = old_dir;
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
		if (auto con = pid->focus_tty ? (Console_t*)cast<Dnode*>(pid->focus_tty)->offs : 0)
		{
			if (!len) con->OutChar(ch);
			else while (len) {
				stduint crt_len = 0x1000 - (ch & 0xFFF);
				if (len < crt_len) crt_len = len;
				auto pa = pid->paging[ch];
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
	else if (ppb->focus_tty && -1 != (ch = VTTY_INNQ(ppb->focus_tty)->inn())) {
		ret = ch;
	}
	return ret;
}

DEFSYSC sysc_EXIT(stduint code) {
	// IC.enAble(false);
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
	return 0;
}

DEFSYSC sysc_COMM(stduint op, stduint to, stduint msg) {
	auto th = Taskman::current_thread[Taskman::getID()];
	usize ret;
	// ploginfo("sysc_COMM:[th%u] op = %x, to = %x, msg = %x", th->getID(), op, to, msg);
	if (op & 0b01) { // SEND
		// ploginfo("%d --> %d: 0x%[x]", pb->getID(), to, msg);
		if (ret = msg_send(th, to, (CommMsg*)msg)) return ret;
	}
	if (op & 0b10) { // RECV
		// if (to && to != ~_IMM0) ploginfo("%d <-- %d", pb->getID(), to);
		ret = msg_recv(th, to, (CommMsg*)msg);
	}
	if (op & 0b11); else {
		plogerro("Invalid op %x", op);
		return 0xF0F0;
	}
	if (th->state == ThreadBlock::State::Pended) {
		Taskman::Schedule(true);
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
			sysrecv(Task_FileSys, open_buf, byteof(open_buf[0]));

			stduint bytes_read = open_buf[0];
			if (bytes_read == 0) {
				syssend(Task_Console, 0, 0, _IMM(ConsoleMsg::INNC));
				stduint ret;
				sysrecv(Task_Console, &ret, byteof(ret));
				if (ret == ~_IMM0) break;

				// Identify the specific vtty device for echo
				stduint tty_idx = (stduint)pb->pfiles[fd]->vfile->f_inode->internal_handler;
				Console_t* con = (tty_idx < vttys.Count()) ? (Console_t*)vttys[tty_idx]->offs : nullptr;

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
				MemCopyP((void*)(addr + total_read), pb->paging_redirect ? *pb->paging_redirect : pb->paging, &ch, kernel_paging, 1);
				total_read++;
				if (con) con->OutChar(ch);
				
				if (ch == '\n') break;
				continue;
			}

			total_read += bytes_read;

			if (total_read > 0) {
				byte last_char;
				MemCopyP(&last_char, kernel_paging, (void*)(addr + total_read - 1), pb->paging_redirect ? *pb->paging_redirect : pb->paging, 1);
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

DEFSYSC sysc_WAIT(stduint usr_status) {
	// ploginfo("syscall wait");
	auto tb = Taskman::current_thread[Taskman::getID()];
	stduint open_buf[2]{ tb->parent_process->getID(),usr_status };
	syssend(Task_TaskMan, sliceof(open_buf), _IMM(TaskmanMsg::WAIT));
	sysrecv(Task_TaskMan, sliceof(open_buf));// (pid, state)
	stdsint ret = open_buf[0];// pid
	if (ret && usr_status) {
		auto pb = tb->parent_process;
		MemCopyP((void*)usr_status, (pb->paging_redirect ? *pb->paging_redirect : pb->paging), &open_buf[1], kernel_paging, byteof(stduint));
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

DEFSYSC sysc_EXEC(stduint path, stduint args, stduint len) {
	stduint msgbuf[4] = { Taskman::current_thread[Taskman::getID()]->parent_process->getID(), path, args, len };
	syssend(Task_TaskMan, sliceof(msgbuf), _IMM(TaskmanMsg::EXEC));
	sysrecv(Task_TaskMan, &msgbuf, byteof(msgbuf[0]));
	return msgbuf[0];
}
DEFSYSC sysc_EXET(stduint path, stduint args, stduint len) {
	stduint msgbuf[4] = { Taskman::current_thread[Taskman::getID()]->parent_process->getID(), path, args, len };
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
};
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
	default:
		plogerro("Unknown syscall no: %d", syscall_num);
		loop HALT();
		cxt->a0 = -1;
	}

	return;
}

#endif
#if (_MCCA & 0xFF00) == 0x8600
__attribute__((optimize("O0")))
stduint Handint_SYSCALL(CallgateFrame* frame) {
	#if _MCCA == 0x8632
	auto callid = (syscall_t)frame->ax;
	stduint para[4] = { frame->cx, frame->dx, frame->bx };
	#elif _MCCA == 0x8664
	auto pb = Taskman::current_thread[Taskman::getID()]->parent_process;
	if (&pb->paging != &kernel_paging) frame = (CallgateFrame*)pb->paging[_IMM(frame)];
	auto callid = (syscall_t)(frame->ax & 0x7FFFFFFFu);
	stduint para[4] = { frame->di, frame->si, frame->dx };
	#endif

	if (_IMM(callid) < numsof(SYSCALL_TABLE) && SYSCALL_TABLE[_IMM(callid)]) {
		return (reinterpret_cast<stdsint(*)(stduint, stduint, stduint)>mglb(SYSCALL_TABLE[_IMM(callid)]))(para[0], para[1], para[2]);
	}
	else switch (callid) {
	case syscall_t::FORK: return sysx_FORK(frame);
	case syscall_t::TEST: return sysc_TEST(para[0], para[1], para[2]);
	
	default:
		printlog(_LOG_ERROR, "Unimplemented syscall: 0x%[32H]", _IMM(callid));
		break;
	}
	return -1;
}
#endif

