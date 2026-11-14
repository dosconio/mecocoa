// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: xHCI Bring-up
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#if (_MCCA & 0xFF00) == 0x8600 && CONFIG_ENABLE_USB && CONFIG_ENABLE_USB_XHCI
#include <cpp/Device/Bus/PCI.hpp>
#include <cpp/Device/USB/xHCI/xHCI.hpp>
#include <cpp/interrupt>
#include <c/proctrl/IAx86_64.msr.h>

void hand_kboard(keyboard_event_t event);

#if _OPT_BUS_USB_xHCI_MultiInterrupter
extern "C" void Handint_XHCI1_Entry();
extern "C" void Handint_XHCI2_Entry();
extern "C" void Handint_XHCI3_Entry();
#endif
extern "C" void register_interrupt_vector_handler(stduint irq_id,
	void (*handler)(stduint));

namespace {
	uni::PCI pci;
	constexpr uint32 XHCIEventHandle = 0x58484349u;
	constexpr uint32 XHCIEventSource = IRQ_xHCI;
	constexpr uint8 XHCIInterruptVectors[] = { IRQ_xHCI, 0x42, 0x43, 0x44 };
	constexpr uint16 XHCIMaxConfiguredInterrupters =
		_OPT_BUS_USB_xHCI_MultiInterrupter
		? static_cast<uint16>(numsof(XHCIInterruptVectors)) : 1;
	constexpr stduint XHCIEventWatchdogTicks = CONFIG_SysTickFreq >= 10 ?
		CONFIG_SysTickFreq / 10 : 1;
	constexpr uint8 XHCIEventStallPolls = 10;
	constexpr stduint XHCIPolicyTicks = 1;
	// USB 2.0 Specification, 7.1.7.3: debounce a new connection for at least 100 ms.
	constexpr uint16 XHCIRootPortDebounceTicks = CONFIG_SysTickFreq >= 10 ?
		(CONFIG_SysTickFreq + 9) / 10 : 1;
	uni::device::SpaceUSB3::HostControllerResourceConfig XHCIResources(
		uint16 interrupter_count) {
		uni::device::SpaceUSB3::HostControllerResourceConfig resources{};
		resources.max_interrupters = interrupter_count;
		resources.event_ring_segments = interrupter_count > 1 ? 2 : 1;
		resources.root_port_debounce_ticks = XHCIRootPortDebounceTicks;
		return resources;
	}

	struct XHCIControllerInstance {
		XHCIControllerInstance(stduint mmio_base, uint8 bus, uint8 device,
			uint8 function, uint16 interrupter_count)
			: controller{ mmio_base, XHCIResources(interrupter_count) },
			mmio_base{ mmio_base }, bus{ bus },
			device{ device }, function{ function } {}

		uni::device::SpaceUSB3::HostController controller;
		stduint mmio_base;
		uint8 bus;
		uint8 device;
		uint8 function;
		volatile stduint acknowledgement_count = 0;
		uint8 watchdog_pending_polls = 0;
		bool running = false;
		bool watchdog_reported = false;
		bool policy_error_reported = false;
		XHCIControllerInstance* next = nullptr;
	};
	XHCIControllerInstance* xhci_controllers = nullptr;
	Mutex xhci_dispatch_lock;
	bool xhci_event_watchdog_armed = false;
	bool xhci_policy_tick_armed = false;
	uint32 xhci_event_generation = 0;
}

static void release_xhci_interrupts(const DeviceNode* node, uint16* handles) {
	#if _MCCA == 0x8664
	if (!handles[0]) return;
	if (!Iova::DisablePciMessageInterrupts(node)) return;
	for0(index, XHCIMaxConfiguredInterrupters) {
		Iova::ReleaseInterrupt(handles[index]);
		handles[index] = 0;
	}
	#else
	(void)node;
	(void)handles;
	#endif
}

