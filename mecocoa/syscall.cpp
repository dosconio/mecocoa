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
extern "C" stdsint sysc_TNEW(stduint, stduint, stduint);
extern "C" stdsint sysc_TEXI(stduint, stduint, stduint);
extern "C" stdsint sysc_TJOI(stduint, stduint, stduint);
extern "C" stdsint sysc_TGET(stduint, stduint, stduint);
extern "C" stdsint sysc_TDET(stduint, stduint, stduint);
extern "C" stdsint sysc_TYLD(stduint, stduint, stduint);
extern "C" stdsint sysc_FUTX(stduint, stduint, stduint);
extern "C" stdsint sysc_SOCK(stduint, stduint, stduint);
extern "C" stdsint sysc_BIND(stduint, stduint, stduint);
extern "C" stdsint sysc_CONN(stduint, stduint, stduint);
extern "C" stdsint sysc_SEND(stduint, stduint, stduint);
extern "C" stdsint sysc_RECV(stduint, stduint, stduint);
extern "C" stdsint sysc_ROUT(stduint, stduint, stduint);
extern "C" stdsint sysc_FCTL(stduint, stduint, stduint);
extern "C" stdsint sysc_SADR(stduint, stduint, stduint);
extern "C" stdsint sysc_SOPT(stduint, stduint, stduint);
extern "C" stdsint sysc_POLL(stduint, stduint, stduint);
extern "C" stdsint sysc_LIST(stduint, stduint, stduint);
extern "C" stdsint sysc_ACPT(stduint, stduint, stduint);
extern "C" void check_and_deliver_signals(void* context);

// Syscall Wrappers
extern stduint SYSCALL_TABLE[50];

void Syscall::Initialize() {
	#if _MCCA == 0x8632
	IC[IRQ_SYSCALL].setModeRupt(mglb(Handint_INTCALL_Entry), SegCo32)->DPL = 3;

	#elif _MCCA == 0x8664
	setMSR(x86MSR::EFER, 0x0501);
	setMSR(x86MSR::LSTAR, mglb(Handint_SYSCALL_Entry));
	setMSR(x86MSR::STAR, (_IMM(SegCo64) << 32) | (_IMM(SegCo32 | _IMM(RING_U)) << 48));
	setMSR(x86MSR::FMASK, 0x200);
	stduint percore_addr = PERCORE_VBASE + Taskman::getID() * PERCORE_STRIDE;
	setMSR(x86MSR::GS_BASE, percore_addr);
	setMSR(x86MSR::KERNEL_GS_BASE, 0);

	#endif
}


bool IsPwcall(syscall_t callid);
stdsint HandlePwcall(syscall_t callid, stduint p1, stduint p2, stduint p3);

#if (_MCCA & 0xFF00) == 0x8600 || (_MCCA & 0xFF00) == 0x1000

__attribute__((optimize("O0")))
stduint syscall(syscall_t callid, stduint para1, stduint para2, stduint para3) {
	stduint ret;
	#if   (_MCCA & 0xFF00) == 0x8600
	#if   (_MCCA & 0xFF00) == 0x8600
	if (IsPwcall(callid)) {
		ret = HandlePwcall(callid, para1, para2, para3);
	}
	else if (_IMM(callid) >= numsof(SYSCALL_TABLE)) {
		plogerro("syscall: callid out of range: %u", callid);
		loop HALT();
	}
	else {
		ret = reinterpret_cast<stdsint(*)(stduint, stduint, stduint)>(SYSCALL_TABLE[_IMM(callid)])(para1, para2, para3);
	}
	#endif

	#elif (_MCCA & 0xFF00) == 0x1000
	void syscall_body(NormalTaskContext * cxt);
	auto th = Taskman::CurrentTB();
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
	auto th = Taskman::CurrentTB();
	KASSERT(th != nullptr);
	if (auto pid = th->parent_process) {
		auto focus_tty = pid->focus_tty.Lock();
		if (auto con = *focus_tty ? (Console_t*)(*focus_tty)->offs : 0)
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
	auto th = Taskman::CurrentTB();
	KASSERT(th != nullptr);
	auto ppb = th->parent_process;
	if (blocked) {
		msgbuf[3] = ppb->getID();
		syssend(Task_Console, sliceof(msgbuf), _IMM(ConsoleMsg::INNC));//
		sysrecv(Task_Console, &ret, byteof(ret));// need Context
	}
	else {
		auto focus_tty = ppb->focus_tty.Lock();
		QueueLimited* q = *focus_tty ? VTTY_INNQ(*focus_tty) : nullptr;
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
		return tick / CONFIG_SysTickFreq;// mecocoa_global->system_time.sec;
	case 1:// ms
		return tick * 1000 / CONFIG_SysTickFreq;
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
		timeout = time * CONFIG_SysTickFreq;
		break;
	case 1: // ms
		timeout = time * CONFIG_SysTickFreq / 1000;
		if (!timeout) timeout = 1; // At least 1 tick
		break;
	default:
		plogwarn("Invalid time unit: %u", unit);
		return -1;
	}

	auto th = Taskman::CurrentTB();
	bool state_rupt = IC.TryMaskInterrupt();

	th->Block(ThreadBlock::BlockReason::BR_Resting); // Block the thread to wait for timer
	SysTimer::Append(timeout, (stduint)th, (_tocall_ft)_TimerWakeUp);
	if (state_rupt) IC.enInterrupt(true);
	
	Taskman::Schedule(true);
	if (_sigset_raw(&th->pending_signals) & ~_sigset_raw(&th->blocked_signals)) {
		return -4; // -EINTR
	}
	return 0;
}

DEFSYSC sysc_COMM(stduint op, stduint to, stduint msg) {
	auto th = Taskman::CurrentTB();
	bool msg_in_kernel = false;
	usize ret = 0;
	// ploginfo("sysc_COMM:[th%u] op = %x, to = %x, msg = %x", th->getID(), op, to, msg);
	if (op & (COMM_SEND | COMM_SEND_ASYNC)) { // SEND or ASYNC_SEND
		// ploginfo("%d --> %d: 0x%[x]", pb->getID(), to, msg);
		if (ret = msg_send(th, to, (CommMsg*)msg, msg_in_kernel, (op & COMM_SEND_ASYNC) != 0)) return ret;
	}
	if (op & COMM_RECV) { // RECV
		// if (to && to != ~_IMM0) ploginfo("%d <-- %d", pb->getID(), to);
		ret = msg_recv(th, to, (CommMsg*)msg, msg_in_kernel);
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
	ThreadBlock* th = Taskman::CurrentTB();
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
	ThreadBlock* th = Taskman::CurrentTB();
	stdsint open_buf[1];
	struct { stduint fd;stduint pid; } open_msg = { fd , th->parent_process->pid };
	syssend(Task_FileSys, &open_msg, sizeof(open_msg), 0x03);
	sysrecv(Task_FileSys, open_buf, byteof(open_buf));
	return open_buf[0];// 0 for success
}

DEFSYSC sysc_MANA(stduint func, stduint arg1, stduint arg2) {
	switch (func)
	{
	case 0:// shutdown
		Coreman::Shutdown();
		break;
	case 1:// reboot
		Coreman::Reboot();
		break;

	default:
		break;
	}
	return 0;
}

DEFSYSC sysc_DUP2(stduint oldfd, stduint newfd) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th->parent_process;
	return pb->Dup2((int)oldfd, (int)newfd);
}

DEFSYSC sysc_PIPE(stduint usr_pipefd) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th->parent_process;
	return pb->Pipe((int*)usr_pipefd);
}

