// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// #pragma GCC optimize("O2")
#include "../include/mecocoa.hpp"

#include <c/cpuid.h>
#include <c/driver/mouse.h>
#include <c/driver/keyboard.h>
#include <c/driver/RealtimeClock.h>
#if (_MCCA & 0xFF00) == 0x8600
#include <cpp/Device/Bus/Serial/IIC-HID.hpp>
#include <cpp/Device/IIC>
#endif
#include "../include/console.hpp"
#include "../depends/desktop.hpp"

String dump_availmem();
rostr text_cpu_factory();

Procontroller_t cpu_type = PCU_Unknown;

loglevel_t _log_limit = _LOG_INFO;

void mecfetch() {
	#if ((_MCCA & 0xFF00) == 0x8600)
	const rostr blue = "\033[48;2;88;200;248m"; 	//  ento_gui ? "\xFE\xF8\xC8\x58" : "\xFF\x30";
	const rostr pink = "\033[48;2;248;168;184m";	//  ento_gui ? "\xFE\xB8\xA8\xF8" : "\xFF\x50";
	const rostr white = "\033[48;2;255;255;255m";	//  ento_gui ? "\xFE\xFF\xFF\xFF" : "\xFF\x70";
	const unsigned attrl = Consman::ento_gui ? 4 : 2;
	const unsigned width = Consman::ento_gui ? 48 : 16;
	const unsigned height = Consman::ento_gui ? 3 : 1;

	#if CONFIG_ECHO_LOGO
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

void ScreenLogPanic();

void kernel_fail(void* _serious, ...) {
	Letvar(serious, loglevel_t, _IMM(_serious));
	if (serious == _LOG_FATAL) {
		#ifndef _UEFI_AUFDBG
		outsfmt("\n\rKernel panic!\n\r");
		#else
		ScreenLogPanic();
		#endif
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

extern file_system_type* registered_filesystems;
static void dump_filesys(OstreamTrait& com1) {
	for (file_system_type* fs_type = registered_filesystems; fs_type; fs_type = fs_type->next) {
		com1.OutFormat("- %s \n\r", fs_type->name);
	}
}

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
	auto pad_column = [&com1](int used, int width) {
		for (; used < width; used++) com1.OutChar(' ');
	};
	const int pointer_column = sizeof(stduint) * 2 + 4; // 0x + hex digits + two spaces
	com1.OutFormat("\033[1mTID  PID   STATE     REASON   CPU \033[35mPRI  \033[32mNAME\033[0m\n\r");
	int header_used = 0;
	com1.OutFormat("\033[1;36m    ");
	com1.OutFormat("IP%n", &header_used);
	pad_column(header_used, pointer_column);
	com1.OutFormat("SP%n", &header_used);
	pad_column(header_used, pointer_column);
	com1.OutFormat("\033[33m");
	com1.OutFormat("LINE_PEAK%n", &header_used);
	pad_column(header_used, 10);
	com1.OutFormat("KERN_PEAK\033[0m\n\r");
	com1.OutFormat("\033[1;34m    SEND     RECV     QHEAD    QNEXT    BR       UMSG\033[0m\n\r");
	for (auto nod = Taskman::thchain.Root(); nod; nod = nod->next) {
		auto th = cast<ThreadBlock*>(nod->offs);
		int used = 0;

		// TID
		com1.OutFormat("%[u]%n", th->tid, &used);
		pad_column(used, 5);

		// PID
		stduint pid = th->parent_process ? th->parent_process->pid : 0;
		com1.OutFormat("%[u]%n", pid, &used);
		pad_column(used, 6);

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
		com1.OutFormat("%s%n", state_name, &used);
		pad_column(used, 10);

		// REASON
		const char* reason_name = "None";
		if (th->state == ThreadBlock::State::Pended) {
			if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_Resting)) reason_name = "Rest";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_SendMsg)) reason_name = "Send";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_RecvMsg)) reason_name = "Recv";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_Waiting)) reason_name = "Wait";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_Lock)) reason_name = "Lock";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_SendPipe)) reason_name = "PipeW";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_RecvPipe)) reason_name = "PipeR";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_SendSock)) reason_name = "SockW";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_RecvSock)) reason_name = "SockR";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_Exiting)) reason_name = "Exit";
		}
		com1.OutFormat("%s%n", reason_name, &used);
		pad_column(used, 9);

		// CPU
		if (th->processor_id == CORE_ID_INVALID) {
			com1.OutFormat("-%n", &used);
		}
		else {
			com1.OutFormat("%[u]%n", th->processor_id, &used);
		}
		pad_column(used, 4);

		// PRI
		com1.OutFormat("\033[35m");
		com1.OutFormat("%[i]%n", (stdsint)th->priority, &used);
		pad_column(used, 5);
		com1.OutFormat("\033[32m%s\033[0m\n\r", th->name.get());

		// IP, SP and sampled stack peaks
		com1.OutFormat("\033[36m    ");
		com1.OutFormat("%p%n", th->context.IP, &used);
		pad_column(used, pointer_column);
		com1.OutFormat("%p%n", th->context.SP, &used);
		pad_column(used, pointer_column);
		com1.OutFormat("\033[33m");
		#if CONFIG_DEBUG_STACKSIZE
		if (th->stack_lineaddr && th->stack_lineaddr != th->stack_levladdr)
			com1.OutFormat("%[u]%n", th->max_linestack_usage, &used);
		else
			com1.OutFormat("-%n", &used);
		pad_column(used, 10);
		if (th->stack_levladdr)
			com1.OutFormat("%[u]\033[0m\n\r", th->max_phyzstack_usage);
		else
			com1.OutFormat("-\033[0m\n\r");
		#else
		com1.OutFormat("-         -\033[0m\n\r");
		#endif

		// IPC state
		String str[6]{ "(null)", "(null)", "(null)", "(null)", "(null)", "0" };
		if (th->send_to_whom) {
			str[0].Format("%u", th->send_to_whom->getID());
		}
		if (th->recv_fo_whom) {
			if ((stduint)th->recv_fo_whom == INTRUPT) str[1] = "(RUPT)";
			else if ((stduint)th->recv_fo_whom == ANYPROC) str[1] = "(ANY)";
			else str[1].Format("%u", th->recv_fo_whom->getID());
		}
		if (th->queue_send_queuehead) {
			str[2].Format("%u", th->queue_send_queuehead->getID());
		}
		if (th->queue_send_queuenext) {
			str[3].Format("%u", th->queue_send_queuenext->getID());
		}
		if (th->unsolved_msg) {
			if (_IMM(th->unsolved_msg) == ~_IMM0) str[4] = "(DEAD)";
			else str[4].Format("%p", (void*)th->unsolved_msg);
		}
		str[5].Format("%u", _IMM(th->block_reason));

		com1.OutFormat("\033[34m    ");
		com1.OutFormat("%s%n", str[0].reference(), &used);
		pad_column(used, 9);
		com1.OutFormat("%s%n", str[1].reference(), &used);
		pad_column(used, 9);
		com1.OutFormat("%s%n", str[2].reference(), &used);
		pad_column(used, 9);
		com1.OutFormat("%s%n", str[3].reference(), &used);
		pad_column(used, 9);
		com1.OutFormat("%s%n", str[5].reference(), &used);
		pad_column(used, 9);
		com1.OutFormat("%s\033[0m\n\r", str[4].reference());
	}
}

