// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Syscalls
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <cpp/Device/UART>
#include <c/driver/keyboard.h>

#define DEFSYSC static stdsint

#ifdef _ARC_x86 // x86:

static const byte syscall_delayflgs[0x100 / _BYTE_BITS_] = {
	0b00110001, 0b00000000, // 0x0X
};

static stduint syscall_06_open(stduint* paras, stduint pid)
{
	// plogtrac("%s", __FUNCIDEN__);
	stduint open_buf[1];
	ProcessBlock* pb = TaskGet(pid);
	struct {
		stduint flag;
		stduint pid;
		char filename[sizeof(stduint) * (8 - 2)];
	} open_msg;
	open_msg.flag = paras[1];
	open_msg.pid = pid;
	auto len = StrCopyP(open_msg.filename, kernel_paging, (rostr)paras[0], pb->paging, byteof(open_msg.filename) - 1);
	// ploginfo("fopen %s %u chars", open_msg.filename, len);
	syssend(Task_FileSys, &open_msg, sizeof(open_msg), _IMM(FilemanMsg::OPEN));
	sysrecv(Task_FileSys, open_buf, byteof(open_buf));
	if (cast<stdsint>(open_buf[0]) < 0) {
		open_buf[0] = ~_IMM0;// no descriptor
	}
	// ploginfo("open file: 0x%[32H]", open_buf[0]);
	return open_buf[0];
}

static stduint syscall_07_close(stduint* paras, stduint pid) {
	stduint open_buf[1];
	struct {
		stduint fd;
		stduint pid;
	} open_msg;
	open_msg.fd = paras[0];
	open_msg.pid = pid;
	syssend(Task_FileSys, &open_msg, sizeof(open_msg), 0x03);
	sysrecv(Task_FileSys, open_buf, byteof(open_buf));
	return open_buf[0];// 0 for success
}


DEFSYSC sysc_OUTC(stduint ch, stduint len);
extern bool fileman_hd_ready;