static stdsint CopySocketAddressFromUser(byte* buffer, stduint capacity, stduint usr_addr, stduint addr_length, ProcessBlock* pb) {
	if (!buffer || !pb || !usr_addr || addr_length < sizeof(uni::Network::SocketAddress)) return -1;
	uni::Network::SocketAddress head{};
	MccaMemCopyP(&head, nullptr, true, (const void*)usr_addr, pb, false, sizeof(head));
	if (head.length < sizeof(head) || addr_length < head.length || head.length > capacity) return -1;
	MccaMemCopyP(buffer, nullptr, true, (const void*)usr_addr, pb, false, head.length);
	return head.length;
}

DEFSYSC sysc_SOCK(stduint domain, stduint type, stduint protocol) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th->parent_process;
	return pb ? pb->Socket(domain, type, protocol) : -1;
}

DEFSYSC sysc_BIND(stduint fd, stduint usr_addr, stduint addr_length) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th->parent_process;
	if (!pb) return -1;
	byte address_buffer[sizeof(uni::Network::SocketAddressIPv6)] = {};
	const stdsint copied = CopySocketAddressFromUser(address_buffer, sizeof(address_buffer), usr_addr, addr_length, pb);
	if (copied < 0) return -1;
	return pb->BindSocket((int)fd,
		reinterpret_cast<const uni::Network::SocketAddress*>(address_buffer), stduint(copied));
}

DEFSYSC sysc_CONN(stduint fd, stduint usr_addr, stduint addr_length) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th->parent_process;
	if (!pb) return -1;
	byte address_buffer[sizeof(uni::Network::SocketAddressIPv6)] = {};
	const stdsint copied = CopySocketAddressFromUser(address_buffer, sizeof(address_buffer), usr_addr, addr_length, pb);
	if (copied < 0) return -1;
	return pb->ConnectSocket((int)fd,
		reinterpret_cast<const uni::Network::SocketAddress*>(address_buffer), stduint(copied));
}

DEFSYSC sysc_SEND(stduint fd, stduint usr_req, stduint flags) {
	(void)flags;
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th->parent_process;
	if (!pb || !usr_req) return -1;
	syscall_net_send_t req{};
	MccaMemCopyP(&req, nullptr, true, (const void*)usr_req, pb, false, sizeof(req));
	if (req.length && !req.payload) return -1;
	byte address_buffer[sizeof(uni::Network::SocketAddressIPv6)] = {};
	const uni::Network::SocketAddress* address = nullptr;
	if (req.address || req.address_length) {
		const stdsint copied = CopySocketAddressFromUser(address_buffer, sizeof(address_buffer),
			(stduint)req.address, req.address_length, pb);
		if (copied < 0) return -1;
		address = reinterpret_cast<const uni::Network::SocketAddress*>(address_buffer);
	}
	byte* payload_buffer = nullptr;
	if (req.length) {
		payload_buffer = new byte[req.length];
		if (!payload_buffer) return -1;
		MccaMemCopyP(payload_buffer, nullptr, true, req.payload, pb, false, req.length);
	}
	const stdsint ret = pb->SendSocket((int)fd, payload_buffer, req.length, address);
	delete[] payload_buffer;
	return ret;
}