extern "C" void* ring3_iret_stacks[PCU_CORES_MAX];
void dump_processors(OstreamTrait& com1) {
	extern Spinlock scheduler_lock;
	SpinlockLocal guard(&scheduler_lock);
	com1.OutFormat("CPU   STATE     LAPIC CURRENT_TID   SWITCH_TID TRAMPOLINE KSTACK\n\r");
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
			com1.OutFormat("%10u    ", percore->current_thread->tid);
			stduint tid = percore->current_thread->tid;
		}
		else {
			com1.OutFormat("-             ");
		}

		// Switching Out Thread
		if (percore->switching_out_thread) {
			com1.OutFormat("%10u ", percore->switching_out_thread->tid);
		}
		else {
			com1.OutFormat("-          ");
		}

		// TRAMPS
		com1.OutFormat("%p ", ring3_iret_stacks[i]);

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

namespace uni {
	extern Mutex vfs_lock;
}

void dump_lock(OstreamTrait& com1) {
	// Declare the external global spinlocks
	extern Spinlock scheduler_lock;
	extern Spinlock timer_lock;
	extern Spinlock mempool_lock;
	extern Spinlock comm_lock;
	extern Spinlock log_lock;

	// Declare the external global mutexes
	extern Mutex outc_mutex;
	#if _MCCA == 0x8632 && _GUI_ENABLE && _GUI_FREETYPE
	extern Mutex ft_lock;
	#endif

	com1.OutFormat("=== Global Spinlocks ===\n\r");
	com1.OutFormat("Address             Locked  HCPUID  Name\n\r");
	com1.OutFormat("%p  %s     %[i]      scheduler_lock\n\r", &scheduler_lock, scheduler_lock.locked ? "Yes" : "No ", (stdsint)scheduler_lock.cpu_id);
	com1.OutFormat("%p  %s     %[i]      timer_lock\n\r", &timer_lock, timer_lock.locked ? "Yes" : "No ", (stdsint)timer_lock.cpu_id);
	com1.OutFormat("%p  %s     %[i]      mempool_lock\n\r", &mempool_lock, mempool_lock.locked ? "Yes" : "No ", (stdsint)mempool_lock.cpu_id);
	com1.OutFormat("%p  %s     %[i]      comm_lock\n\r", &comm_lock, comm_lock.locked ? "Yes" : "No ", (stdsint)comm_lock.cpu_id);
	com1.OutFormat("%p  %s     %[i]      log_lock\n\r", &log_lock, log_lock.locked ? "Yes" : "No ", (stdsint)log_lock.cpu_id);

	com1.OutFormat("\n\r=== Global Mutexes ===\n\r");
	com1.OutFormat("Address             Locked  Waiters Name\n\r");
	com1.OutFormat("%p  %s     %[u]       outc_mutex\n\r", &outc_mutex, outc_mutex.locked ? "Yes" : "No ", outc_mutex.wait_queue.Count());
	com1.OutFormat("%p  %s     %[u]       vfs_lock\n\r", &uni::vfs_lock, uni::vfs_lock.locked ? "Yes" : "No ", uni::vfs_lock.wait_queue.Count());
	#if _MCCA == 0x8632 && _GUI_ENABLE && _GUI_FREETYPE
	com1.OutFormat("%p  %s     %[u]       ft_lock\n\r", &ft_lock, ft_lock.locked ? "Yes" : "No ", ft_lock.wait_queue.Count());
	#endif
	com1.OutFormat("\n\r=== Per-Process Locks ===\n\r");
	com1.OutFormat("PID  Process Name        VMA Lock (Spinlock)    Sys Lock (Mutex)\n\r");
	com1.OutFormat("                         Addr      Locked HCPUID Addr      Locked Waiters\n\r");

	// Never block here: dump_lock() is also used by spinlock timeout diagnostics.
	bool old_if = false;
	if (!scheduler_lock.TryAcquire(old_if)) {
		com1.OutFormat("(skip process lock walk: scheduler_lock busy on CPU %[i])\n\r", (stdsint)scheduler_lock.cpu_id);
		return;
	}
	for (auto nod = Taskman::chain.Root(); nod; nod = nod->next) {
		auto pb = cast<ProcessBlock*>(nod->offs);
		com1.OutFormat("%[u]    ", pb->pid);
		// Align name
		stduint name_len = 0;
		if (pb->main_thread && pb->main_thread->name) {
			com1.OutFormat("%s", pb->main_thread->name.get());
			while (pb->main_thread->name.get()[name_len]) name_len++;
		} else {
			com1.OutFormat("(unknown)");
			name_len = 9;
		}
		for (stduint space = name_len; space < 20; space++) {
			com1.OutFormat(" ");
		}
		auto& fileman_mutex = pb->fileman.raw_mutex();
		com1.OutFormat("%p  %s    %[i]      %p  %s    %[u]\n\r",
			&pb->vma_lock, pb->vma_lock.locked ? "Yes" : "No ", (stdsint)pb->vma_lock.cpu_id,
			&fileman_mutex, fileman_mutex.locked ? "Yes" : "No ", fileman_mutex.wait_queue.Count());
	}
	scheduler_lock.Release(old_if);
}



