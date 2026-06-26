// ASCII g++ TAB4 CRLF
// Attribute: Arch(AMD64)
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#include "c/driver/UART.h"
#include <c/driver/mouse.h>// qemu only
#include <c/driver/timer.h>
#include <cpp/Device/ACPI.hpp>
#include <c/format/filesys/FAT.h>
#include <cpp/Device/Bus/PCI.hpp>
#include <cpp/Device/USB/xHCI/xHCI.hpp>
#include <cpp/Witch/Control/Control-Label.hpp>

PCI pci;
byte _BUF_xhc[sizeof(uni::device::SpaceUSB3::HostController)];

alignas(16) byte kernel_stack[1024 * 1024];

UefiData uefi_data;


extern OstreamTrait* con0_out;
// extern x86_COM com1;
extern "C" void R_COM1_INIT();

static bool start_xhci_driver(DeviceNode* xhc_node) {
	if (!xhc_node) return false;
	auto& xhc = *reinterpret_cast<uni::device::SpaceUSB3::HostController*>(_BUF_xhc);
	auto* mmio = Devsman::FindResource(xhc_node, DeviceResourceType::PciBarMmio, 0);
	if (!mmio) return false;
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
	auto* xhc_dev = uni::device::SpaceUSB::HIDMouseDriver::Initialize(pci, xhc_tree_dev, mmio->start, irq_line, irq_pin, &xhc);
	if (!xhc_dev) return false;
	xhc_node->fields.binding.driver_data = &xhc;
	auto usb_bus_name = String::newFormat("usb-bus@%04x:%02x:%02x.%x", 0,
		(stduint)xhc_node->fields.pci_bus,
		(stduint)xhc_node->fields.pci_device,
		(stduint)xhc_node->fields.pci_function);
	Devsman::RegisterUSBBus(usb_bus_name.reference(), "xhci", &xhc);
	ploginfo("xHC-USB-Mouse has been found: %[8H].%[8H].%[8H]", xhc_dev->bus, xhc_dev->device, xhc_dev->function);
	return true;
}


// ---- Kernel

#define Systime SysTimer
extern "C" //__attribute__((ms_abi))
void mecocoa(const UefiData& uefi_data_ref)
{
	#ifdef _UEFI
	stduint rsp;
	uefi_data = uefi_data_ref;
	_ASM("mov %%rsp, %0" : "=r"(rsp));
	#endif

	_call_serious = kernel_fail;
	x86_COM com1;
	con0_out = &com1;
	if (!Memory::initialize('UEFI', (byte*)(&uefi_data.memory_map))) HALT();
	Consman::Initialize();
	//{} Cache_t::enAble();
	Filesys::Initialize();
	Systime::Initialize();
	Taskman::Initialize();
	Devsman::Initialize();
	Virtman::Initialize();
	Syscall::Initialize();
	// Coreman::Initialize();

	//{} mem > 4G

	R_COM1_INIT();

	#ifdef _UEFI
	ploginfo("Ciallo %lf, rsp=%[x]", 2025.09, rsp);
	#endif

	// Deivce Driver
	auto& xhc = *reinterpret_cast<uni::device::SpaceUSB3::HostController*>(_BUF_xhc);
	if (!PCI_Init(pci)) {
		plogerro("No devices on PCI or PCI init failed.");
	}
	IC[IRQ_xHCI].setModeRupt(mglb(Handint_XHCI_Entry), SegCo64);
	register_interrupt_handler(IRQ_xHCI, Handint_XHCI);
	//[TIM.LAPIC] -> sys-delay
	ACPI::Assert(*(const ACPI::RSDP*)uefi_data.acpi_table);
	IC[IRQ_LAPICTimer].setModeRupt(mglb(Handint_LAPICT_Entry), SegCo64);
	register_interrupt_handler(IRQ_LAPICTimer, Handint_LAPICT);
	lapic_timer.Reset();

	lapic_timer.Reset(lapic_timer.Frequency / SysTickFreq);
	//[USB Mouse&Keyboard]
	uni::device::SpaceUSB::HIDMouseDriver::default_observer = hand_mouse_usb;
	uni::device::SpaceUSB::HIDKeyboardDriver::default_observer = hand_kboard;
	Devsman::RegisterXHCIDeviceTreeHook();
	Devsman::RegisterDriverStarter("xhci", start_xhci_driver);
	Devsman::StartKnownDrivers();

	mecfetch();
	tryUD();

	// Service
	Taskman::Create((void*)&serv_task_loop, RING_M);
	Taskman::Create((void*)&serv_cons_loop, RING_M);
	Taskman::Create((void*)&serv_graf_loop, RING_M);
	Taskman::Create((void*)&serv_file_loop, RING_M);
	//
	Taskman::Create((void*)&serv_dev_mem_loop, RING_M);

	// try priority_queue Dchain
	SysTimer::Append(250, 1);
	SysTimer::Append(100, 0);

	serv_sysmsg();
}
