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

static rostr text_device_node_type(uint16 node_type) {
	switch (DeviceNodeType(node_type)) {
	case DeviceNodeType::SystemRoot: return "system-root";
	case DeviceNodeType::BusRoot:    return "bus-root";
	case DeviceNodeType::PCI_Root:   return "pci-root";
	case DeviceNodeType::PciBus:     return "pci-bus";
	case DeviceNodeType::PciDevice:  return "pci-dev";
	case DeviceNodeType::UsbBus:     return "usb-bus";
	case DeviceNodeType::UsbRootHub: return "usb-root-hub";
	case DeviceNodeType::UsbDevice:  return "usb-dev";
	case DeviceNodeType::UsbInterface: return "usb-if";
	case DeviceNodeType::PlatformDevice: return "platform-dev";
	case DeviceNodeType::SerioController: return "serio-ctl";
	case DeviceNodeType::SerioDevice: return "serio-dev";
	default: return "unknown";
	}
}

static rostr text_device_bus_type(uint16 bus_type) {
	switch (DeviceBusType(bus_type)) {
	case DeviceBusType::PCI: return "pci";
	case DeviceBusType::USB: return "usb";
	case DeviceBusType::Platform: return "platform";
	case DeviceBusType::I2C: return "i2c";
	case DeviceBusType::SPI: return "spi";
	case DeviceBusType::Serio: return "serio";
	case DeviceBusType::Virtio: return "virtio";
	default: return "none";
	}
}

static rostr text_driver_binding_state(uint32 state) {
	switch (DriverBindingState(state)) {
	case DriverBindingState::None: return "none";
	case DriverBindingState::Matched: return "matched";
	case DriverBindingState::Probed: return "probed";
	case DriverBindingState::Started: return "started";
	case DriverBindingState::Failed: return "failed";
	default: return "?";
	}
}

static rostr text_device_resource_type(uint16 type) {
	switch (DeviceResourceType(type)) {
	case DeviceResourceType::PciBarMmio:        return "BAR-MMIO";
	case DeviceResourceType::PciBarIo:          return "BAR-IO";
	case DeviceResourceType::IoPortRange:       return "IOPORT";
	case DeviceResourceType::IrqLine:           return "IRQ";
	case DeviceResourceType::PciBridgeBusRange: return "BUS-RANGE";
	case DeviceResourceType::UsbLocation:       return "USB-LOC";
	case DeviceResourceType::UsbEndpoint:       return "USB-EP";
	default: return "RES";
	}
}

static rostr text_usb_endpoint_transfer_type(stduint type) {
	switch (type) {
	case 0: return "control";
	case 1: return "iso";
	case 2: return "bulk";
	case 3: return "interrupt";
	default: return "?";
	}
}

static void dump_device_tree_indent(OstreamTrait& com1, stduint depth) {
	for0(i, depth) com1.OutFormat("  ");
}

static void dump_device_tree_resources(OstreamTrait& com1, const DeviceNode& node, stduint depth) {
	for0(i, node.fields.resource_count) {
		const auto& res = node.fields.resources[i];
		dump_device_tree_indent(com1, depth);
		switch (DeviceResourceType(res.type)) {
		case DeviceResourceType::PciBarMmio:
			com1.OutFormat("- %s[%[u]] base=%p flags=%04X\n\r",
				text_device_resource_type(res.type), res.index, (void*)res.start, (unsigned)res.flags);
			break;
		case DeviceResourceType::PciBarIo:
			com1.OutFormat("- %s[%[u]] base=%p\n\r",
				text_device_resource_type(res.type), res.index, (void*)res.start);
			break;
		case DeviceResourceType::IoPortRange:
			com1.OutFormat("- %s[%[u]] base=%p len=%p\n\r",
				text_device_resource_type(res.type), res.index, (void*)res.start, (void*)res.length);
			break;
		case DeviceResourceType::IrqLine:
			com1.OutFormat("- %s line=%[u] pin=%[u]\n\r",
				text_device_resource_type(res.type), (stduint)res.start, (stduint)res.extra);
			break;
		case DeviceResourceType::PciBridgeBusRange:
			com1.OutFormat("- %s primary=%[u] secondary=%[u] subordinate=%[u]\n\r",
				text_device_resource_type(res.type), (stduint)res.start, (stduint)res.length, (stduint)res.extra);
			break;
		case DeviceResourceType::UsbLocation:
			com1.OutFormat("- %s port=%[u] slot=%[u]\n\r",
				text_device_resource_type(res.type), (stduint)res.start, (stduint)res.extra);
			break;
		case DeviceResourceType::UsbEndpoint:
			com1.OutFormat("- %s[%[u]] addr=%[u] type=%s mps=%[u] interval=%[u]\n\r",
				text_device_resource_type(res.type), res.index,
				(stduint)res.start,
				text_usb_endpoint_transfer_type(res.extra & 0xFFu),
				(stduint)res.length,
				(stduint)((res.extra >> 8) & 0xFFu));
			break;
		default:
			com1.OutFormat("- %s[%[u]] start=%p len=%p extra=%p\n\r",
				text_device_resource_type(res.type), res.index, (void*)res.start, (void*)res.length, (void*)res.extra);
			break;
		}
	}
}

