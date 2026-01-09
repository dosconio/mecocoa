// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST

#include <c/consio.h>
#include <c/driver/keyboard.h>

use crate uni;
#ifdef _ARC_x86 // x86:
#include "../include/atx-x86-flap32.hpp"

Handler_t syscalls[_TEMP 1];

//{TODO} Syscall class
//{TODO} Use callgate-para (but register nor kernel-area) to pass parameters
static const byte syscall_paracnts[0x100] = {
	// ---- 0x0X ----
	1, //0x00 OUTC
	0, //0x01 INNC ()->[blocked]ASCII
	1, //0x02 EXIT (code)
	0, //0x03 TIME ()->(second)
	0, //0x04 REST
	3, //0x05 COMM
	2, //0x06 OPEN (pathname,flags)->(fd: < 0 if not success)
	1, //0x07 CLOS (fd)
	4, //0x08 READ (fd, slice.addr, slice.len) ->(bytes_done)
	4, //0x09 WRIT (fd, slice.addr, slice.len) ->(bytes_done)
	1, //0x0A DELF (pathname)
	0, //KEPT for advanced file op.
	0,
	1, //0x0D WAIT (&state)->(pid)
	0, //0x0E FORK ()->(child_pid for parent and nil for child)
	0, //0x0F TMSG ()->(msg_unsovled)
	// ---- 0x1X ----
	2, //0x10 EXEC (pathname, argv)->(0 for success)
	0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// 1XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// 2XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// 3XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// 4XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// 5XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// 6XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// 7XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,//	8XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// 9XH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// AXH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// BXH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// CXH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// DXH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,// EXH
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,
	3, //TEST // FXH
};
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