#if _GUI_ENABLE
static const char* find_video_driver_name_recursive(const DeviceNode* root, void* driver_data) {
	if (!root) return nullptr;
	for (auto crt = root; crt; crt = cast<DeviceNode*>(crt->link.next)) {
		if (crt->fields.binding.driver_data == driver_data && crt->fields.binding.driver_name) {
			return crt->fields.binding.driver_name;
		}
		if (crt->link.subf) {
			auto res = find_video_driver_name_recursive(cast<DeviceNode*>(crt->link.subf), driver_data);
			if (res) return res;
		}
	}
	return nullptr;
}
#endif

void dump_screens_and_forms(OstreamTrait& com1) {
	com1.OutFormat("=== Display & Screen Devices ===\n\r");
	#if _GUI_ENABLE && ((_MCCA & 0xFF00) == 0x8600)
	if (Consman::current_video_device) {
		auto fb = Consman::current_video_device->GetFramebuffer();
		const char* drv_name = find_video_driver_name_recursive(Devsman::Root(), Consman::current_video_device);
		if (!drv_name) drv_name = "classic-video-driver";

		const char* fmt_str = "Unknown";
		if (fb.format == PixelFormat::ARGB8888) fmt_str = "ARGB8888";
		else if (fb.format == PixelFormat::RGB565) fmt_str = "RGB565";
		else if (fb.format == PixelFormat::RGB888) fmt_str = "RGB888";
		else if (fb.format == PixelFormat::RGBA8888) fmt_str = "RGBA8888";

		com1.OutFormat("Screen 0: [Active] %[u]x%[u] (%[u] bpp, %s) | Driver: %s\n\r",
			(stduint)fb.screen_size.x, (stduint)fb.screen_size.y,
			(stduint)fb.bpp, fmt_str, drv_name);
		com1.OutFormat("          FB Physical Address: %p, Pitch: %[u] bytes, Size: %[u] bytes\n\r",
			(void*)fb.physical_range.address, (stduint)fb.pitch, (stduint)fb.physical_range.length);
	} else {
		com1.OutFormat("No active video device.\n\r");
	}

	com1.OutFormat("\n\r=== Window / Form Topology ===\n\r");
	com1.OutFormat("PID  FormID State        Z-Index  Position & Size       Title\n\r");

	extern Spinlock scheduler_lock;
	bool old_if = false;
	if (!scheduler_lock.TryAcquire(old_if)) {
		com1.OutFormat("(skip form walk: scheduler_lock busy on CPU %[i])\n\r", (stdsint)scheduler_lock.cpu_id);
		return;
	}

	constexpr stduint MAX_PROCS = 128;
	stduint pids[MAX_PROCS];
	stduint proc_count = 0;
	for (auto pnod = Taskman::chain.Root(); pnod && proc_count < MAX_PROCS; pnod = pnod->next) {
		auto p = cast<ProcessBlock*>(pnod->offs);
		pids[proc_count++] = p->pid;
	}
	scheduler_lock.Release(old_if);

	stduint total_forms = 0;
	struct FormItem {
		stduint pid;
		stduint form_id;
		::uni::Witch::Form* pfrm;
	};
	constexpr stduint MAX_DUMP_FORMS = 128;
	FormItem items[MAX_DUMP_FORMS];
	stduint item_count = 0;

	for (stduint i = 0; i < proc_count && item_count < MAX_DUMP_FORMS; i++) {
		ProcessBlock* pb = ProcessBlock::AcquireActiveByPID(pids[i]);
		if (!pb) continue;
		{
			auto pforms = pb->pforms.Lock();
			for (stduint j = 0; j < pforms->Count() && item_count < MAX_DUMP_FORMS; j++) {
				SheetTrait* st = (*pforms)[j];
				if (!st) continue;
				items[item_count++] = { pb->pid, j, static_cast<::uni::Witch::Form*>(st) };
			}
		}
		ProcessBlock::Release(pb);
	}

	for (stduint k = 0; k < item_count; k++) {
		auto& item = items[k];
		auto pfrm = item.pfrm;
		total_forms++;

		// Calculate Z-index in global_layman (from bottom to top)
		stdsint z_index = -1;
		bool is_top = false;
		{
			auto layman = global_layman.Lock();
			stduint cur_z = 0;
			for (auto nod = layman->subl; nod; nod = nod->left) {
				if (nod->offs == pfrm) {
					z_index = (stdsint)cur_z;
				}
				cur_z++;
			}
			for (auto nod = layman->subf ? layman->subf->next : nullptr; nod; nod = nod->next) {
				if (!nod->offs || nod->offs == global_desktop || nod->offs == Cursor::global_cursor) continue;
				auto nod_frm = static_cast<::uni::Witch::Form*>(nod->offs);
				if (!nod_frm->is_dock) {
					if (nod_frm == pfrm) {
						is_top = true;
					}
					break;
				}
			}
		}

		const char* state_str = "[NORMAL]   ";
		if (pfrm->is_dock) {
			state_str = "[DOCK]     ";
		} else if (pfrm->state == ::uni::Witch::FormState::Minimized) {
			state_str = "[MINIMIZED]";
		} else if (pfrm->state == ::uni::Witch::FormState::Maximized) {
			state_str = "[MAXIMIZED]";
		} else if (pfrm->state == ::uni::Witch::FormState::Hidden) {
			state_str = "[HIDDEN]   ";
		}

		com1.OutFormat("%[u]    %[u]      %s  ", item.pid, item.form_id, state_str);

		if (z_index >= 0) {
			if (is_top) {
				com1.OutFormat("Z=%[i] (Top) ", z_index);
			} else {
				com1.OutFormat("Z=%[i]       ", z_index);
			}
		} else {
			com1.OutFormat("-           ");
		}

		com1.OutFormat("(%[i], %[i]) %[u]x%[u] ",
			(stdsint)pfrm->sheet_area.x, (stdsint)pfrm->sheet_area.y,
			(stduint)pfrm->sheet_area.width, (stduint)pfrm->sheet_area.height);

		if (pfrm->Title.reference() && pfrm->Title.reference()[0] != '\0') {
			com1.OutFormat("%s\n\r", pfrm->Title.reference());
		} else if (pfrm->is_dock) {
			com1.OutFormat("(Dock)\n\r");
		} else {
			com1.OutFormat("(Titleless)\n\r");
		}
	}

	if (total_forms == 0) {
		com1.OutFormat("(no active forms in system)\n\r");
	}
	#else
	com1.OutFormat("GUI not enabled or non-x86 architecture.\n\r");
	#endif
}

