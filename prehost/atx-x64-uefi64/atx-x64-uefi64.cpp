// ASCII g++ TAB4 CRLF
// Attribute: Arch(AMD64)
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _HIS_TIME_H
#include <cpp/unisym>
//
#include <cpp/queue>
#include <cpp/interrupt>
#include <c/driver/mouse.h>// qemu only
#include <c/driver/timer.h>

#include <cpp/Device/Bus/PCI.hpp>
#include <cpp/Device/USB/xHCI/xHCI.hpp>
// #include <cpp/Device/USB/USB-Header.hpp>

#include "../../include/atx-x64.hpp"
#include "atx-x64-uefi64.loader/loader-graph.h"


static byte _b_vcb[byteof(VideoControlBlock)];

static byte _b_vcon0[byteof(VideoConsole)];
extern VideoConsole* vcon0;

struct Message {
	enum Type {
		RUPT_xHCI,
	} type;
};
Queue<Message>* message_queue;
static Message _BUF_Message[32];



void MouseObserver(int8 displacement_x, int8 displacement_y) {
	// ploginfo("mov (%d, %d)", displacement_x, displacement_y);
	// mouse_cursor->MoveRelative({ displacement_x,displacement_y });
	if (Cursor::global_cursor) global_layman.Domove(Cursor::global_cursor, { displacement_x,displacement_y });
}

// ---- Interrupt
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
	asserv(message_queue)->Enqueue(Message{ Message::RUPT_xHCI });
	sendEOI();
}

alignas(16) byte kernel_stack[1024 * 1024];

UefiData uefi_data;

// ---- Paging
/** @brief 静的に確保するPageDirectoryの個数
 *
 * この定数は SetupIdentityPageMap で使用される．
 * 1 つのPageDirectoryには 512 個の 2MiB ページを設定できるので，
 * kPageDirectoryCount x 1GiB の仮想アドレスがマッピングされることになる．
 */
const size_t kPageDirectoryCount = 64;

namespace {
	const uint64_t kPageSize4K = 4096;
	const uint64_t kPageSize2M = 512 * kPageSize4K;
	const uint64_t kPageSize1G = 512 * kPageSize2M;
	//
	alignas(kPageSize4K) std::array<uint64_t, 512> pml4_table;
	alignas(kPageSize4K) std::array<uint64_t, 512> pdp_table;
	alignas(kPageSize4K) std::array<std::array<uint64_t, 512>, kPageDirectoryCount> page_directory;
}

void SetupIdentityPageTable() {
	pml4_table[0] = reinterpret_cast<uint64_t>(&pdp_table[0]) | 0x003;
	for (int i_pdpt = 0; i_pdpt < page_directory.size(); ++i_pdpt) {
		pdp_table[i_pdpt] = reinterpret_cast<uint64_t>(&page_directory[i_pdpt]) | 0x003;
		for (int i_pd = 0; i_pd < 512; ++i_pd) {
			page_directory[i_pdpt][i_pd] = i_pdpt * kPageSize1G + i_pd * kPageSize2M | 0x083;
		}
	}
	setCR3(_IMM(&pml4_table[0]));
}

extern OstreamTrait* con0_out;

// ---- Kernel

extern "C" //__attribute__((ms_abi))
void mecocoa(const UefiData& uefi_data_ref)
{
	stduint rsp;
	_ASM("mov %%rsp, %0" : "=r"(rsp));
	uefi_data = uefi_data_ref;

	lapic_timer.Reset();
	lapic_timer.Ento();
	// Platform and Memory
	GDT_Init();
	SetupIdentityPageTable();
	Memory::initialize('UEFI', (byte*)(&uefi_data.memory_map));
	
	cons_init();

	ploginfo("Ciallo %lf, rsp=%[x]", 2025.09, rsp);

	Memory::pagebmap->dump_avail_memory();

	// IVT and Device
	InterruptControl APIC(_IMM(mem.allocate(256 * sizeof(gate_t))));
	APIC.Reset(SegCo64, 0x00000000);

	// Message Queue
	Queue<Message> msg_queue(_BUF_Message, numsof(_BUF_Message));
	::message_queue = &msg_queue;



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
		// vcon0->OutFormat("%[8H].%[8H].%[8H]: vend 0x%[16H], class 0x%[32H], head 0x%[8H]\n\r",
		// 	dev.bus, dev.device, dev.function, vendor_id, class_code, dev.header_type);
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
		APIC[InterruptVector::xHCI].setModeRupt(reinterpret_cast<uint64>(IntHandlerXHCI), SegCo64);
		// config MSI
		const uint8_t bsp_local_apic_id = treat<uint32>_IMM(0xFEE00020) >> 24;// or STI is useless -- Phina 20260117
		pci.configure_MSI_fixed_destination(
			*xhc_dev, bsp_local_apic_id,
			PCI::MSITriggerMode::Level,
			PCI::MSIDeliveryMode::Fixed,
			InterruptVector::xHCI, 0);
	}



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
		for1 (i, xhc.MaxPorts()) {
			auto port = xhc.PortAt(i);
			// ploginfo("Port %d: IsConnected=%d", i, port.IsConnected());
			if (port.IsConnected()) {
				if (auto err = ConfigurePort(xhc, port)) {
					plogerro("Failed to configure port: %s at %s:%d", err.Name(), err.File(), err.Line());
					continue;                                                                                                                
				}
			}
		}
		::xhc = &xhc;
		APIC.enAble(true);
		stduint elapsed_span = lapic_timer.Read();
		lapic_timer.Endo();
		//
		

		ploginfo("There are %[u] layers, f=%[x], l=%[x]", global_layman.Count(), global_layman.subf, global_layman.subl);
		// ploginfo("l_left=%[x]", layman.subl->sheet_pleft);
		ploginfo("Kernel Ready in 0x%[x] ticks", elapsed_span);
	}

	_ASM("UD2");

	while (true) {
		APIC.enAble(false);
		if (!message_queue->Count()) {
			APIC.enAble(true);
			HALT();
			continue;
		}
		Message msg;
		message_queue->Dequeue(msg);
		APIC.enAble(true);
		switch (msg.type) {
		case Message::RUPT_xHCI:
			while (xhc->PrimaryEventRing()->HasFront()) {
				if (auto err = ProcessEvent(*xhc)) {
					plogerro("Error while ProcessEvent: %s at %s:%d", err.Name(), err.File(), err.Line());
				}
			}
			break;
		default:
			plogerro("Unknown message type: %d", msg.type);
			break;
		}

		

	}
}




extern "C" { void* __dso_handle = 0; }
extern "C" { void __cxa_atexit(void) {} }

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