static uint16 configure_xhci_interrupts(uni::PCI::Device& xhc_dev, DeviceNode* node,
	uint32 bsp_local_apic_id, uint8 irq_line, uint8 irq_pin,
	uint16 controller_limit, uint16* handles) {
	using namespace uni;
	#if _MCCA == 0x8664
	const bool extended = Iova::ExtendedInterruptMode();
	#else
	constexpr bool extended = false;
	#endif
	if (bsp_local_apic_id == CORE_ID_INVALID || (!extended && bsp_local_apic_id >= 0xFFu)) {
		plogerro("xHCI interrupt target LAPIC ID=%u requires interrupt remapping",
			(unsigned)bsp_local_apic_id);
		return 0;
	}
	#if _MCCA == 0x8664
	if (extended && !Iova::PreparePciMessageInterrupts(node)) return 0;
	#endif
	#if _OPT_BUS_USB_xHCI_MultiInterrupter
	const uint16 table_size = pci.MSIXTableSize(xhc_dev);
	if (table_size != 0 && controller_limit != 0) {
		uint16 message_count = table_size < XHCIMaxConfiguredInterrupters
			? table_size : XHCIMaxConfiguredInterrupters;
		if (message_count > controller_limit) message_count = controller_limit;
		PCI::MSIMessage messages[XHCIMaxConfiguredInterrupters]{};
		uint32 targets[XHCIMaxConfiguredInterrupters]{};
		uint16 target_count = 0;
		for (stduint core = 0; core < PCU_CORES_MAX &&
			target_count < XHCIMaxConfiguredInterrupters; ++core) {
			auto* percore = Taskman::PCU_CORES_PERCORE[core];
			if (percore == nullptr || percore->state != CoreState::Online ||
				percore->lapic_id == CORE_ID_INVALID || (!extended && percore->lapic_id >= 0xFFu)) continue;
			targets[target_count++] = percore->lapic_id;
		}
		if (target_count == 0)
			targets[target_count++] = bsp_local_apic_id;
		if (target_count > 1) {
			if (message_count > target_count) message_count = target_count;
			uint16 prepared = 0;
			for (; prepared < message_count; ++prepared) {
				if (extended) {
					Iova::InterruptMessage message{};
					if (!Iova::AllocatePciInterrupt(node, targets[prepared],
						XHCIInterruptVectors[prepared], message)) break;
					handles[prepared] = message.handle;
					messages[prepared] = {message.address, message.data};
				}
				else {
					messages[prepared].address = 0xfee00000u | (targets[prepared] << 12);
					messages[prepared].data = XHCIInterruptVectors[prepared];
				}
			}
			if (prepared == message_count) {
				const auto msix_result = pci.ConfigureMSIX(xhc_dev, messages, message_count);
				if (!msix_result) {
					ploginfo("xHCI MSI-X enabled vectors=%u cores=%u",
						(unsigned)message_count, (unsigned)target_count);
					return message_count;
				}
				plogwarn("xHCI MSI-X setup failed: %s; falling back to MSI", msix_result.Name());
			}
			release_xhci_interrupts(node, handles);
			if (handles[0]) return 0;
		}
		else {
			ploginfo("xHCI MSI-X skipped: one online core; using MSI");
		}
	}
	#else
	(void)controller_limit;
	#endif

	ploginfo("xHC legacy IRQ line=%u pin=%u; configuring MSI",
		(unsigned)irq_line, (unsigned)irq_pin);
	Iova::InterruptMessage message{};
	if (extended && !Iova::AllocatePciInterrupt(node, bsp_local_apic_id, IRQ_xHCI, message)) return 0;
	handles[0] = message.handle;
	const auto msi_result = extended ?
		pci.configure_MSI(xhc_dev, uint32(message.address), message.data, 0) :
		pci.configure_MSI_fixed_destination(xhc_dev, static_cast<uint8>(bsp_local_apic_id),
		PCI::MSITriggerMode::Edge,
		PCI::MSIDeliveryMode::Fixed,
		IRQ_xHCI, 0);
	if (msi_result) {
		release_xhci_interrupts(node, handles);
		plogerro("xHCI MSI setup failed: %s", msi_result.Name());
		return 0;
	}
	ploginfo("xHCI MSI enabled vector=%u lapic=%u",
		(unsigned)IRQ_xHCI, (unsigned)bsp_local_apic_id);
	return 1;
}