__attribute__((optimize("O0")))
stduint Handint_SYSCALL(CallgateFrame* frame) {
	auto task_switch_enable_old = task_switch_enable;//{TODO} {spinlk for multi-Proc}
	task_switch_enable = false;
	bool ch_tse = task_switch_enable_old == true;
	auto callid = (syscall_t)frame->ax;
	stduint para[3] = { frame->cx, frame->dx, frame->bx };
	stduint msgbuf[4] = {};
	stduint ret = 0;
	stduint caller_pid = Taskman::CurrentPID();
	byte flg = syscall_delayflgs[_IMM(callid) / _BYTE_BITS_];

	char ch;
	if (!(flg & _IMM1S(_IMM(callid) & 0b111)) && ch_tse) {
		task_switch_enable = task_switch_enable_old;
	}
	switch (callid) {
	case syscall_t::OUTC: {
		sysc_OUTC(para[0], para[1]);
		if (ch_tse) task_switch_enable = task_switch_enable_old;
		break;
	}
	case syscall_t::INNC:// Read from its TTY, return -1 if the TTY is occupied
	{
		//{} Create buffer for each vtty; focused_tty; tty[0] is desk-back
		auto ppb = Taskman::Locate(caller_pid);
		if (para[0]) {
			msgbuf[3] = caller_pid;// pid
			syssend(Task_Console, sliceof(msgbuf), _IMM(ConsoleMsg::INNC));
			sysrecv(Task_Console, &ret, byteof(ret));
		}
		else if (ppb->focus_tty && -1 != (ch = VTTY_INNQ(ppb->focus_tty)->inn())) {
			return ch;
		}
		else ret = ~_IMM0;
		break;
	}
	case syscall_t::EXIT:
	{
		// __asm("mov %0, %%eax" : : "m"(para[0])); TaskReturn();
		para[1] = para[0];
		para[0] = caller_pid;
		syssend(Task_TaskMan, para, byteof(para), _IMM(TaskmanMsg::EXIT));
		sysrecv(Task_TaskMan, &ret, byteof(ret));
		// unreachable
		break;
	}
	case syscall_t::TIME:
		ret = mecocoa_global->system_time.sec;
		break;
	case syscall_t::REST:
	// case_syscall_t_REST:
		Taskman::Schedule(true);
		if (ch_tse) task_switch_enable = task_switch_enable_old;
		break;
	case syscall_t::COMM:// (mode, obj, vaddr msg)
	{
		// assert(!src && src < numsof(Tasks));
		ProcessBlock* pb = TaskGet(caller_pid);
		if (para[0] & 0b01) { // SEND
			// ploginfo("%d --> %d", pb->getID(), para[1]);
			ret = msg_send(pb, (para[1]), (CommMsg*)para[2]);
		}
		if (para[0] & 0b10) { // RECV
			// if (para[1] && para[1] != ~_IMM0) ploginfo("%d <-- %d", pb->getID(), para[1]);
			ret = msg_recv(pb, (para[1]), (CommMsg*)para[2]);
		}
		
		if (pb->state == ProcessBlock::State::Pended) {
			Taskman::Schedule(true);
		}
		if (ch_tse) task_switch_enable = task_switch_enable_old;
		break;
	}
	case syscall_t::OPEN:// (str, uint)->(uint)
		while (!fileman_hd_ready);// wait hd registered (in real, HD can not be used before it inited then registered)
		// while (1);
		ret = syscall_06_open(para, caller_pid);
		break;
	case syscall_t::CLOS:
		while (!fileman_hd_ready);// wait hd registered
		ret = syscall_07_close(para, caller_pid);
		break;
	case syscall_t::READ:
	case syscall_t::WRIT:
	{
		while (!fileman_hd_ready);// wait hd registered
		stduint open_buf[4];
		open_buf[0] = para[0];// fid
		open_buf[1] = para[1];// addr
		open_buf[2] = para[2];// len
		open_buf[3] = caller_pid;
		syssend(Task_FileSys, open_buf, byteof(open_buf), _IMM(callid) - _IMM(syscall_t::READ) + _IMM(FilemanMsg::READ));
		sysrecv(Task_FileSys, open_buf, byteof(open_buf[0]));
		ret = open_buf[0];// 0 for success
		break;
	}
	case syscall_t::DELF:// (usr filename) --> (pid, filename[7]...) --> (!success)
	{
		stduint open_buf[(8)];
		ProcessBlock* pb = TaskGet(caller_pid);
		open_buf[0] = caller_pid;
		StrCopyP((char*)&open_buf[1], kernel_paging, (rostr)para[0], pb->paging, byteof(stduint) * 7 - 1);
		syssend(Task_FileSys, open_buf, byteof(open_buf), _IMM(FilemanMsg::REMOVE));
		sysrecv(Task_FileSys, open_buf, byteof(open_buf[0]));
		ret = open_buf[0];// 0 for success
		break;
	}

	case syscall_t::WAIT:
	{
		// ploginfo("syscall wait");
		stduint tmp[2];
		para[1] = para[0];
		para[0] = caller_pid;
		syssend(Task_TaskMan, para, byteof(para), _IMM(TaskmanMsg::WAIT));
		sysrecv(Task_TaskMan, tmp, byteof(tmp));// (pid, state)
		ret = tmp[0];// pid
		if (ret && para[1]) {
			MemCopyP((void*)para[1], TaskGet(caller_pid)->paging, &tmp[1], kernel_paging, byteof(stduint));
		}
		break;
	}
	case syscall_t::FORK:
	{
		// ploginfo("syscall fork");
		msgbuf[0] = caller_pid;
		msgbuf[1] = _IMM(frame);
		syssend(Task_TaskMan, sliceof(msgbuf), _IMM(TaskmanMsg::FORK));
		sysrecv(Task_TaskMan, &ret, byteof(ret));
		break;
	}
	case syscall_t::TMSG:
	{
		ProcessBlock* pb = TaskGet(caller_pid);
		ret = pb->queue_send_queuehead ? _TEMP 1 : 0;
		break;
	}
	case syscall_t::EXEC:
	{
		plogerro("Please send to Task_TaskMan");
		// ret = execv((const char*)para[0], (char**)para[1]);
		break;
	}



	case syscall_t::TEST:
		//{TODO} ISSUE 20250706 Each time subapp (Ring3) print %d or other integer by outsfmt() will panic, but OutInteger() or Kernel Ring0 is OK.
		if (para[0] == 'T' && para[1] == 'E' && para[2] == 'S') {
			rostr test_msg = "Syscalls Test OK!";
			Console.OutFormat("\xFF\x70[Mecocoa]\xFF\x27 PID");
			Console.OutInteger(caller_pid, +10);
			Console.OutFormat(": %s\xFF\x07\n\r", test_msg + 0x80000000);
		}
		else {
			rostr test_msg = "\xFF\x70[Mecocoa]\xFF\x47 Syscalls Test FAIL!\xFF\x07";
			Console.OutFormat("%s %x %x %x\n\r", test_msg + 0x80000000,
				para[0], para[1], para[2]);
		}
		ret = caller_pid;
		// task_switch_enable = task_switch_enable_old;
		break;
	default:
		printlog(_LOG_ERROR, "Unimplemented syscall: 0x%[32H]", _IMM(callid));
		break;
	}
	// task_switch_enable = task_switch_enable_old;//{MUTEX for multi-Proc}
	return ret;
}


