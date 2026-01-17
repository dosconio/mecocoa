// ASCII g++ TAB4 CRLF
// Attribute: Arch(AMD64)
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#include <cpp/unisym>
#include <cpp/interrupt>
#include <c/driver/mouse.h>// qemu only
#include <cpp/Device/_Video.hpp>
#include <cpp/Device/Bus/PCI.hpp>
#include <cpp/Device/USB/xHCI/xHCI.hpp>
// #include <cpp/Device/USB/USB-Header.hpp>


using namespace uni;
#include "atx-x64-uefi64.loader/loader-graph.h"
#include "../../include/atx-x64-uefi64.hpp"


static byte _b_vcb[byteof(VideoControlBlock)];

static byte _b_vcon0[byteof(VideoConsole)];
VideoConsole* vcon0;

extern "C" byte BSS_ENTO, BSS_ENDO;
FrameBufferConfig config_graph;

Cursor* mouse_cursor;
void MouseObserver(int8 displacement_x, int8 displacement_y) {
	// ploginfo("mov (%d, %d)", displacement_x, displacement_y);
	mouse_cursor->MoveRelative({ displacement_x, displacement_y });
}

// ---- Interrupt
alignas(64) gate_t idt[256];
struct InterruptVector {
	enum Number {
		xHCI = 0x40,
	};
};


// ---- USB Driver
alignas(64) uint8_t memory_pool[kMemoryPoolSize];
uintptr_t alloc_ptr = reinterpret_cast<uintptr_t>(memory_pool);
void* AllocMem(size_t size, unsigned int alignment, unsigned int boundary) {
	if (alignment > 0) {
		alloc_ptr = ceilAlign(alignment, alloc_ptr);
	}
	if (boundary > 0) {
		auto next_boundary = ceilAlign(boundary, alloc_ptr); 
		if (next_boundary < alloc_ptr + size) {
			alloc_ptr = next_boundary;
		}
	}
	if (uintptr_t(memory_pool) + kMemoryPoolSize < alloc_ptr + size) {
		return nullptr;
	}
	auto p = alloc_ptr;
	alloc_ptr += size;
	return reinterpret_cast<void*>(p);
}

void SwitchEhci2Xhci(PCI& pci, const PCI::Device& xhc_dev) {
	bool intel_ehc_exist = false;
	for0 (i, pci.num_device) {
		if (pci.devices[i].class_code.Match(0x0cu, 0x03u, 0x20u) /* EHCI */ &&
			0x8086 == pci.read_vendor_id(pci.devices[i])) {
			intel_ehc_exist = true;
			break;
		}
	}
	if (!intel_ehc_exist) {
		return;
	}
	uint32_t superspeed_ports = pci.read_config_register(xhc_dev, 0xdc); // USB3PRM
	pci.write_config_register(xhc_dev, 0xd8, superspeed_ports); // USB3_PSSEN
	uint32_t ehci2xhci_ports = pci.read_config_register(xhc_dev, 0xd4); // XUSB2PRM
	pci.write_config_register(xhc_dev, 0xd0, ehci2xhci_ports); // XUSB2PR
	ploginfo("%s: SS = %02, xHCI = %02x\n", __FUNCIDEN__, superspeed_ports, ehci2xhci_ports);
}

usb::xhci::Controller* xhc;

_ESYM_C void General_IRQHandler(InterruptFrame* frame);
__attribute__((interrupt, target("general-regs-only")))
void IntHandlerXHCI(InterruptFrame* frame) {
	// while(1);
	// ploginfo("rupt!");
	while (xhc->PrimaryEventRing()->HasFront()) {
		if (auto err = ProcessEvent(*xhc)) {
			plogerro("Error while ProcessEvent: %s at %s:%d", err.Name(), err.File(), err.Line());
		}
	}
	sendEOI();
}

void EnableLocalAPIC() {
    // SVR
    volatile uint32_t* svr = reinterpret_cast<volatile uint32_t*>(0xFEE000F0);
    uint32_t value = *svr;
    // set Bit 8 (Enable) and Bits 0-7 (Spurious Vector = 0xFF)
    value |= 0x100; // Bit 8: Software Enable
    value |= 0xFF;  // Vector 0xFF for spurious interrupts
    *svr = value;
    // ploginfo("Local APIC Software Enabled via SVR.");
}