//{TEMP} version
static bool initialize_xhci_controller(uni::PCI::Device& xhc_dev, uint64 mmio_base,
	XHCIControllerInstance& instance) {
	using namespace uni;
	auto* xhc = &instance.controller;
	if (!mmio_base) return false;
	pci.enable_MMIO(xhc_dev);
	pci.ConvertFromEhci(xhc_dev);
	if (auto err = xhc->Initialize()) {
		plogerro("xhc.Initialize failed: %s at %s:%d", err.Name(), err.File(), err.Line());
		return false;
	}
	//
	if (auto err = xhc->Run()) {
		plogerro("xhc.Run failed: %s at %s:%d", err.Name(), err.File(), err.Line());
		return false;
	}
	instance.running = true;
	for1(i, xhc->MaxPorts()) {
		auto port = xhc->PortAt(i);
		// ploginfo("Port %d: IsConnected=%d", i, port.IsConnected());
		if (!port.IsConnected()) continue;
		if (auto err = xhc->ConfigurePort(port)) {
			plogerro("Failed to configure port: %s at %s:%d", err.Name(), err.File(), err.Line());
		}
	}
	//
	return true;
}

static bool acknowledge_xhci_instance(XHCIControllerInstance& instance,
	int interrupter_index = -1) {
	if (!instance.running) return false;
	using namespace uni::device::SpaceUSB3;
	auto* capability = reinterpret_cast<CapabilityRegisters*>(instance.mmio_base);
	auto* operational = reinterpret_cast<OperationalRegisters*>(
		instance.mmio_base + capability->CAPLENGTH.Read());
	const auto status = operational->USBSTS.Read();
	bool acknowledged = false;
	const uint16 first = interrupter_index < 0 ? 0 : uint16(interrupter_index);
	const uint16 end = interrupter_index < 0
		? instance.controller.InterrupterCount() : uint16(first + 1u);
	for (uint16 index = first; index < end; ++index) {
		if (instance.controller.AcknowledgeInterrupter(index)) acknowledged = true;
	}
	if (!status.bits.event_interrupt && !status.bits.port_change_detect && !acknowledged) return false;
	__atomic_add_fetch(&instance.acknowledgement_count, 1, __ATOMIC_RELAXED);
	if (status.bits.event_interrupt || status.bits.port_change_detect) {
		USBSTS_t clear = {};
		clear.bits.event_interrupt = status.bits.event_interrupt;
		clear.bits.port_change_detect = status.bits.port_change_detect;
		operational->USBSTS.Write(clear);
	}
	return true;
}

static void signal_pending_xhci_events() {
	if (!xhci_event_generation) return;
	bool pending = false;
	for (auto* instance = xhci_controllers; instance; instance = instance->next) {
		if (!instance->running || !instance->controller.HasPendingEvent()) continue;
		pending = true;
	}
	if (!pending) return;
	(void)Devsman::AcknowledgeXHCIInterrupt();
	(void)device_interrupt_retry(IRQ_xHCI);
}

static void poll_xhci_policy(pureptr_t, stduint) {
	MutexLocal guard(&xhci_dispatch_lock);
	xhci_policy_tick_armed = false;
	for (auto* instance = xhci_controllers; instance; instance = instance->next) {
		if (!instance->running) continue;
		if (auto err = instance->controller.ProcessDelayed()) {
			if (!instance->policy_error_reported) {
				plogwarn("xHCI PCI %[8H].%[8H].%[8H] delayed policy failed: %s at %s:%d",
					instance->bus, instance->device, instance->function,
					err.Name(), err.File(), err.Line());
				instance->policy_error_reported = true;
			}
		}
		else {
			instance->policy_error_reported = false;
		}
	}
	xhci_policy_tick_armed = Systimex::AppendDeferredCallback(
		XHCIPolicyTicks, 0, (_tocall_ft)poll_xhci_policy);
	if (!xhci_policy_tick_armed) {
		plogerro("xHCI cannot re-arm delayed policy tick");
	}
}

