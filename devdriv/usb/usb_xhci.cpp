// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: xHCI Bring-up
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#if _MCCA == 0x8664 && defined(_UEFI)
#include <cpp/Device/Bus/PCI.hpp>
#include <cpp/Device/USB/xHCI/xHCI.hpp>
#include <cpp/interrupt>
#include <c/proctrl/IAx86_64.msr.h>

namespace {
	uni::PCI pci;
	constexpr uint32 XHCIEventHandle = 0x58484349u;
	constexpr uint32 XHCIEventSource = IRQ_xHCI;
	constexpr stduint XHCIEventWatchdogTicks = CONFIG_SysTickFreq >= 10 ?
		CONFIG_SysTickFreq / 10 : 1;
	constexpr stduint XHCIPolicyTicks = 1;
	struct XHCIControllerInstance {
		XHCIControllerInstance(stduint mmio_base, uint8 bus, uint8 device, uint8 function)
			: controller{ mmio_base }, mmio_base{ mmio_base }, bus{ bus },
			device{ device }, function{ function } {}

		uni::device::SpaceUSB3::HostController controller;
		stduint mmio_base;
		uint8 bus;
		uint8 device;
		uint8 function;
		volatile stduint acknowledgement_count = 0;
		bool running = false;
		bool watchdog_reported = false;
		bool policy_error_reported = false;
		XHCIControllerInstance* next = nullptr;
	};
	XHCIControllerInstance* xhci_controllers = nullptr;
	bool xhci_event_watchdog_armed = false;
	bool xhci_policy_tick_armed = false;
	uint32 xhci_event_generation = 0;
}