void dump_device_tree(OstreamTrait& com1, bool verbose);
void dump_backends(OstreamTrait& com1, SysinfoDumpDevsType type);
void sysinfo_classic(OstreamTrait& com1, byte func)
{
	switch (func)
	{
	case 'f':// filesys
		com1.OutFormat("\n\r");
		com1.OutFormat("File Systems\n\r");
		dump_filesys(com1);
		com1.OutFormat("\n\r");
		Filesys::Tree(com1, false);
		break;

	case 'h': case 'H':// hardware
		com1.OutFormat("\n\r");
		if (func == 'H') {
			dump_backends(com1, SysinfoDumpDevsType::Audio);
			com1.OutFormat("\n\r");
			dump_backends(com1, SysinfoDumpDevsType::General);
			com1.OutFormat("\n\r");
		}
		dump_device_tree(com1, func != 'h');
		break;

	case 'l':// lock
		com1.OutFormat("\n\r");
		dump_lock(com1);
		break;
	case 'm':// mem
		com1.OutFormat("\n\r");
		mempool0.dump_available();
		break;
	case 's':// proc threads ready_queue
		com1.OutFormat("\n\r");
		void dump_processors(OstreamTrait & com1);
		dump_processors(com1);
		com1.OutFormat("\n\r");
		void dump_threads(OstreamTrait & com1);
		dump_threads(com1);
		com1.OutFormat("\n\r");
		void dump_ready_queue(OstreamTrait & com1);
		dump_ready_queue(com1);
		break;

	case 'w': case 'W':// window and display topology
		com1.OutFormat("\n\r");
		dump_screens_and_forms(com1);
		break;

	case '-':
		Coreman::Reboot();
		break;
	case '+':
		Coreman::Shutdown();
		break;

	default:
		break;
	}
}