static void dump_device_tree_usb_strings(OstreamTrait& com1, const DeviceNode& node, stduint depth) {
	if (DeviceNodeType(node.fields.node_type) != DeviceNodeType::UsbDevice) return;
	if (node.fields.text_manufacturer) {
		dump_device_tree_indent(com1, depth);
		com1.OutFormat("- USB-STR manufacturer=\"%s\"\n\r", node.fields.text_manufacturer);
	}
	if (node.fields.text_product) {
		dump_device_tree_indent(com1, depth);
		com1.OutFormat("- USB-STR product=\"%s\"\n\r", node.fields.text_product);
	}
	if (node.fields.text_serial) {
		dump_device_tree_indent(com1, depth);
		com1.OutFormat("- USB-STR serial=\"%s\"\n\r", node.fields.text_serial);
	}
}

static void dump_device_tree_node(OstreamTrait& com1, const DeviceNode* node, stduint depth, bool verbose) {
	for (auto crt = node; crt; crt = cast<DeviceNode*>(crt->link.next)) {
		dump_device_tree_indent(com1, depth);
		const rostr name = crt->link.addr ? crt->link.addr : "(unnamed)";
		const rostr driver_name = crt->fields.binding.driver_name ? crt->fields.binding.driver_name : nullptr;
		const bool has_binding = driver_name && crt->fields.binding.state != static_cast<uint32>(DriverBindingState::None);
		switch (DeviceNodeType(crt->fields.node_type)) {
		case DeviceNodeType::BusRoot:
		case DeviceNodeType::PCI_Root:
			com1.OutFormat("%s <%s bus=%s%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				text_device_bus_type(crt->fields.bus_type),
				driver_name ? " drv=" : "",
				driver_name ? driver_name : "",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		case DeviceNodeType::PciBus:
			com1.OutFormat("%s <%s seg=%[u] bus=%[u]%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				(stduint)crt->fields.pci_segment, (stduint)crt->fields.pci_bus,
				driver_name ? " drv=" : "",
				driver_name ? driver_name : "",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		case DeviceNodeType::PciDevice:
			com1.OutFormat("%s <%s %02X.%02X.%02X vend=%04X dev=%04X%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				(unsigned)crt->fields.class_base, (unsigned)crt->fields.class_sub, (unsigned)crt->fields.class_if,
				(unsigned)crt->fields.vendor_id, (unsigned)crt->fields.device_id,
				driver_name ? " drv=" : "",
				driver_name ? driver_name : "",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		case DeviceNodeType::UsbDevice:
			com1.OutFormat("%s <%s %02X.%02X.%02X vid=%04X pid=%04X bus=%s%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				(unsigned)crt->fields.class_base, (unsigned)crt->fields.class_sub, (unsigned)crt->fields.class_if,
				(unsigned)crt->fields.vendor_id, (unsigned)crt->fields.device_id,
				text_device_bus_type(crt->fields.bus_type),
				driver_name ? " drv=" : "",
				driver_name ? driver_name : "",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		case DeviceNodeType::UsbRootHub:
			com1.OutFormat("%s <%s %02X.%02X.%02X bus=%s%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				(unsigned)crt->fields.class_base, (unsigned)crt->fields.class_sub, (unsigned)crt->fields.class_if,
				text_device_bus_type(crt->fields.bus_type),
				driver_name ? " drv=" : "",
				driver_name ? driver_name : "",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		case DeviceNodeType::UsbInterface:
			com1.OutFormat("%s <%s %02X.%02X.%02X bus=%s%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				(unsigned)crt->fields.class_base, (unsigned)crt->fields.class_sub, (unsigned)crt->fields.class_if,
				text_device_bus_type(crt->fields.bus_type),
				driver_name ? " drv=" : "",
				driver_name ? driver_name : "",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		case DeviceNodeType::UsbBus:
		case DeviceNodeType::PlatformDevice:
		case DeviceNodeType::SerioController:
		case DeviceNodeType::SerioDevice:
			com1.OutFormat("%s <%s bus=%s%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				text_device_bus_type(crt->fields.bus_type),
				driver_name ? " drv=" : "",
				driver_name ? driver_name : "",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		default:
			com1.OutFormat("%s <%s%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				driver_name ? " drv=" : "",
				driver_name ? driver_name : "",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		}
		if (verbose && crt->fields.resource_count) {
			dump_device_tree_resources(com1, *crt, depth + 1);
		}
		if (verbose) {
			dump_device_tree_usb_strings(com1, *crt, depth + 1);
		}
		if (crt->link.subf) {
			dump_device_tree_node(com1, cast<DeviceNode*>(crt->link.subf), depth + 1, verbose);
		}
	}
}

void dump_device_tree(OstreamTrait& com1, bool verbose) {
	com1.OutFormat("=== Device Tree ===\n\r");
	auto root = Devsman::Root();
	if (!root) {
		com1.OutFormat("(empty)\n\r");
		return;
	}
	dump_device_tree_node(com1, root, 0, verbose);
}

void dump_device_tree(OstreamTrait& com1) {
	dump_device_tree(com1, true);
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
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_Lock)) reason_name = "Lock";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_Exiting)) reason_name = "Exit";
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

namespace uni {
	extern Spinlock vfs_lock;
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
	com1.OutFormat("%p  %s     %[i]      vfs_lock\n\r", &uni::vfs_lock, uni::vfs_lock.locked ? "Yes" : "No ", (stdsint)uni::vfs_lock.cpu_id);

	com1.OutFormat("\n\r=== Global Mutexes ===\n\r");
	com1.OutFormat("Address             Locked  Waiters Name\n\r");
	com1.OutFormat("%p  %s     %[u]       outc_mutex\n\r", &outc_mutex, outc_mutex.locked ? "Yes" : "No ", outc_mutex.wait_queue.Count());
	#if _MCCA == 0x8632 && _GUI_ENABLE && _GUI_FREETYPE
	com1.OutFormat("%p  %s     %[u]       ft_lock\n\r", &ft_lock, ft_lock.locked ? "Yes" : "No ", ft_lock.wait_queue.Count());
	#endif
	com1.OutFormat("\n\r=== Per-Process Locks ===\n\r");
	com1.OutFormat("PID  Process Name        VMA Lock (Spinlock)    Sys Lock (Mutex)\n\r");
	com1.OutFormat("                         Addr      Locked HCPUID Addr      Locked Waiters\n\r");

	// Safely acquire scheduler_lock to walk the process chain Taskman::chain
	bool old_if = scheduler_lock.Acquire();
	for (auto nod = Taskman::chain.Root(); nod; nod = nod->next) {
		auto pb = cast<ProcessBlock*>(nod->offs);
		com1.OutFormat("%[u]    ", pb->pid);
		// Align name
		stduint name_len = 0;
		if (pb->main_thread && pb->main_thread->name) {
			com1.OutFormat("%s", pb->main_thread->name);
			while (pb->main_thread->name[name_len]) name_len++;
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

//// ---- POWER MANAGER ---- ////

