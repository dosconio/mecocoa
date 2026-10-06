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
	volatile stduint xhci_mmio_base = 0;
	volatile bool xhci_running = false;
	uint32 xhci_event_generation = 0;
}

alignas(32)
byte _BUF_xhc[sizeof(uni::device::SpaceUSB3::HostController)];

//{TEMP} version
static bool initialize_xhci_controller(uni::PCI::Device& xhc_dev, uint64 mmio_base,
	uint8 irq_line, uint8 irq_pin, uni::device::SpaceUSB3::HostController* xhc) {
	using namespace uni;
	if (!mmio_base || !xhc) return false;
	pci.enable_MMIO(xhc_dev);
	ploginfo("xHC resource IRQ line=%u pin=%u", (unsigned)irq_line, (unsigned)irq_pin);
	// config MSI
	const bool x2mode = (getMSR(x86MSR::APIC_BASE) & (1ULL << 10)) != 0;
	PortAdapter lapic;
	lapic.typ = x2mode ? 2 : 1;
	const uint32 apic_id_value = lapic.ReadLAPIC(0x20);
	const uint8 bsp_local_apic_id = x2mode
		? uint8(apic_id_value) : uint8(apic_id_value >> 24); // or STI is useless -- Phina 20260117
	pci.configure_MSI_fixed_destination(
		xhc_dev, bsp_local_apic_id,
		PCI::MSITriggerMode::Edge,
		PCI::MSIDeliveryMode::Fixed,
		IRQ_xHCI, 0);
	//
	new (xhc) uni::device::SpaceUSB3::HostController(mmio_base);
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

static void signal_pending_xhci_events() {
	if (!xhci_running || !xhci_event_generation) return;
	auto& xhc = *reinterpret_cast<uni::device::SpaceUSB3::HostController*>(_BUF_xhc);
	if (!xhc.PrimaryEventRing()->HasFront()) return;
	(void)Devsman::AcknowledgeXHCIInterrupt();
	device_interrupt_proc(IRQ_xHCI);
}

static bool start_xhci_driver(DeviceNode* xhc_node) {
	if (!xhc_node) return false;
	xhci_running = false;
	auto& xhc = *reinterpret_cast<uni::device::SpaceUSB3::HostController*>(_BUF_xhc);
	auto* mmio = Devsman::FindResource(xhc_node, DeviceResourceType::PciBarMmio, 0);
	if (!mmio) return false;
	xhci_mmio_base = stduint(mmio->start);
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
	if (!initialize_xhci_controller(xhc_tree_dev, mmio->start, irq_line, irq_pin, &xhc)) {
		xhci_mmio_base = 0;
		return false;
	}
	xhci_running = true;
	signal_pending_xhci_events();
	xhc_node->fields.binding.driver_data = &xhc;
	auto usb_bus_name = String::newFormat("usb-bus@%04x:%02x:%02x.%x", 0,
		(stduint)xhc_node->fields.pci_bus,
		(stduint)xhc_node->fields.pci_device,
		(stduint)xhc_node->fields.pci_function);
	auto* usb_bus_node = Devsman::RegisterUSBBus(usb_bus_name.reference(), "xhci", &xhc);
	Devsman::RegisterUSBRootHub(usb_bus_node, "usb-root-hub@0", 0x09u, 0x00u, 0x03u, "usb-root-hub", &xhc);
	ploginfo("xHC-USB-Mouse has been found: %[8H].%[8H].%[8H]",
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
	if (!xhci_running || !xhci_mmio_base) return false;
	using namespace uni::device::SpaceUSB3;
	auto* capability = reinterpret_cast<CapabilityRegisters*>(xhci_mmio_base);
	auto* operational = reinterpret_cast<OperationalRegisters*>(
		xhci_mmio_base + capability->CAPLENGTH.Read());
	auto* interrupter = reinterpret_cast<InterrupterRegisterSet*>(
		xhci_mmio_base + capability->RTSOFF.Read().Offset() + 0x20u);
	const auto status = operational->USBSTS.Read();
	const auto iman = interrupter->IMAN.Read();
	if (!status.bits.event_interrupt && !iman.bits.interrupt_pending) return false;
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

bool Devsman::ProcessXHCIEvent(const DeviceEvent& event) {
	if (!xhci_running || DeviceEventKind(event.kind) != DeviceEventKind::Interrupt ||
		event.device_handle != XHCIEventHandle || event.source != XHCIEventSource ||
		event.generation != xhci_event_generation) return false;
	auto& xhc = *reinterpret_cast<uni::device::SpaceUSB3::HostController*>(_BUF_xhc);
	if (auto err = xhc.ProcessEvents()) {
		plogerro("Error while ProcessEvent: %s at %s:%d", err.Name(), err.File(), err.Line());
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