extern bool fileman_hd_ready;
static stduint call_body(const syscall_t callid, ...) {
	auto task_switch_enable_old = task_switch_enable;//{TODO} {spinlk for multi-Proc}
	task_switch_enable = false;
	bool ch_tse = task_switch_enable_old == true;
	// for
	// - ProcessBlock::cpu0_task
	//
	Letpara(paras, callid);
	stduint para[3] = {0xF1, 0xF8, 0xFA};// magic for debug
	stduint ret = 0;
	if (_IMM(callid) >= numsof(syscall_paracnts)) return ~_IMM0;
	byte pcnt = syscall_paracnts[_IMM(callid)];
	for0(i, pcnt) {
		para[i] = para_next(paras, stduint);
	}
	stduint caller_pid = ProcessBlock::cpu0_task;
	byte flg = syscall_delayflgs[_IMM(callid) / _BYTE_BITS_];
	if (!(flg & _IMM1S(_IMM(callid) & 0b111)) && ch_tse) {
		task_switch_enable = task_switch_enable_old;
	}
	switch (callid) {
	case syscall_t::OUTC: {
		ProcessBlock* pb = TaskGet(caller_pid);
		// bcons[pb->focus_tty_id]->OutChar(para[0]);
		_TEMP if (!pb->focus_tty_id) outc(para[0]);
		if (ch_tse) task_switch_enable = task_switch_enable_old;
		break;
	}
	case syscall_t::INNC:// ()->ASCII
	{
		// Read from its TTY, return -1 if the TTY is occupied.
		stduint p[4];
		p[0] = _TODO _TEMP 0;// dev
		p[3] = caller_pid;// pid
		syssend(Task_Con_Serv, p, byteof(p), 3);
		sysrecv(Task_Con_Serv, &ret, byteof(ret));
		// ploginfo("INNC: %d", ret);
		break;
	}
	case syscall_t::EXIT:
	{
		// __asm("mov %0, %%eax" : : "m"(para[0])); TaskReturn();
		para[1] = para[0];
		para[0] = caller_pid;
		syssend(Task_TaskMan, para, byteof(para), _IMM(TaskmanMsg::EXIT));
		// unreachable
		break;
	}
	case syscall_t::TIME:
		ret = mecocoa_global->system_time.sec;
		break;
	case syscall_t::REST:
	// case_syscall_t_REST:
		switch_halt();
		if (ch_tse) task_switch_enable = task_switch_enable_old;
		break;
	case syscall_t::COMM:// (mode, obj, vaddr msg)
	{

		// assert(!src && src < numsof(Tasks));
		//{}INTERRUPT src == ~1
		ProcessBlock* pb = TaskGet(caller_pid);
		if (para[0] == 0b01) { // SEND
			// ploginfo("%d --> %d", pb->getID(), para[1]);
			ret = msg_send(pb, (para[1]), (CommMsg*)para[2]);
		}
		else if (para[0] == 0b10) { // RECV
			// if (para[1] && para[1] != ~_IMM0) ploginfo("%d <-- %d", pb->getID(), para[1]);
			ret = msg_recv(pb, (para[1]), (CommMsg*)para[2]);
		}
		else {
			printlog(_LOG_ERROR, "Bad `mode` of syscall 0x%[32H]", _IMM(callid));
		}
		
		if (pb->state == ProcessBlock::State::Pended) {
			// goto case_syscall_t_REST;
			switch_halt();
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
		ret = caller_pid;
		syssend(Task_TaskMan, &ret, byteof(ret), _IMM(TaskmanMsg::FORK));
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

static int hh;
void call_gate() { // noreturn
	// here stack top: TOP >>> 0x0804923d (CLogaddr: +0x4(%ebp))  0x0000000f (CSeg)  0x00005f44 (DLogaddr)  0x0000003f (DSeg)
	stduint para[4];// a c d b
	stduint bp, si, di;
	stduint clogadd, dlogadd;// ip and sp
	//  612:	55                   	push   %ebp <- 0(ebp)
	//  613:	89 e5                	mov    %esp,%ebp
	//  615:	53                   	push   %ebx
	//  616:	83 ec 24             	sub    ...,%esp
	__asm("cli");
	__asm("mov  %%eax, %0" : "=m"(para[0]));
	__asm("mov  %%ecx, %0" : "=m"(para[1]));
	__asm("mov  %%edx, %0" : "=m"(para[2]));
	__asm("mov  %%ebx, %0" : "=m"(para[3]));
	__asm("mov  %%esi, %0" : "=m"(si));
	__asm("mov  %%edi, %0" : "=m"(di));
	__asm("mov  +0x0(%ebp), %eax");
	__asm("mov  %%eax, %0" : "=m"(bp));
	//{} only when ring-switch that exists. Omit when ring0 call.
	__asm("mov  +0x4(%ebp), %eax");
	__asm("mov  %%eax, %0" : "=m"(clogadd));
	__asm("mov  +0xC(%ebp), %eax");
	__asm("mov  %%eax, %0" : "=m"(dlogadd));
	__asm("push %ebx;push %ecx;push %edx;push %ebp;push %esi;push %edi;");
	__asm("call PG_PUSH");
	auto pb = TaskGet(ProcessBlock::cpu0_task);
	if (ProcessBlock::cpu0_task && pb) {
		if (!pb->before_syscall_data_pointer) {
			pb->before_syscall_data_pointer = dlogadd;// sp
			dlogadd = nil;
		}//{} only when ring-switch that exists. Omit when ring0 call.
		if (!pb->before_syscall_code_pointer) {
			pb->before_syscall_eax = para[0];
			pb->before_syscall_ecx = para[1];
			pb->before_syscall_edx = para[2];
			pb->before_syscall_ebx = para[3];
			pb->before_syscall_esi = si;
			pb->before_syscall_edi = di;
			pb->before_syscall_ebp = bp;
			pb->before_syscall_code_pointer = clogadd;
			clogadd = nil;
		}
	}
	__asm("sti");
	// if (ProcessBlock::cpu0_task == Task_Init) {
	// 	plogwarn("d=%[32H] c=%[32H]", pb->before_syscall_data_pointer, pb->before_syscall_code_pointer);
	// }
	stduint ret = call_body((syscall_t)para[0], para[1], para[2], para[3]);
	__asm("cli");
	pb = TaskGet(ProcessBlock::cpu0_task);
	if (!dlogadd) pb->before_syscall_data_pointer = nil;
	if (!clogadd) pb->before_syscall_code_pointer = nil;
	__asm("call PG_POP");
	// pb_src->TSS.PDBR = getCR3();
	__asm("mov  %0, %%eax" : : "m"(ret));
	__asm("pop %edi");// popad
	__asm("pop %esi");
	__asm("pop %ebp");
	__asm("pop %edx");
	__asm("pop %ecx");
	__asm("pop %ebx");
	__asm("mov  %ebp, %esp");
	__asm("pop  %ebp      ");
	__asm("sti");
	__asm("jmp returnfar");
	__asm("callgate_endo:");
	loop;
}

void call_intr() {
	stduint para[4];// a c d b
	__asm("mov  %%eax, %0" : "=m"(para[0]));
	__asm("mov  %%ecx, %0" : "=m"(para[1]));
	__asm("mov  %%edx, %0" : "=m"(para[2]));
	__asm("mov  %%ebx, %0" : "=m"(para[3]));
	__asm("push %ebx;push %ecx;push %edx;push %ebp;push %esi;push %edi;");
	__asm("call PG_PUSH");
	stduint ret = call_body((syscall_t)para[0], para[1], para[2], para[3]);
	__asm("call PG_POP");
	__asm("mov  %0, %%eax" : : "m"(ret));
	__asm("pop %edi");// popad
	__asm("pop %esi");
	__asm("pop %ebp");
	__asm("pop %edx");
	__asm("pop %ecx");
	__asm("pop %ebx");
	__asm("mov  %ebp, %esp");
	__asm("pop  %ebp      ");
	__asm("iretl");// iretd
}

void* call_gate_entry() {
	return (void*)call_gate;
}

static stduint syscall0(syscall_t callid) {
	stduint ret;
	__asm("mov  %0, %%eax" : : "m"(callid));
	CallFar(0, SegCall);
	__asm("mov  %%eax, %0" : "=m"(ret));
	return ret;
}
static stduint syscall1(syscall_t callid, stduint para1) {
	stduint ret;
	__asm("mov  %0, %%eax" : : "m"(callid));
	__asm("mov  %0, %%ecx" : : "m"(para1));
	CallFar(0, SegCall);
	__asm("mov  %%eax, %0" : "=m"(ret));
	return ret;
}
static stduint syscall2(syscall_t callid, stduint para1, stduint para2) {
	stduint ret;
	__asm("mov  %0, %%eax" : : "m"(callid));
	__asm("mov  %0, %%ecx" : : "m"(para1));
	__asm("mov  %0, %%edx" : : "m"(para2));
	CallFar(0, SegCall);
	__asm("mov  %%eax, %0" : "=m"(ret));
	return ret;
}
static stduint syscall3(syscall_t callid, stduint para1, stduint para2, stduint para3) {
	stduint ret;
	__asm("mov  %0, %%eax" : : "m"(callid));
	__asm("mov  %0, %%ecx" : : "m"(para1));
	__asm("mov  %0, %%edx" : : "m"(para2));
	__asm("mov  %0, %%ebx" : : "m"(para3));
	CallFar(0, SegCall);
	__asm("mov  %%eax, %0" : "=m"(ret));
	return ret;
}

stduint syscall(syscall_t callid, ...) {
	// GCC style
	Letpara(paras, callid);
	stduint p1, p2, p3;
	switch (syscall_paracnts[_IMM(callid)])
	{
	case 0:
		return syscall0(callid);
		break;
	case 1:
		p1 = para_next(paras, stduint);
		return syscall1(callid, p1);
		break;
	case 2:
		p1 = para_next(paras, stduint);
		p2 = para_next(paras, stduint);
		return syscall2(callid, p1, p2);
		break;
	case 3:
		p1 = para_next(paras, stduint);
		p2 = para_next(paras, stduint);
		p3 = para_next(paras, stduint);
		return syscall3(callid, p1, p2, p3);
		break;
	default:
		break;
	}
	return ~_IMM0;
}

// No dynamic core

#endif