#endif




#if (_MCCA & 0xFF00) == 0x8600 || (_MCCA & 0xFF00) == 0x1000
_ESYM_C stduint SYSCALL_TABLE[];
DEFSYSC sysc_OUTC(stduint ch, stduint len);
DEFSYSC sysc_EXIT(stduint code);
stduint SYSCALL_TABLE[] = {
	_IMM(sysc_OUTC),// OUTC(ch/str, strlen) | strlen: 0 for single char
	0,// INNC(else_blocked)
	_IMM(sysc_EXIT),// EXIT(code)
};

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
	if (_IMM(callid) >= numsof(SYSCALL_TABLE)) {
		plogerro("syscall: callid out of range");
		loop HALT();
	}
	return reinterpret_cast<stdsint(*)(stduint, stduint, stduint)>(SYSCALL_TABLE[_IMM(callid)])(para1, para2, para3);
	#endif
	return ret;
}


DEFSYSC sysc_OUTC(stduint ch, stduint len) {// OUTC
	#if 0
	if (len) {
		UART0.out((rostr)ch, len);
	}
	else {
		UART0.OutChar(ch);
	}
	#else
	if (auto pid = Taskman::Locate(Taskman::CurrentPID())) {
		auto con = pid->focus_tty ? (Console_t*)cast<Dnode*>(pid->focus_tty)->offs : 0;
		if (con) {
			if (len) {
				while (len) {
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
			}
			else {
				con->OutChar(ch);
			}
			con->OutChar(nil);
			return 0;
		}
		else plogerro("sysc_try: null console, id = %u", pid->focus_tty);
	}
	else plogerro("sysc_try: null task");
	#endif
	return -1;
}

// sysc_INNC

DEFSYSC sysc_EXIT(stduint code) {
	// IC.enAble(false);
	Taskman::ExitCurrent(code);
	printlog(_LOG_FATAL, "sysc_EXIT");// unreachable
	return -1;
}

#endif

#if (_MCCA & 0xFF00) == 0x1000

static int sys_gethid(unsigned int *ptr_hid)
{
	auto pid = Taskman::Locate(Taskman::CurrentPID());
	auto pa = (stduint*)pid->paging[_IMM(ptr_hid)];
	ploginfo("--> sys_gethid, arg0 = LA %[x] PA %[x]", ptr_hid, pa);
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
		sysc_OUTC(cxt->a0, cxt->a1);
		break;
	case syscall_t::GET_CORE_ID:
		cxt->a0 = sys_gethid((unsigned int *)(cxt->a0));
		break;
	default:
		UART0.OutFormat("Unknown syscall no: %d\n", syscall_num);
		cxt->a0 = -1;
	}

	return;
}


#endif