static bool arm_xhci_policy_tick() {
	if (xhci_policy_tick_armed) return true;
	xhci_policy_tick_armed = Systimex::AppendDeferredCallback(
		XHCIPolicyTicks, 0, (_tocall_ft)poll_xhci_policy);
	if (!xhci_policy_tick_armed) {
		plogerro("xHCI cannot arm delayed policy tick");
	}
	return xhci_policy_tick_armed;
}

static void poll_xhci_events(pureptr_t, stduint) {
	MutexLocal guard(&xhci_dispatch_lock);
	xhci_event_watchdog_armed = false;
	bool pending = false;
	for (auto* instance = xhci_controllers; instance; instance = instance->next) {
		if (!instance->running) continue;
		if (!instance->controller.HasPendingEvent()) {
			instance->watchdog_pending_polls = 0;
			instance->watchdog_reported = false;
			continue;
		}
		pending = true;
		if (instance->watchdog_pending_polls != 0xffu) {
			instance->watchdog_pending_polls++;
		}
		auto& xhc = instance->controller;
		if (instance->watchdog_pending_polls >= XHCIEventStallPolls &&
			!instance->watchdog_reported) {
			using namespace uni::device::SpaceUSB3;
			auto* capability = reinterpret_cast<CapabilityRegisters*>(instance->mmio_base);
			auto* operational = reinterpret_cast<OperationalRegisters*>(
				instance->mmio_base + capability->CAPLENGTH.Read());
			uint16 pending_interrupter = 0;
			while (pending_interrupter < xhc.InterrupterCount() &&
				!xhc.HasPendingEvent(pending_interrupter)) ++pending_interrupter;
			auto* interrupter = reinterpret_cast<InterrupterRegisterSet*>(
				instance->mmio_base + capability->RTSOFF.Read().Offset() + 0x20u) +
				pending_interrupter;
			plogwarn("xHCI watchdog PCI %[8H].%[8H].%[8H]: intr=%u ack=%u usbsts=%[32H] iman=%[32H]",
				instance->bus, instance->device, instance->function,
				pending_interrupter, instance->acknowledgement_count,
				operational->USBSTS.Read().data[0],
				interrupter->IMAN.Read().data[0]);
			instance->watchdog_reported = true;
		}
	}
	if (pending) {
		(void)Devsman::AcknowledgeXHCIInterrupt();
		(void)device_interrupt_retry(IRQ_xHCI);
	}
	(void)arm_xhci_policy_tick();
	xhci_event_watchdog_armed = Systimex::AppendDeferredCallback(
		XHCIEventWatchdogTicks, 0, (_tocall_ft)poll_xhci_events);
	if (!xhci_event_watchdog_armed) {
		plogerro("xHCI cannot re-arm event watchdog");
	}
}

static bool arm_xhci_event_watchdog() {
	if (xhci_event_watchdog_armed) return true;
	xhci_event_watchdog_armed = Systimex::AppendDeferredCallback(
		XHCIEventWatchdogTicks, 0, (_tocall_ft)poll_xhci_events);
	if (!xhci_event_watchdog_armed) {
		plogerro("xHCI cannot arm event watchdog");
	}
	return xhci_event_watchdog_armed;
}