DEFSYSC sysc_RECV(stduint fd, stduint usr_req, stduint flags) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th->parent_process;
	if (!pb || !usr_req) return -1;
	syscall_net_recv_t req{};
	MccaMemCopyP(&req, nullptr, true, (const void*)usr_req, pb, false, sizeof(req));
	if (req.capacity && !req.payload) return -1;
	byte* payload_buffer = nullptr;
	if (req.capacity) {
		payload_buffer = new byte[req.capacity];
		if (!payload_buffer) return -1;
	}
	uni::Network::SocketAddressIPv6 address_buffer{};
	stduint address_length = req.address && req.address_length ? sizeof(address_buffer) : 0;
	const stduint io_flags = (flags & syscall_net_msg_flag_dontwait) ? 0 : syscall_net_io_flag_wait;
	const stdsint ret = pb->RecvSocket((int)fd, payload_buffer, req.capacity,
		address_length ? reinterpret_cast<uni::Network::SocketAddress*>(&address_buffer) : nullptr,
		address_length ? &address_length : nullptr, io_flags);
	if (ret > 0 && req.payload) {
		MccaMemCopyP(req.payload, pb, false, payload_buffer, nullptr, true, stduint(ret));
	}
	if (ret >= 0 && req.address && req.address_length) {
		stduint user_length = 0;
		MccaMemCopyP(&user_length, nullptr, true, req.address_length, pb, false, sizeof(user_length));
		if (user_length >= address_length && address_length) {
			MccaMemCopyP(req.address, pb, false, &address_buffer, nullptr, true, address_length);
			MccaMemCopyP(req.address_length, pb, false, &address_length, nullptr, true, sizeof(address_length));
		}
	}
	delete[] payload_buffer;
	return ret;
}

DEFSYSC sysc_FCTL(stduint fd, stduint cmd, stduint arg) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th ? th->parent_process : nullptr;
	if (!pb) return -1;
	return pb->Fcntl((int)fd, (int)cmd, arg);
}

DEFSYSC sysc_SADR(stduint fd, stduint usr_req, stduint func) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th ? th->parent_process : nullptr;
	if (!pb || !usr_req) return -1;

	syscall_net_socket_address_t req{};
	MccaMemCopyP(&req, nullptr, true, (const void*)usr_req, pb, false, sizeof(req));
	if (!req.address || !req.address_length) return -1;

	stduint user_length = 0;
	MccaMemCopyP(&user_length, nullptr, true, req.address_length, pb, false, sizeof(user_length));
	if (user_length < sizeof(uni::Network::SocketAddressIPv4)) return -1;

	uni::Network::SocketAddressIPv6 address_buffer{};
	stduint address_length = sizeof(address_buffer);
	bool peer = false;
	switch (syscall_net_socket_address_func_t(func)) {
	case syscall_net_socket_address_func_t::Local:
		break;
	case syscall_net_socket_address_func_t::Peer:
		peer = true;
		break;
	default:
		return -1;
	}
	const stdsint ret = pb->GetSocketAddress((int)fd, peer,
		reinterpret_cast<uni::Network::SocketAddress*>(&address_buffer), &address_length);
	if (ret < 0) return ret;
	if (user_length < address_length) return -1;
	MccaMemCopyP(req.address, pb, false, &address_buffer, nullptr, true, address_length);
	MccaMemCopyP(req.address_length, pb, false, &address_length, nullptr, true, sizeof(address_length));
	return 0;
}

DEFSYSC sysc_SOPT(stduint fd, stduint usr_req, stduint func) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th ? th->parent_process : nullptr;
	if (!pb || !usr_req) return -1;

	syscall_net_socket_option_t req{};
	MccaMemCopyP(&req, nullptr, true, (const void*)usr_req, pb, false, sizeof(req));
	if (!req.option_value) return -1;

	switch (syscall_net_socket_option_func_t(func)) {
	case syscall_net_socket_option_func_t::Set: {
		if (req.option_length < sizeof(int)) return -1;
		int value = 0;
		MccaMemCopyP(&value, nullptr, true, req.option_value, pb, false, sizeof(value));
		return pb->SetSocketOption((int)fd, req.level, req.option_name, value);
	}
	case syscall_net_socket_option_func_t::Get: {
		if (!req.result_length || req.option_length < sizeof(int)) return -1;
		int value = 0;
		const stdsint ret = pb->GetSocketOption((int)fd, req.level, req.option_name, &value);
		if (ret < 0) return ret;
		const stduint length = sizeof(value);
		MccaMemCopyP(req.option_value, pb, false, &value, nullptr, true, length);
		MccaMemCopyP(req.result_length, pb, false, &length, nullptr, true, sizeof(length));
		return 0;
	}
	default:
		return -1;
	}
}

DEFSYSC sysc_POLL(stduint usr_fds, stduint nfds, stduint timeout) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th ? th->parent_process : nullptr;
	if (!pb || nfds > 64) return -1;
	if (!nfds) return pb->Poll(nullptr, 0, (stdsint)timeout);
	if (!usr_fds) return -1;
	syscall_pollfd_t* fds = new syscall_pollfd_t[nfds];
	if (!fds) return -1;
	MccaMemCopyP(fds, nullptr, true, (const void*)usr_fds, pb, false, sizeof(syscall_pollfd_t) * nfds);
	const stdsint ret = pb->Poll(fds, nfds, (stdsint)timeout);
	if (ret >= 0) {
		MccaMemCopyP((void*)usr_fds, pb, false, fds, nullptr, true, sizeof(syscall_pollfd_t) * nfds);
	}
	delete[] fds;
	return ret;
}

DEFSYSC sysc_LIST(stduint fd, stduint backlog, stduint) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th ? th->parent_process : nullptr;
	if (!pb) return -1;
	return pb->ListenSocket((int)fd, backlog);
}