extern "C" //__attribute__((ms_abi))
void _entry(const UefiData& uefi_data)
{
	MemSet(&BSS_ENTO, &BSS_ENDO - &BSS_ENTO, 0);
	config_graph = uefi_data;

	GloScreenARGB8888 vga_ARGB8888;
	GloScreenABGR8888 vga_ABGR8888;
	VideoControlInterface* screen;

	switch (uefi_data.pixel_format) {
	case PixelFormat::ARGB8888:
		screen = (&vga_ARGB8888);
		break;
	case PixelFormat::ABGR8888:
		screen = &vga_ABGR8888;
		break;
	default:
		loop _ASM("hlt");
	}


	Size2 screen_size(uefi_data.horizontal_resolution, uefi_data.vertical_resolution);
	Rectangle screen0_win(Point(0, 0), screen_size, Color::White);
	VideoControlBlock* p_vcb = new (_b_vcb) VideoControlBlock\
		((pureptr_t)uefi_data.frame_buffer, *screen);
	p_vcb->setMode(uefi_data.pixel_format, screen_size.x, screen_size.y);

	vcon0 = new (_b_vcon0) VideoConsole(*screen, screen0_win);
	vcon0->backcolor = Color::White;
	vcon0->forecolor = Color::Black;
	vcon0->Clear();
	ploginfo("Ciallo %lf", 2025.09);

	// IVT and Device
	InterruptControl GIC(_IMM(idt));
	// EnableLocalAPIC();

	PCI pci;
	uint64_t xhc_mmio_base = nil;
	auto err = pci.scan_all_bus();
	vcon0->OutFormat("[Devices] (%s) PCI: Detect %u devices.\n\r", err.Name(), pci.num_device);

	// seek an Intel xHC
	PCI::Device* xhc_dev = nullptr;
	bool found_usbmouse = false;
	for0(i, pci.num_device) {
		const auto& dev = pci.devices[i];
		auto vendor_id = pci.read_vendor_id(dev);
		if (!found_usbmouse && pci.devices[i].class_code.Match(ClassCodeGroup_xHC)) {
			xhc_dev = &pci.devices[i];
			if (0x8086 == vendor_id) {
				found_usbmouse = true;
			}
		}
		auto class_code = pci.read_class_code(dev.bus, dev.device, dev.function);
		//
		vcon0->OutFormat("%[8H].%[8H].%[8H]: vend 0x%[16H], class 0x%[32H], head 0x%[8H]\n\r",
			dev.bus, dev.device, dev.function, vendor_id, class_code, dev.header_type);
	}
	// get xHC memory base address
	if (xhc_dev) {
		ploginfo("xHC has been found: %[8H].%[8H].%[8H]", xhc_dev->bus, xhc_dev->device, xhc_dev->function);
		auto xhc_bar = pci.ReadBar(*xhc_dev, 0);
		if (!xhc_bar.pvalue) {
			plogerro("xHC BAR0 is not valid.");
		}
		xhc_mmio_base = *xhc_bar.pvalue & ~_IMM(0xF);
		ploginfo("xHC mmio_base = 0x%[x]", xhc_mmio_base);
		// setting interrupt of xHC
		//{TODO}  use GIC
		const uint16 cs = getCS();
		for0(i, 0x1000 / sizeof(gate_t)) {
			auto& idt_entry = idt[i];
			idt_entry.setModeRupt(reinterpret_cast<uint64>(General_IRQHandler), cs);
		}
		auto& xchi_idt_entry = idt[InterruptVector::xHCI];
		xchi_idt_entry.setModeRupt(reinterpret_cast<uint64>(IntHandlerXHCI), cs);
		loadIDT(_IMM(idt), sizeof(idt) - 1);
		// config MSI
		const uint8_t bsp_local_apic_id = treat<uint32>(0xfee00020) >> 24;// or STI is useless -- Phina 20260117
		pci.configure_MSI_fixed_destination(
			*xhc_dev, bsp_local_apic_id,
			PCI::MSITriggerMode::Level,
			PCI::MSIDeliveryMode::Fixed,
			InterruptVector::xHCI, 0);
	}

	Cursor cursor{
		&p_vcb->getVCI(), Color::White, {300, 200}
	};
	mouse_cursor = &cursor;

	//
	if (xhc_mmio_base) {
		usb::xhci::Controller xhc{ xhc_mmio_base };
		if (0x8086 == pci.read_vendor_id(*xhc_dev)) {
			SwitchEhci2Xhci(pci, *xhc_dev);
		}
		{
			auto err = xhc.Initialize();
			ploginfo("xhc.Initialize: %s", err.Name());
		}
		ploginfo("xHC starting");
		xhc.Run();
		// 
		usb::HIDMouseDriver::default_observer = MouseObserver;
		for (int i = 1; i <= xhc.MaxPorts(); ++i) {
			auto port = xhc.PortAt(i);
			ploginfo("Port %d: IsConnected=%d", i, port.IsConnected());
			if (port.IsConnected()) {
			if (auto err = ConfigurePort(xhc, port)) {
				plogerro("failed to configure port: %s at %s:%d", err.Name(), err.File(), err.Line());
				continue;
			}
			}
		}
		::xhc = &xhc;
		enInterrupt(true);
		

		// ploginfo("Kernel Ready");
		// loop if (auto err = usb::xhci::ProcessEvent(xhc)) {
		// 	plogerro("Error while ProcessEvent: %s at %s:%d", err.Name(), err.File(), err.Line());
		// }
		loop;
	}

	loop _ASM("hlt");
}

// console.cpp
void outtxt(const char* str, stduint len) {
	vcon0->out(str, len);
}




extern "C" { void* __dso_handle = 0; }
extern "C" { void __cxa_atexit(void) {} }
void operator delete(void*) {}
void operator delete(void* ptr, unsigned long size) noexcept { _TODO }
void operator delete(void* ptr, unsigned long size, std::align_val_t) noexcept { ::operator delete(ptr, size); }
_ESYM_C void __cxa_pure_virtual(void) {}
void std::__throw_bad_function_call(void) {
	plogerro("%s", __FUNCIDEN__); loop;
}
_ESYM_C void* memset(void* dest, int val, size_t count) {
	auto d = (byte*)dest;
	for0(i, count) d[i] = (byte)val;
	return dest;
}
_ESYM_C char* memmove(char* dest, const char* sors, size_t width) {
	return MemAbsolute(dest, sors, width);
}
_ESYM_C void abort(void) {
	plogerro("%s", __FUNCIDEN__); loop;
}