static bool start_xhci_driver(DeviceNode* xhc_node) {
	if (!xhc_node) return false;
	auto* mmio = Devsman::FindResource(xhc_node, DeviceResourceType::PciBarMmio, 0);
	if (!mmio || !mmio->start ||
		mmio->length < sizeof(uni::device::SpaceUSB3::CapabilityRegisters) ||
		mmio->start > uint64(~stduint(0)) ||
		mmio->length > uint64(~stduint(0)) - mmio->start) {
		plogwarn("xHCI has an invalid or unaddressable BAR0 MMIO resource");
		return false;
	}
	// MSI/MSI-X needs a local APIC; do not access APIC MSRs on a PIC-only target.
	if (IC.getType() == 0) {
		plogwarn("xHCI requires APIC interrupt delivery");
		return false;
	}
	uni::PCI::Device xhc_tree_dev{};
	xhc_tree_dev.segment_group = xhc_node->fields.pci_segment;
	xhc_tree_dev.bus = xhc_node->fields.pci_bus;
	xhc_tree_dev.device = xhc_node->fields.pci_device;
	xhc_tree_dev.function = xhc_node->fields.pci_function;
	xhc_tree_dev.header_type = pci.read_header_type(xhc_tree_dev);
	xhc_tree_dev.class_code.base = xhc_node->fields.class_base;
	xhc_tree_dev.class_code.sub = xhc_node->fields.class_sub;
	xhc_tree_dev.class_code.interface = xhc_node->fields.class_if;
	uint8 irq_line = 0xFF;
	uint8 irq_pin = 0;
	if (auto* irq = Devsman::FindResource(xhc_node, DeviceResourceType::IrqLine)) {
		irq_line = uint8(irq->start);
		irq_pin = uint8(irq->extra);
	}
	pci.enable_MMIO(xhc_tree_dev);
	const bool x2mode = (getMSR(x86MSR::APIC_BASE) & (1ULL << 10)) != 0;
	PortAdapter lapic;
	lapic.typ = x2mode ? 2 : 1;
	const uint32 apic_id_value = lapic.ReadLAPIC(0x20);
	const uint32 bsp_local_apic_id = x2mode ? apic_id_value : apic_id_value >> 24;
	uint16 handles[XHCIMaxConfiguredInterrupters]{};
	const uint16 interrupter_count = configure_xhci_interrupts(
		xhc_tree_dev, xhc_node, bsp_local_apic_id, irq_line, irq_pin,
		reinterpret_cast<uni::device::SpaceUSB3::CapabilityRegisters*>(
			stduint(mmio->start))->HCSPARAMS1.Read().bits.max_interrupters, handles);
	if (interrupter_count == 0) return false;
	void* instance_storage = uni_hostenv_allocator->allocate(
		sizeof(XHCIControllerInstance), 6);
	if (!instance_storage) {
		release_xhci_interrupts(xhc_node, handles);
		plogerro("xHCI cannot allocate controller instance");
		return false;
	}
	auto* instance = new (instance_storage) XHCIControllerInstance{
		stduint(mmio->start), xhc_node->fields.pci_bus,
		xhc_node->fields.pci_device, xhc_node->fields.pci_function,
		interrupter_count };
	auto* xhc = &instance->controller;
	instance->next = xhci_controllers;
	xhci_controllers = instance;
	if (!initialize_xhci_controller(xhc_tree_dev, mmio->start, *instance)) {
		release_xhci_interrupts(xhc_node, handles);
		xhci_controllers = instance->next;
		instance->~XHCIControllerInstance();
		uni_hostenv_allocator->deallocate(instance);
		return false;
	}
	signal_pending_xhci_events();
	(void)arm_xhci_event_watchdog();
	(void)arm_xhci_policy_tick();
	xhc_node->fields.binding.driver_data = xhc;
	auto usb_bus_name = String::newFormat("usb-bus@%04x:%02x:%02x.%x",
		(stduint)xhc_node->fields.pci_segment,
		(stduint)xhc_node->fields.pci_bus,
		(stduint)xhc_node->fields.pci_device,
		(stduint)xhc_node->fields.pci_function);
	auto* usb_bus_node = Devsman::RegisterUSBBus(usb_bus_name.reference(), "xhci", xhc);
	Devsman::RegisterUSBRootHub(usb_bus_node, "usb-root-hub@0", 0x09u, 0x00u, 0x03u, "usb-root-hub", xhc);
	ploginfo("xHCI controller started at PCI %[16H]:%[8H]:%[8H].%[8H] interrupters=%u",
		(stduint)xhc_tree_dev.segment_group, (stduint)xhc_tree_dev.bus,
		(stduint)xhc_tree_dev.device, (stduint)xhc_tree_dev.function,
		xhc->InterrupterCount());
	return true;
}