DEFSYSC sysc_ACPT(stduint fd, stduint usr_addr, stduint usr_addr_length) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th ? th->parent_process : nullptr;
	if (!pb) return -1;

	uni::Network::SocketAddressIPv6 address_buffer{};
	stduint address_length = sizeof(address_buffer);
	uni::Network::SocketAddress* address = nullptr;
	stduint* address_length_ptr = nullptr;
	if (usr_addr || usr_addr_length) {
		if (!usr_addr || !usr_addr_length) return -1;
		stduint user_length = 0;
		MccaMemCopyP(&user_length, nullptr, true, (const void*)usr_addr_length, pb, false, sizeof(user_length));
		if (user_length < sizeof(uni::Network::SocketAddressIPv4)) return -1;
		address = reinterpret_cast<uni::Network::SocketAddress*>(&address_buffer);
		address_length_ptr = &address_length;
	}

	const stdsint new_fd = pb->AcceptSocket((int)fd, address, address_length_ptr);
	if (new_fd < 0) return new_fd;
	if (new_fd >= 0 && address && address_length_ptr) {
		MccaMemCopyP((void*)usr_addr, pb, false, address, nullptr, true, *address_length_ptr);
		MccaMemCopyP((void*)usr_addr_length, pb, false, address_length_ptr, nullptr, true, sizeof(*address_length_ptr));
	}
	return new_fd;
}

DEFSYSC sysc_SEEK(stduint fd, stduint off, stduint whence) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th ? th->parent_process : nullptr;
	if (!pb) return -1;
	return pb->Seek((int)fd, (stdsint)off, (int)whence);
}


DEFSYSC sysc_ROUT(stduint func, stduint p1, stduint p2) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th ? th->parent_process : nullptr;
	if (!pb || !p1) return -1;
	switch (syscall_net_route_func_t(func)) {
	case syscall_net_route_func_t::IPv4Default: {
		syscall_net_route_ipv4_t route{};
		if (p2 < sizeof(route)) return -1;
		#if (_MCCA & 0xFF00) == 0x8600
		if (!Devsman::GetDefaultIPv4Route(&route, sizeof(route))) return -1;
		MccaMemCopyP((void*)p1, pb, false, &route, nullptr, true, sizeof(route));
		return 0;
		#else
		return -1;
		#endif
	}
	case syscall_net_route_func_t::IPv4InterfaceCount: {
		if (p2 < sizeof(stduint)) return -1;
		#if (_MCCA & 0xFF00) == 0x8600
		const stduint count = Devsman::LinkDeviceCount();
		MccaMemCopyP((void*)p1, pb, false, &count, nullptr, true, sizeof(count));
		return 0;
		#else
		return -1;
		#endif
	}
	case syscall_net_route_func_t::IPv4Interface: {
		#if (_MCCA & 0xFF00) == 0x8600
		syscall_net_interface_ipv4_t iface{};
		if (p2 < sizeof(iface)) return -1;
		MccaMemCopyP(&iface, nullptr, true, (void*)p1, pb, false, sizeof(iface));
		const stduint index = iface.link_index;
		if (!Devsman::GetIPv4Interface(index, &iface, sizeof(iface))) return -1;
		MccaMemCopyP((void*)p1, pb, false, &iface, nullptr, true, sizeof(iface));
		return 0;
		#else
		return -1;
		#endif
	}
	case syscall_net_route_func_t::IPv4ArpCacheCount: {
		if (p2 < sizeof(stduint)) return -1;
		#if (_MCCA & 0xFF00) == 0x8600
		const stduint count = Devsman::IPv4ArpCacheCount();
		MccaMemCopyP((void*)p1, pb, false, &count, nullptr, true, sizeof(count));
		return 0;
		#else
		return -1;
		#endif
	}
	case syscall_net_route_func_t::IPv4ArpCacheEntry: {
		#if (_MCCA & 0xFF00) == 0x8600
		syscall_net_arp_ipv4_t entry{};
		if (p2 < sizeof(entry)) return -1;
		MccaMemCopyP(&entry, nullptr, true, (void*)p1, pb, false, sizeof(entry));
		const stduint index = entry.entry_index;
		if (!Devsman::GetIPv4ArpCacheEntry(index, &entry, sizeof(entry))) return -1;
		MccaMemCopyP((void*)p1, pb, false, &entry, nullptr, true, sizeof(entry));
		return 0;
		#else
		return -1;
		#endif
	}
	default:
		return -1;
	}
}

