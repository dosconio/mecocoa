// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// #pragma GCC optimize("O2")
#include "../include/mecocoa.hpp"

#include <c/cpuid.h>
#include <c/driver/mouse.h>
#include <c/driver/keyboard.h>
#include <c/driver/RealtimeClock.h>
#include "../include/console.hpp"

String dump_availmem();
rostr text_cpu_factory();

Procontroller_t cpu_type = PCU_Unknown;

void mecfetch() {
	#if ((_MCCA & 0xFF00) == 0x8600)
	const rostr blue = "\033[48;2;88;200;248m"; 	//  ento_gui ? "\xFE\xF8\xC8\x58" : "\xFF\x30";
	const rostr pink = "\033[48;2;248;168;184m";	//  ento_gui ? "\xFE\xB8\xA8\xF8" : "\xFF\x50";
	const rostr white = "\033[48;2;255;255;255m";	//  ento_gui ? "\xFE\xFF\xFF\xFF" : "\xFF\x70";
	const unsigned attrl = Consman::ento_gui ? 4 : 2;
	const unsigned width = Consman::ento_gui ? 48 : 16;
	const unsigned height = Consman::ento_gui ? 3 : 1;

	#if _GUI_LOGO
	Console.OutFormat(blue, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\033[0m\n\r%s", blue); }
	Console.OutFormat(pink, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\033[0m\n\r%s", pink); }
	Console.OutFormat(white, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\033[0m\n\r%s", white); }
	Console.OutFormat(pink, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\033[0m\n\r%s", pink); }
	Console.OutFormat(blue, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\033[0m\n\r%s", blue); }
	Console.OutFormat("\033[0m");// Console.out("\xFF\xFF", 2);
	#endif

	printlog(_LOG_TRACE, "メココア");
	printlog(cpu_type ? _LOG_GOOD : _LOG_WARN, "CPU Factory: %s", text_cpu_factory());
	ploginfo("CPU Brand: %s", text_brand());

	tm datime = {};
	#if _MCCA == 0x8632 //((_MCCA & 0xFF00) == 0x8600)
	CMOS_Readtime(&datime);
	#endif
	Console.OutFormat("Date Time: %d/%d/%d %d:%d:%d\n\r",
		datime.tm_year, datime.tm_mon, datime.tm_mday,
		datime.tm_hour, datime.tm_min, datime.tm_sec
	);
	#endif

	Console.OutFormat("Avail Mem: %s\n\r", dump_availmem().reference());
}// like neofetch

#if ((_MCCA & 0xFF00) == 0x8600)

char _buf[64];
String ker_buf(_buf, byteof(_buf));
extern uint32 _start_eax, _start_ebx;

void kernel_fail(void* _serious, ...) {
	Letvar(serious, loglevel_t, _IMM(_serious));
	if (serious == _LOG_FATAL) {
		outsfmt("\n\rKernel panic!\n\r");
		__asm("cli; hlt");
	}
}

rostr text_cpu_factory() {
	unsigned a[1], b[1], c[1], d[1];
	unsigned* ker_ptr = (unsigned*)ker_buf.reflect();
	_IO_CPUID(0, 0, a, b, c, d);
	*ker_ptr++ = *b;
	*ker_ptr++ = *d;
	*ker_ptr++ = *c;
	*ker_ptr = 0;
	if (StrCompare("GenuineIntel", ker_buf.reference()) == 0) cpu_type = PCU_Intel;
	else if (StrCompare("AuthenticAMD", ker_buf.reference()) == 0) cpu_type = PCU_AMD;
	else cpu_type = PCU_Unknown;
	return ker_buf.reference();
}

rostr text_brand() {
	CpuBrand(ker_buf.reflect());
	ker_buf.reflect()[_CPU_BRAND_SIZE] = 0;
	const char* ret = ker_buf.reference();
	while (*ret == ' ') ret++;
	return ret;
}

#endif

String dump_availmem() {
	stduint mem = Memory::total_memsize;
	stduint frac = 0;
	char unit[]{ ' ', 'K', 'M', 'G', 'T' };
	int level = 0;
	// assert mem != 0
	while (mem >= 1024 && level < 4) {
		frac = (mem % 1024) * 100 / 1024;
		mem /= 1024;
		level++;
	}
	String ret;
	if (level) ret.Format("%u.%02u %cB", (unsigned int)mem, (unsigned int)frac, unit[level]);
	else ret.Format("0x%[x] B", Memory::total_memsize);
	return ret;
}

void dump_threads(OstreamTrait& com1) {
	// print all threads
	extern Spinlock scheduler_lock;
	SpinlockLocal guard(&scheduler_lock);
	com1.OutFormat("TID  PID   STATE     REASON   CPU PRI  IP          SP\n\r");
	com1.OutFormat("           NAME      SEND    RECV    QHEAD    QNEXT\n\r");
	for (auto nod = Taskman::thchain.Root(); nod; nod = nod->next) {
		auto th = cast<ThreadBlock*>(nod->offs);

		// TID
		com1.OutFormat("%[u]", th->tid);
		if (th->tid < 10) com1.OutFormat("    ");
		else if (th->tid < 100) com1.OutFormat("   ");
		else com1.OutFormat("  ");

		// PID
		stduint pid = th->parent_process ? th->parent_process->pid : 0;
		com1.OutFormat("%[u]", pid);
		if (pid < 10) com1.OutFormat("     ");
		else if (pid < 100) com1.OutFormat("    ");
		else com1.OutFormat("   ");

		// STATE
		const char* state_name = "Unknown";
		switch (th->state) {
		case ThreadBlock::State::Running: state_name = "Running"; break;
		case ThreadBlock::State::Ready:   state_name = "Ready";   break;
		case ThreadBlock::State::Pended:  state_name = "Pended";  break;
		case ThreadBlock::State::Uninit:  state_name = "Uninit";  break;
		case ThreadBlock::State::Exited:  state_name = "Exited";  break;
		case ThreadBlock::State::Hanging: state_name = "Hanging"; break;
		case ThreadBlock::State::Invalid: state_name = "Invalid"; break;
		}
		com1.OutFormat("%s", state_name);
		stduint state_len = 0;
		while (state_name[state_len]) state_len++;
		for (stduint s = state_len; s < 10; s++) {
			com1.OutFormat(" ");
		}

		// REASON
		const char* reason_name = "None";
		if (th->state == ThreadBlock::State::Pended) {
			if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_Resting)) reason_name = "Rest";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_SendMsg)) reason_name = "Send";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_RecvMsg)) reason_name = "Recv";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_Waiting)) reason_name = "Wait";
		}
		com1.OutFormat("%s", reason_name);
		stduint reason_len = 0;
		while (reason_name[reason_len]) reason_len++;
		for (stduint s = reason_len; s < 9; s++) {
			com1.OutFormat(" ");
		}

		// CPU
		if (th->processor_id == CORE_ID_INVALID) {
			com1.OutFormat("-   ");
		}
		else {
			com1.OutFormat("%[u]", th->processor_id);
			if (th->processor_id < 10) com1.OutFormat("   ");
			else com1.OutFormat("  ");
		}

		// PRI
		com1.OutFormat("%[i]", (stdsint)th->priority);
		stdsint pri = th->priority;
		if (pri >= 0) {
			if (pri < 10) com1.OutFormat("    ");
			else com1.OutFormat("   ");
		}
		else {
			if (pri > -10) com1.OutFormat("   ");
			else com1.OutFormat("  ");
		}

		// IP and SP
		com1.OutFormat("%p  %p\n\r", th->context.IP, th->context.SP);
		String str[4]{ "(null)", "(null)", "(null)", "(null)" };
		if (th->send_to_whom) {
			str[0].Format("%u", th->send_to_whom->getID());
		}
		if (th->recv_fo_whom) {
			if (_IMM(th->recv_fo_whom) == ~_IMM0) str[1] = "(RUPT)";
			else str[1].Format("%u", th->recv_fo_whom->getID());
		}
		if (th->queue_send_queuehead) {
			str[2].Format("%u", th->queue_send_queuehead->getID());
		}
		if (th->queue_send_queuenext) {
			str[3].Format("%u", th->queue_send_queuenext->getID());
		}

		com1.OutFormat("           %s %s %s %s %s\n\r",
			th->name, str[0].reference(), str[1].reference(), str[2].reference(), str[3].reference());
	}
}

void dump_processors(OstreamTrait& com1) {
	extern Spinlock scheduler_lock;
	SpinlockLocal guard(&scheduler_lock);
	com1.OutFormat("CPU   STATE     LAPIC CURRENT_TID   SWITCHING_TID KSTACK\n\r");
	for (stduint i = 0; i < Taskman::PCU_CORES; i++) {
		#if (_MCCA & 0xFF00) == 0x8600
		auto percore = Taskman::PCU_CORES_PERCORE[i];
		if (!percore) continue;

		// CPU Core ID
		com1.OutFormat("%[u]", i);
		if (i < 10) com1.OutFormat("     ");
		else com1.OutFormat("    ");

		// State
		const char* state_name = "Unknown";
		switch (percore->state) {
		case CoreState::Empty:    state_name = "Empty";    break;
		case CoreState::Prepared: state_name = "Prepared"; break;
		case CoreState::Booting:  state_name = "Booting";  break;
		case CoreState::Online:   state_name = "Online";   break;
		case CoreState::Failed:   state_name = "Failed";   break;
		}
		com1.OutFormat("%s", state_name);
		stduint state_len = 0;
		while (state_name[state_len]) state_len++;
		for (stduint s = state_len; s < 10; s++) com1.OutFormat(" ");

		// LAPIC ID
		if (percore->lapic_id == CORE_ID_INVALID) {
			com1.OutFormat("-     ");
		}
		else {
			com1.OutFormat("%[u]", (stduint)percore->lapic_id);
			if (percore->lapic_id < 10) com1.OutFormat("     ");
			else if (percore->lapic_id < 100) com1.OutFormat("    ");
			else com1.OutFormat("   ");
		}

		// Current Thread
		if (percore->current_thread) {
			com1.OutFormat("%[u]", percore->current_thread->tid);
			stduint tid = percore->current_thread->tid;
			if (tid < 10) com1.OutFormat("             ");
			else if (tid < 100) com1.OutFormat("            ");
			else com1.OutFormat("           ");
		}
		else {
			com1.OutFormat("-             ");
		}

		// Switching Out Thread
		if (percore->switching_out_thread) {
			com1.OutFormat("%[u]", percore->switching_out_thread->tid);
			stduint tid = percore->switching_out_thread->tid;
			if (tid < 10) com1.OutFormat("             ");
			else if (tid < 100) com1.OutFormat("            ");
			else com1.OutFormat("           ");
		}
		else {
			com1.OutFormat("-             ");
		}

		// Kernel Stack
		com1.OutFormat("%p\n\r", percore->kernel_stack);

		#else
				// For non-x86 architectures
		com1.OutFormat("%[u]     Online    -     ", i);

		auto cur_th = Taskman::current_thread(i);
		if (cur_th) {
			com1.OutFormat("%[u]", cur_th->tid);
			stduint tid = cur_th->tid;
			if (tid < 10) com1.OutFormat("             ");
			else if (tid < 100) com1.OutFormat("            ");
			else com1.OutFormat("           ");
		}
		else {
			com1.OutFormat("-             ");
		}

		auto sw_th = Taskman::switching_out_threads(i);
		if (sw_th) {
			com1.OutFormat("%[u]", sw_th->tid);
			stduint tid = sw_th->tid;
			if (tid < 10) com1.OutFormat("             ");
			else if (tid < 100) com1.OutFormat("            ");
			else com1.OutFormat("           ");
		}
		else {
			com1.OutFormat("-             ");
		}

		com1.OutFormat("-\n\r");
		#endif
	}
}

void dump_ready_queue(OstreamTrait& com1) {
	extern Spinlock scheduler_lock;
	SpinlockLocal guard(&scheduler_lock);

	com1.OutFormat("=== Active Ready Queues ===\n\r");
	bool active_any = false;
	for (int idx = 31; idx >= 0; idx--) {
		if (Taskman::priority_queues[idx].head) {
			active_any = true;
			com1.OutFormat("  Pri %[i] (idx %[i]):", (stdsint)idx - 16, idx);
			ThreadBlock* node = Taskman::priority_queues[idx].head;
			stduint guard_cnt = 0;
			while (node) {
				com1.OutFormat(" -> Th%[u]", node->tid);
				node = node->queue_state_next;
				if (++guard_cnt > 4096) break;
			}
			com1.OutFormat("\n\r");
		}
	}
	if (!active_any) {
		com1.OutFormat("  (empty)\n\r");
	}

	com1.OutFormat("=== Expired Ready Queues ===\n\r");
	bool expired_any = false;
	for (int idx = 31; idx >= 0; idx--) {
		if (Taskman::expired_queues[idx].head) {
			expired_any = true;
			com1.OutFormat("  Pri %[i] (idx %[i]):", (stdsint)idx - 16, idx);
			ThreadBlock* node = Taskman::expired_queues[idx].head;
			stduint guard_cnt = 0;
			while (node) {
				com1.OutFormat(" -> Th%[u]", node->tid);
				node = node->queue_state_next;
				if (++guard_cnt > 4096) break;
			}
			com1.OutFormat("\n\r");
		}
	}
	if (!expired_any) {
		com1.OutFormat("  (empty)\n\r");
	}
}

static Spinlock log_lock;
void printlog(loglevel_t level, const char* fmt, ...)
{
	SpinlockLocal lock(&log_lock);
	Letpara(paras, fmt);
	printlogx(level, fmt, paras);
	para_endo(paras);
}

//// ---- POWER MANAGER ---- ////