bool Devsman::BindXHCIEventOwner(stduint owner_pid, stduint owner_tid) {
	if (!device_interrupt_bind(owner_pid, owner_tid, XHCIEventHandle,
		XHCIEventSource, IRQ_xHCI, &xhci_event_generation)) return false;
	if (!RegisterDriverStarter("xhci", start_xhci_driver)) return false;
	StartKnownDrivers();
	return true;
}

bool Devsman::AcknowledgeXHCIInterrupt() {
	bool acknowledged = false;
	for (auto* instance = xhci_controllers; instance; instance = instance->next) {
		if (acknowledge_xhci_instance(*instance)) acknowledged = true;
	}
	return acknowledged;
}

static bool AcknowledgeXHCIInterrupter(uint16 interrupter_index) {
	bool acknowledged = false;
	for (auto* instance = xhci_controllers; instance; instance = instance->next) {
		if (interrupter_index >= instance->controller.InterrupterCount()) continue;
		if (acknowledge_xhci_instance(*instance, interrupter_index)) acknowledged = true;
	}
	return acknowledged;
}

bool Devsman::ProcessXHCIEvent(const DeviceEvent& event) {
	if (DeviceEventKind(event.kind) != DeviceEventKind::Interrupt ||
		event.device_handle != XHCIEventHandle || event.source != XHCIEventSource ||
		event.generation != xhci_event_generation) return false;
	MutexLocal guard(&xhci_dispatch_lock);
	for (auto* instance = xhci_controllers; instance; instance = instance->next) {
		if (!instance->running) continue;
		if (auto err = instance->controller.ProcessEvents()) {
			plogerro("xHCI PCI %[8H].%[8H].%[8H] ProcessEvents failed: %s at %s:%d",
				instance->bus, instance->device, instance->function,
				err.Name(), err.File(), err.Line());
		}
	}
	auto* current = Taskman::CurrentTB();
	if (!current || !current->parent_process ||
		!device_interrupt_ack(current->parent_process->pid, current->tid, event)) {
		plogwarn("[USB] xHCI interrupt ACK failed sequence=%llu", event.sequence);
	}
	return true;
}

static stduint XHCIInterruptEntry(uint16 index) {
	switch (index) {
	case 0: return mglb(Handint_XHCI_Entry);
	#if _OPT_BUS_USB_xHCI_MultiInterrupter
	case 1: return mglb(Handint_XHCI1_Entry);
	case 2: return mglb(Handint_XHCI2_Entry);
	case 3: return mglb(Handint_XHCI3_Entry);
	#endif
	default: return 0;
	}
}

static int XHCIInterrupterForVector(stduint vector) {
	for (uint16 index = 0; index < XHCIMaxConfiguredInterrupters; ++index) {
		if (XHCIInterruptVectors[index] == vector) return index;
	}
	return -1;
}

static void HandleXHCIInterruptVector(stduint vector) {
	const int interrupter = XHCIInterrupterForVector(vector);
	if (interrupter >= 0 && AcknowledgeXHCIInterrupter(uint16(interrupter))) {
		device_interrupt_proc(IRQ_xHCI);
	}
	IC.SendEOI(vector);
}

_ESYM_C void R_XHCI_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_XHCI{
	.init = R_XHCI_INIT,
	.name = "xHCI",
};

void R_XHCI_INIT() {
	if (!PCI_Init(pci)) {
		plogwarn("No devices on PCI or PCI init failed.");
	}
	for (uint16 index = 0; index < XHCIMaxConfiguredInterrupters; ++index) {
		const uint8 vector = XHCIInterruptVectors[index];
		#if _MCCA == 0x8664
		IC[vector].setModeRupt(XHCIInterruptEntry(index), SegCo64);
		#else
		IC[vector].setRange(XHCIInterruptEntry(index), SegCo32);
		#endif
		register_interrupt_vector_handler(vector, HandleXHCIInterruptVector);
	}
	#if _GUI_ENABLE
	uni::device::SpaceUSB::HIDMouseDriver::default_observer = hand_mouse_usb;
	#endif
	uni::device::SpaceUSB::HIDKeyboardDriver::default_observer = hand_kboard;
}
#endif