DEFSYSC sysc_READ(stduint fd, stduint addr, stduint len) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th->parent_process;
	bool is_named_pipe = false;
	bool is_char_special = false;

	// Intercept named pipes to bypass Task_FileSys server and execute in current thread's context to avoid deadlocks.
	{
		auto files = pb->fileman.Lock();
		if (fd < files->pfiles.Count() &&
			files->pfiles[fd] &&
			files->pfiles[fd]->vfile &&
			files->pfiles[fd]->vfile->f_inode &&
			(files->pfiles[fd]->vfile->f_inode->i_mode & I_TYPE_MASK) == I_NAMED_PIPE) {
			is_named_pipe = true;
		}
		if (fd < files->pfiles.Count() &&
			files->pfiles[fd] &&
			asrtand(files->pfiles[fd])->vfile &&
			asrtand(files->pfiles[fd]->vfile->f_dentry)->d_inode &&
			files->pfiles[fd]->vfile->f_dentry->d_inode->i_mode == I_CHAR_SPECIAL) {
			is_char_special = true;
		}
	}
	if (is_named_pipe) {
		return pb->Rdwt(false, fd, uni::Slice{ addr, len });
	}

	// TTY device
	if (is_char_special) {
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
				stduint tty_idx = (stduint)~0;
				{
					auto files = pb->fileman.Lock();
					if (fd < files->pfiles.Count() && files->pfiles[fd] && files->pfiles[fd]->vfile &&
						files->pfiles[fd]->vfile->f_inode) {
						tty_idx = (stduint)files->pfiles[fd]->vfile->f_inode->internal_handler;
					}
				}
				Console_t* con = nullptr;
				auto focus_tty = pb->focus_tty.Lock();
				if (tty_idx == (stduint)~0) {
					if (*focus_tty) con = (Console_t*)(*focus_tty)->offs;
				} else if (tty_idx < vttys.Count()) {
					con = (Console_t*)vttys[tty_idx]->offs;
				}

				// Handle the character "consumed" by Task_Console service
				byte ch = (byte)ret;
				if (ch == 0x04) { // Ctrl+D (EOF)
					break;
				}
				if (ch == '\b' || ch == 0x7F) {
					if (total_read > 0) {
						total_read--;
						if (con) con->out("\b \b", 3);
					}
					continue;
				}
				// Store in user buffer and provide visual echo
				MccaMemCopyP(
					(void*)(addr + total_read), pb, false,
					& ch, NULL, true,
					1);
				total_read++;
				if (con) con->OutChar(ch);

				if (ch == '\n') {
					if (con) con->OutChar('\r');
					break;
				}
				continue;
			}

			total_read += bytes_read;

			if (total_read > 0) {
				byte last_char;
				MccaMemCopyP(
					&last_char, NULL, true,
					(void*)(addr + total_read - 1), pb, false,
					1);
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
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th->parent_process;
	bool is_named_pipe = false;

	// Intercept named pipes to bypass Task_FileSys server and execute in current thread's context to avoid deadlocks.
	{
		auto files = pb->fileman.Lock();
		if (fd < files->pfiles.Count() &&
			files->pfiles[fd] &&
			files->pfiles[fd]->vfile &&
			files->pfiles[fd]->vfile->f_inode &&
			(files->pfiles[fd]->vfile->f_inode->i_mode & I_TYPE_MASK) == I_NAMED_PIPE) {
			is_named_pipe = true;
		}
	}
	if (is_named_pipe) {
		return pb->Rdwt(true, fd, uni::Slice{ addr, len });
	}

	stduint open_buf[4]{ fd, addr, len, th->getID() };
	syssend(Task_FileSys, open_buf, byteof(open_buf), _IMM(FilemanMsg::WRITE));
	sysrecv(Task_FileSys, open_buf, byteof(open_buf[0]));
	return open_buf[0];
}

DEFSYSC sysc_DELF(stduint usr_filepath) {
	stduint open_buf[2]{ usr_filepath, Taskman::CurrentTID() };
	syssend(Task_FileSys, open_buf, byteof(open_buf), _IMM(FilemanMsg::REMOVE));
	sysrecv(Task_FileSys, open_buf, byteof(open_buf[0]));
	return open_buf[0];// 0 for success
}

DEFSYSC sysc_PORP(stduint fd, stduint usr_proper) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th->parent_process;
	auto files = pb->fileman.Lock();

	// Check if fd is valid
	if (fd >= files->pfiles.Count() || !files->pfiles[fd] || !files->pfiles[fd]->vfile) {
		return -1; // Invalid file descriptor
	}

	auto* vfile = files->pfiles[fd]->vfile;
	auto* inode = vfile->f_inode;
	if (!inode) {
		return -1;
	}

	file_proper_t prop;
	prop.size = inode->i_size;
	prop.mode = inode->i_mode & I_TYPE_MASK;

	MccaMemCopyP(
		(void*)usr_proper, pb, false,
		&prop, nullptr, true,
		sizeof(prop));
	return 0; // Success
}

DEFSYSC sysc_ENUM(stduint fd, stduint addr, stduint count) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th->parent_process;

	// Check if fd is valid
	{
		auto files = pb->fileman.Lock();
		if (fd >= files->pfiles.Count() || !files->pfiles[fd] || !files->pfiles[fd]->vfile) {
			return -1; // Invalid file descriptor
		}
	}

	stduint open_buf[4]{ fd, addr, count, th->getID() };
	syssend(Task_FileSys, open_buf, byteof(open_buf), _IMM(FilemanMsg::ENUMER));
	sysrecv(Task_FileSys, open_buf, byteof(open_buf[0]));
	return open_buf[0];
}

DEFSYSC sysc_SETD(stduint usr_path) {
	ThreadBlock* th = Taskman::CurrentTB();
	stdsint open_buf[2]{ (stdsint)usr_path, (stdsint)th->getID() };
	syssend(Task_FileSys, open_buf, byteof(open_buf), _IMM(FilemanMsg::SETD));
	sysrecv(Task_FileSys, open_buf, byteof(open_buf[0]));
	return open_buf[0];
}

DEFSYSC sysc_GETD(stduint usr_buf, stduint size) {
	ThreadBlock* th = Taskman::CurrentTB();
	stdsint open_buf[3]{ (stdsint)usr_buf, (stdsint)size, (stdsint)th->getID() };
	syssend(Task_FileSys, open_buf, byteof(open_buf), _IMM(FilemanMsg::GETD));
	sysrecv(Task_FileSys, open_buf, byteof(open_buf[0]));
	return open_buf[0];
}

DEFSYSC sysc_WAIT(stduint pid, stduint usr_status) {

	// ploginfo("syscall wait");
	auto tb = Taskman::CurrentTB();
	stduint open_buf[3]{ tb->parent_process->getID(), pid, usr_status };
	syssend(Task_TaskMan, sliceof(open_buf), _IMM(TaskmanMsg::WAIT));
	stduint r = sysrecv(Task_TaskMan, sliceof(open_buf));// (pid, state)
	if ((stdsint)r < 0) return r; // Return -EINTR if interrupted
	stdsint ret = open_buf[0];// pid
	if (ret && usr_status) {
		auto pb = tb->parent_process;
		MccaMemCopyP(
			(void*)usr_status, pb, false,
			&open_buf[1], NULL, true,
			byteof(stduint));
	}
	return ret;
}