//{TEMP} version
static bool initialize_xhci_controller(uni::PCI::Device& xhc_dev, uint64 mmio_base,
	uint8 irq_line, uint8 irq_pin, XHCIControllerInstance& instance) {
	using namespace uni;
	auto* xhc = &instance.controller;
	if (!mmio_base) return false;
	pci.enable_MMIO(xhc_dev);
	ploginfo("xHC legacy IRQ line=%u pin=%u; configuring MSI",
		(unsigned)irq_line, (unsigned)irq_pin);
	// config MSI
	const bool x2mode = (getMSR(x86MSR::APIC_BASE) & (1ULL << 10)) != 0;
	PortAdapter lapic;
	lapic.typ = x2mode ? 2 : 1;
	const uint32 apic_id_value = lapic.ReadLAPIC(0x20);
	const uint8 bsp_local_apic_id = x2mode
		? uint8(apic_id_value) : uint8(apic_id_value >> 24); // or STI is useless -- Phina 20260117
	const auto msi_result = pci.configure_MSI_fixed_destination(
		xhc_dev, bsp_local_apic_id,
		PCI::MSITriggerMode::Edge,
		PCI::MSIDeliveryMode::Fixed,
		IRQ_xHCI, 0);
	if (msi_result) {
		plogerro("xHCI MSI setup failed: %s", msi_result.Name());
		return false;
	}
	ploginfo("xHCI MSI enabled vector=%u lapic=%u",
		(unsigned)IRQ_xHCI, (unsigned)bsp_local_apic_id);
	//
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

static bool acknowledge_xhci_instance(XHCIControllerInstance& instance) {
	if (!instance.running) return false;
	using namespace uni::device::SpaceUSB3;
	auto* capability = reinterpret_cast<CapabilityRegisters*>(instance.mmio_base);
	auto* operational = reinterpret_cast<OperationalRegisters*>(
		instance.mmio_base + capability->CAPLENGTH.Read());
	auto* interrupter = reinterpret_cast<InterrupterRegisterSet*>(
		instance.mmio_base + capability->RTSOFF.Read().Offset() + 0x20u);
	const auto status = operational->USBSTS.Read();
	const auto iman = interrupter->IMAN.Read();
	if (!status.bits.event_interrupt && !iman.bits.interrupt_pending) return false;
	++instance.acknowledgement_count;
	if (iman.bits.interrupt_pending) {
		IMAN_t clear = {};
		clear.bits.interrupt_pending = true;
		clear.bits.interrupt_enable = iman.bits.interrupt_enable;
		interrupter->IMAN.Write(clear);
	}
	if (status.bits.event_interrupt) {
		USBSTS_t clear = {};
		clear.bits.event_interrupt = true;
		operational->USBSTS.Write(clear);
	}
	return true;
}

static void signal_pending_xhci_events() {
	if (!xhci_event_generation) return;
	bool pending = false;
	for (auto* instance = xhci_controllers; instance; instance = instance->next) {
		if (!instance->running || !instance->controller.PrimaryEventRing()->HasFront()) continue;
		pending = true;
	}
	if (!pending) return;
	(void)Devsman::AcknowledgeXHCIInterrupt();
	device_interrupt_proc(IRQ_xHCI);
}

static void poll_xhci_policy(pureptr_t, stduint) {
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
	xhci_event_watchdog_armed = false;
	for (auto* instance = xhci_controllers; instance; instance = instance->next) {
		if (!instance->running || !instance->controller.PrimaryEventRing()->HasFront()) continue;
		auto& xhc = instance->controller;
		if (!instance->watchdog_reported) {
			using namespace uni::device::SpaceUSB3;
			auto* capability = reinterpret_cast<CapabilityRegisters*>(instance->mmio_base);
			auto* operational = reinterpret_cast<OperationalRegisters*>(
				instance->mmio_base + capability->CAPLENGTH.Read());
			auto* interrupter = reinterpret_cast<InterrupterRegisterSet*>(
				instance->mmio_base + capability->RTSOFF.Read().Offset() + 0x20u);
			plogwarn("xHCI watchdog PCI %[8H].%[8H].%[8H]: ack=%u usbsts=%[32H] iman=%[32H]",
				instance->bus, instance->device, instance->function,
				instance->acknowledgement_count, operational->USBSTS.Read().data[0],
				interrupter->IMAN.Read().data[0]);
			instance->watchdog_reported = true;
		}
		(void)acknowledge_xhci_instance(*instance);
		if (auto err = xhc.ProcessEvents()) {
			plogerro("xHCI watchdog PCI %[8H].%[8H].%[8H] ProcessEvents failed: %s at %s:%d",
				instance->bus, instance->device, instance->function,
				err.Name(), err.File(), err.Line());
		}
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
	if (!mmio) return false;
	void* instance_storage = uni_hostenv_allocator->allocate(
		sizeof(XHCIControllerInstance), 6);
	if (!instance_storage) {
		plogerro("xHCI cannot allocate controller instance");
		return false;
	}
	auto* instance = new (instance_storage) XHCIControllerInstance{
		stduint(mmio->start), xhc_node->fields.pci_bus,
		xhc_node->fields.pci_device, xhc_node->fields.pci_function };
	auto* xhc = &instance->controller;
	instance->next = xhci_controllers;
	xhci_controllers = instance;
	uint8 irq_line = 0xFF;
	uint8 irq_pin = 0;
	if (auto* irq = Devsman::FindResource(xhc_node, DeviceResourceType::IrqLine)) {
		irq_line = uint8(irq->start);
		irq_pin = uint8(irq->extra);
	}
	uni::PCI::Device xhc_tree_dev{};
	xhc_tree_dev.bus = xhc_node->fields.pci_bus;
	xhc_tree_dev.device = xhc_node->fields.pci_device;
	xhc_tree_dev.function = xhc_node->fields.pci_function;
	xhc_tree_dev.header_type = pci.read_header_type(xhc_tree_dev.bus, xhc_tree_dev.device, xhc_tree_dev.function);
	xhc_tree_dev.class_code.base = xhc_node->fields.class_base;
	xhc_tree_dev.class_code.sub = xhc_node->fields.class_sub;
	xhc_tree_dev.class_code.interface = xhc_node->fields.class_if;
	if (!initialize_xhci_controller(xhc_tree_dev, mmio->start, irq_line, irq_pin, *instance)) {
		xhci_controllers = instance->next;
		instance->~XHCIControllerInstance();
		uni_hostenv_allocator->deallocate(instance);
		return false;
	}
	signal_pending_xhci_events();
	(void)arm_xhci_event_watchdog();
	(void)arm_xhci_policy_tick();
	xhc_node->fields.binding.driver_data = xhc;
	auto usb_bus_name = String::newFormat("usb-bus@%04x:%02x:%02x.%x", 0,
		(stduint)xhc_node->fields.pci_bus,
		(stduint)xhc_node->fields.pci_device,
		(stduint)xhc_node->fields.pci_function);
	auto* usb_bus_node = Devsman::RegisterUSBBus(usb_bus_name.reference(), "xhci", xhc);
	Devsman::RegisterUSBRootHub(usb_bus_node, "usb-root-hub@0", 0x09u, 0x00u, 0x03u, "usb-root-hub", xhc);
	ploginfo("xHCI controller started at PCI %[8H].%[8H].%[8H]",
		xhc_tree_dev.bus, xhc_tree_dev.device, xhc_tree_dev.function);
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

bool Devsman::ProcessXHCIEvent(const DeviceEvent& event) {
	if (DeviceEventKind(event.kind) != DeviceEventKind::Interrupt ||
		event.device_handle != XHCIEventHandle || event.source != XHCIEventSource ||
		event.generation != xhci_event_generation) return false;
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
		plogwarn("[Devsman] xHCI interrupt ACK failed sequence=%llu", event.sequence);
	}
	return true;
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
	IC[IRQ_xHCI].setModeRupt(mglb(Handint_XHCI_Entry), SegCo64);
	register_interrupt_handler(IRQ_xHCI, Handint_XHCI);
	uni::device::SpaceUSB::HIDMouseDriver::default_observer = hand_mouse_usb;
	uni::device::SpaceUSB::HIDKeyboardDriver::default_observer = hand_kboard;
	Devsman::RegisterUSBDeviceTreeHooks();
}
#endif