DEFSYSC sysx_FORK(CallgateFrame* phyzik_frame) {
	stduint msgbuf[2] = { Taskman::CurrentPID(), _IMM(phyzik_frame) };
	syssend(Task_TaskMan, sliceof(msgbuf), _IMM(TaskmanMsg::FORK));
	sysrecv(Task_TaskMan, &msgbuf, byteof(msgbuf[0]));
	return msgbuf[0];
}
DEFSYSC sysc_TMSG() {
	auto th = Taskman::CurrentTB();
	return _IMM(th->queue_send_queuehead) || th->async_messages.Count();
}

DEFSYSC sysc_EXEC(stduint path, stduint argv, stduint envp) {
	stduint msgbuf[4] = { Taskman::CurrentPID(), path, argv, envp };
	syssend(Task_TaskMan, sliceof(msgbuf), _IMM(TaskmanMsg::EXEC));
	sysrecv(Task_TaskMan, &msgbuf, byteof(msgbuf[0]));
	return msgbuf[0];
}
DEFSYSC sysc_EXET(stduint path, stduint argv, stduint envp) {
	stduint msgbuf[4] = { Taskman::CurrentPID(), path, argv, envp };
	syssend(Task_TaskMan, sliceof(msgbuf), _IMM(TaskmanMsg::EXET));
	sysrecv(Task_TaskMan, &msgbuf, byteof(msgbuf[0]));
	return msgbuf[0];
}


DEFSYSC sysc_TEST(stduint t, stduint e, stduint s) {
	ThreadBlock* caller_th = Taskman::CurrentTB();
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

void sysinfo_classic(OstreamTrait& com1, byte func);

DEFSYSC sysc_DBUG(stduint func) {
	auto th = Taskman::CurrentTB();
	if (th && th->parent_process) {
		ProcessBlock* pb = th->parent_process;
		struct DbugStream : public OstreamTrait {
			ProcessBlock* pb;
			DbugStream(ProcessBlock* pb) : pb(pb) {}
			virtual int out(const char* str, stduint len) override {
				if (pb && len) {
					auto files = pb->fileman.Lock();
					if (1 < files->pfiles.Count() && files->pfiles[1] && files->pfiles[1]->vfile) {
						FileDescriptor* fd_entry = files->pfiles[1];
						vfs_file* file = fd_entry->vfile;
						const bool is_magic_tty = file->f_inode &&
							(file->f_inode->i_mode & I_TYPE_MASK) == I_CHAR_SPECIAL &&
							(stduint)file->f_inode->internal_handler == (stduint)~0;
						if (is_magic_tty) {
							auto focus_tty = pb->focus_tty.Lock();
							if (*focus_tty && (*focus_tty)->offs) {
								Console_t* con = (Console_t*)(*focus_tty)->offs;
								con->out(str, len);
							}
							files.Unlock();
						} else {
							files.Unlock();
							Filesys::Write(file, str, len);
							auto files_re = pb->fileman.Lock();
							if (1 < files_re->pfiles.Count() && files_re->pfiles[1] == fd_entry && fd_entry->vfile == file) {
								fd_entry->fd_pos = file->f_pos;
							}
							files_re.Unlock();
						}
					} else {
						files.Unlock();
					}
				}
				return len;
			}
		} stream(pb);
		sysinfo_classic(stream, (byte)func);
		// sysinfo_classic(*(Console_t*)vttys[1]->offs, (byte)func);
		return 0;
	}
	return -1;
}


DEFSYSC sysc_SIGA(stduint sig, stduint act, stduint oact) {
	auto pb = Taskman::CurrentPB();
	
	stduint phys_act = act ? (stduint)pb->paging[act] : 0;
	stduint phys_oact = oact ? (stduint)pb->paging[oact] : 0;
	
	const struct _POSIX_sigaction* p_act = act ? (const struct _POSIX_sigaction*)(phys_act) : nullptr;
	struct _POSIX_sigaction* p_oact = oact ? (struct _POSIX_sigaction*)(phys_oact) : nullptr;
	
	if (oact && _IMM(phys_oact) == ~_IMM0) {
		plogerro("sysc_SIGA: oact address 0x%[x] not mapped in user page table!", oact);
		return -1;
	}
	
	return (stdsint)sys_sigaction((int)sig, p_act, p_oact);
}

DEFSYSC sysc_KILL(stduint pid, stduint sig, stduint tid) {
	return (stdsint)sys_kill(pid, (int)sig, tid);
}

DEFSYSC sysc_MMAP(stduint len, stduint flag, stduint fd) {
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th->parent_process;
	if (&pb->paging == &kernel_paging) {
		return ~_IMM0; // Reject kernel page table
	}
	len = (len + 0xFFF) & ~_IMM(0xFFF); // Align to 4KB page size
	SpinlockLocal guard(&pb->vma_lock);
	stduint addr = pb->heaptop;
	pb->heaptop += len;

	VirtualMemoryArea vma(addr, pb->heaptop, flag);
	if (fd != ~_IMM0) {
		auto files = pb->fileman.Lock();
		if (fd < files->pfiles.Count() && files->pfiles[fd] && files->pfiles[fd]->vfile) {
			uni::vfs_file* parent_vfile = files->pfiles[fd]->vfile;
			uni::vfs_file* nvfile = (uni::vfs_file*)malc(sizeof(uni::vfs_file));
			if (nvfile) {
				*nvfile = *parent_vfile;
				if (nvfile->f_inode) {
					nvfile->f_inode->ref_count++;
				}
				vma.vm_type = VMA_FILE;
				vma.vfile = nvfile;
				vma.file_offset = 0;
			}
		}
	}
	pb->vmas.Append(vma);
	return addr;
}

DEFSYSC sysc_UMAP(stduint addr, stduint len) {
	if ((addr & 0xFFF) || (len & 0xFFF)) {
		return ~_IMM0; // Address and length must be 4KB aligned
	}
	ThreadBlock* th = Taskman::CurrentTB();
	ProcessBlock* pb = th->parent_process;
	if (&pb->paging == &kernel_paging) {
		return ~_IMM0; // Reject kernel page table
	}
	SpinlockLocal guard(&pb->vma_lock);
	
	// Write back dirty pages for file-backed mappings in the unmapped range
	for (stduint i = 0; i < pb->vmas.Count(); i++) {
		auto& vma = pb->vmas[i];
		if (vma.vm_type == VMA_FILE && vma.vfile && (vma.vm_flags & PGPROP_writable)) {
			stduint overlap_start = maxof(vma.vm_start, addr);
			stduint overlap_end = minof(vma.vm_end, addr + len);
			if (overlap_start < overlap_end) {
				for (stduint page_addr = overlap_start; page_addr < overlap_end; page_addr += 0x1000) {
					void* phys = pb->paging[page_addr];
					if (phys != (void*)~_IMM0) {
						stduint file_offset = page_addr - vma.vm_start + vma.file_offset;
						if (file_offset < vma.vfile->f_inode->i_size) {
							stduint write_len = minof(_IMM(0x1000), vma.vfile->f_inode->i_size - file_offset);
							vma.vfile->f_pos = file_offset;
							Filesys::Write(vma.vfile, (const void*)mglb(phys), write_len);
						}
					}
				}
			}
		}
	}

	// Unmap and free physical memory
	for (stduint curr = addr; curr < addr + len; curr += 0x1000) {
		void* phys_addr = pb->paging[curr];
		if (phys_addr != (void*)~_IMM0) {
			bool release_phys = true;
			for (stduint i = 0; i < pb->vmas.Count(); i++) {
				const auto& vma = pb->vmas[i];
				if (curr >= vma.vm_start && curr < vma.vm_end) {
					release_phys = vma.vm_type != VMA_DEVICE;
					break;
				}
			}
			if (release_phys) {
				free(phys_addr);
			}
			pb->paging.Unmap(curr, 0x1000);
			RefreshVirtualAddress(curr);
		}
	}
	
	// Remove or split VMAs in [addr, addr + len)
	for (stduint i = 0; i < pb->vmas.Count(); ) {
		auto& vma = pb->vmas[i];
		if (vma.vm_start >= addr && vma.vm_end <= addr + len) {
			if (vma.vm_type == VMA_FILE && vma.vfile) {
				if (vma.vfile->f_inode) vma.vfile->f_inode->ref_count--;
				Filesys::Close(vma.vfile);
			}
			pb->vmas.Remove(i, 1);
		} else if (vma.vm_start < addr && vma.vm_end > addr + len) {
			stduint old_end = vma.vm_end;
			vma.vm_end = addr;
			VirtualMemoryArea new_vma(addr + len, old_end, vma.vm_flags);
			if (vma.vm_type == VMA_FILE && vma.vfile) {
				uni::vfs_file* nvfile = (uni::vfs_file*)malc(sizeof(uni::vfs_file));
				if (nvfile) {
					*nvfile = *(vma.vfile);
					if (nvfile->f_inode) nvfile->f_inode->ref_count++;
					new_vma.vm_type = VMA_FILE;
					new_vma.vfile = nvfile;
					new_vma.file_offset = vma.file_offset + (addr + len - vma.vm_start);
				}
			}
			pb->vmas.Insert(i + 1, &new_vma);
			i += 2;
		} else if (vma.vm_start < addr && vma.vm_end > addr) {
			vma.vm_end = addr;
			i++;
		} else if (vma.vm_start < addr + len && vma.vm_end > addr + len) {
			if (vma.vm_type == VMA_FILE && vma.vfile) {
				vma.file_offset = vma.file_offset + (addr + len - vma.vm_start);
			}
			vma.vm_start = addr + len;
			i++;
		} else {
			i++;
		}
	}
	
	if (addr + len == pb->heaptop) {
		pb->heaptop = addr;
	}
	return len;
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
	mglb(sysc_PORP),
	mglb(sysc_ENUM),// U-only
	mglb(sysc_WAIT),
	mglb(sysx_FORK) & 0,// Fork need frame, process it other way
	mglb(sysc_TMSG),
	mglb(sysc_EXEC),// 0x10
	mglb(sysc_EXET),
	0, //{} mglb(sysc_MALC), // 0x12 unchk
	mglb(sysc_SIGA), // 0x13 unchk
	mglb(sysc_KILL), // 0x14 unchk
	0, // 0x15 unchk (handled manually to pass context frame)
	mglb(sysc_SETD), // 0x16 (SETD)
	mglb(sysc_GETD), // 0x17 (GETD)
	mglb(sysc_MMAP), // 0x18 (MMAP)
	mglb(sysc_UMAP), // 0x19 (UMAP)
	mglb(sysc_DUP2), // 0x1A (DUP2)
	mglb(sysc_PIPE), // 0x1B (PIPE)
	mglb(sysc_SEEK), // 0x1C (SEEK)
	0, // 0x1D (GET_CORE_ID)
	mglb(sysc_MANA),
	mglb(sysc_TNEW),
	mglb(sysc_TEXI),
	mglb(sysc_TJOI),
	mglb(sysc_TGET),
	mglb(sysc_TDET),
	mglb(sysc_TYLD),
	mglb(sysc_FUTX),
	mglb(sysc_SOCK),
	mglb(sysc_BIND),
	mglb(sysc_CONN),
	mglb(sysc_SEND),
	mglb(sysc_RECV),
	mglb(sysc_ROUT),
	mglb(sysc_FCTL),
	mglb(sysc_SADR),
	mglb(sysc_SOPT),
	mglb(sysc_POLL),
	mglb(sysc_LIST),
	mglb(sysc_ACPT),
};
#endif

#endif

#if (_MCCA & 0xFF00) == 0x1000

static int sysc_GETCID(stduint ptr_hid)
{
	auto pid = Taskman::CurrentTB();
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
	case syscall_t::SETD:
		cxt->a0 = sysc_SETD(cxt->a0);
		break;
	case syscall_t::GETD:
		cxt->a0 = sysc_GETD(cxt->a0, cxt->a1);
		break;
	case syscall_t::PORP:
		cxt->a0 = sysc_PORP(cxt->a0, cxt->a1);
		break;
	case syscall_t::ENUM:
		cxt->a0 = sysc_ENUM(cxt->a0, cxt->a1, cxt->a2);
		break;
	case syscall_t::DUP2:
		cxt->a0 = sysc_DUP2(cxt->a0, cxt->a1);
		break;
	case syscall_t::PIPE:
		cxt->a0 = sysc_PIPE(cxt->a0);
		break;
	case syscall_t::FCTL:
		cxt->a0 = sysc_FCTL(cxt->a0, cxt->a1, cxt->a2);
		break;
	case syscall_t::SADR:
		cxt->a0 = sysc_SADR(cxt->a0, cxt->a1, cxt->a2);
		break;
	case syscall_t::SOPT:
		cxt->a0 = sysc_SOPT(cxt->a0, cxt->a1, cxt->a2);
		break;
	case syscall_t::POLL:
		cxt->a0 = sysc_POLL(cxt->a0, cxt->a1, cxt->a2);
		break;
	case syscall_t::LIST:
		cxt->a0 = sysc_LIST(cxt->a0, cxt->a1, cxt->a2);
		break;
	case syscall_t::ACPT:
		cxt->a0 = sysc_ACPT(cxt->a0, cxt->a1, cxt->a2);
		break;
	case syscall_t::SEEK:
		cxt->a0 = sysc_SEEK(cxt->a0, cxt->a1, cxt->a2);
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
	auto crt_th = Taskman::CurrentTB();
	bool was_pinned = false;
	const stduint entry_cpu_id = Taskman::getID();
	if (crt_th) {
		#if _MCCA == 0x8632
		if (entry_cpu_id != 0) {
			if (crt_th->ring_coreid != CORE_ID_INVALID) {
				was_pinned = true;
			} else {
				crt_th->ring_coreid = entry_cpu_id;
			}
		}
		#endif
	}
	auto pb = Taskman::CurrentPB();
	#if _MCCA == 0x8632
	CallgateFrame frame_copy = *frame;
	CallgateFrame* active_frame = &frame_copy;
	auto callid = (syscall_t)active_frame->ax;
	stduint para[4] = { active_frame->cx, active_frame->dx, active_frame->bx };
	#elif _MCCA == 0x8664
	// frame is now always on the real kernel stack, no need to translate it via user page table
	auto callid = (syscall_t)(frame->ax & 0x7FFFFFFFu);
	stduint para[4] = { frame->di, frame->si, frame->dx };
	#endif

	stduint ret_val = -1;
	if (IsPwcall(callid)) {
		ret_val = HandlePwcall(callid, para[0], para[1], para[2]);
	}
	else if (_IMM(callid) < numsof(SYSCALL_TABLE) && SYSCALL_TABLE[_IMM(callid)]) {
		ret_val = (reinterpret_cast<stdsint(*)(stduint, stduint, stduint)>(SYSCALL_TABLE[_IMM(callid)]))(para[0], para[1], para[2]);
	}
	else switch (callid) {
	case syscall_t::FORK:
		#if _MCCA == 0x8632
		ret_val = sysx_FORK(active_frame);
		#else
		ret_val = sysx_FORK(frame);
		#endif
		break;
	case syscall_t::SIGR:
		#if _MCCA == 0x8632
		ret_val = sysc_SIGR(active_frame);
		#else
		ret_val = sysc_SIGR(frame);
		#endif
		break;
	case syscall_t::TEST: ret_val = sysc_TEST(para[0], para[1], para[2]); break;
	case syscall_t::DBUG: ret_val = sysc_DBUG(para[0]); break;
	
	default:
		printlog(_LOG_ERROR, "Unimplemented syscall: 0x%[32H]", _IMM(callid));
		break;
	}

	#if _MCCA == 0x8632
	// Is these useful?
	if (callid == syscall_t::EXET && ret_val == 0) {
		auto th = crt_th ? crt_th : Taskman::CurrentTB();

		if (th) {
			active_frame->ip = th->context.IP;
			active_frame->sp0 = th->context.SP;
			active_frame->cs = th->context.CS;
			active_frame->ss0 = th->context.SS;
			active_frame->flags = th->context.FLAG | 0x200;

			active_frame->cr3 = th->context.CR3;

			active_frame->ds = th->context.DS;
			active_frame->es = th->context.ES;
			active_frame->fs = th->context.FS;
			active_frame->gs = th->context.GS;
		}
	}

	active_frame->ax = ret_val;
	check_and_deliver_signals_syscall(active_frame);
	//
	auto resume_th = Taskman::CurrentTB();
	const stduint resume_cpu_id = resume_th ? resume_th->processor_id : Taskman::getID();
	if (resume_cpu_id < PCU_CORES_MAX) {
		active_frame->percore_ptr = _IMM(Taskman::PCU_CORES_PERCORE[resume_cpu_id]);
	}
	//
	*frame = frame_copy;
	#else
	frame->ax = ret_val;
	check_and_deliver_signals_syscall(frame);
	#endif
	if (crt_th && !was_pinned) {
		crt_th->ring_coreid = CORE_ID_INVALID;
	}
	return frame->ax;
}
#endif
