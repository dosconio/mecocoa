// ASCII g++ TAB4 CRLF
// Attribute: Arch(AMD64)
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#include <cpp/unisym>
#include <cpp/interrupt>
#include <c/driver/mouse.h>// qemu only
#include <c/driver/timer.h>
#include <cpp/Device/Bus/PCI.hpp>
#include <cpp/Device/USB/xHCI/xHCI.hpp>
#include "../../include/atx-x64.hpp"

extern VideoConsole* vcon0;

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
void* usb::HIDMouseDriver::operator new(size_t size) {
	return AllocMem(sizeof(HIDMouseDriver), 0, 0);
}

void usb::HIDMouseDriver::operator delete(void* ptr) noexcept {
	// FreeMem(ptr);
}

PCI pci;
byte _BUF_xhc[sizeof(usb::xhci::Controller)];

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
			page_directory[i_pdpt][i_pd] = i_pdpt * kPageSize1G + i_pd * kPageSize2M | 0x093;// 0x83 for cacheable
		}
	}
	setCR3(_IMM(&pml4_table[0]));
}

extern OstreamTrait* con0_out;

// ---- Kernel

extern "C" //__attribute__((ms_abi))
void mecocoa(const UefiData& uefi_data_ref)
{
	#ifdef _UEFI
	stduint rsp;
	uefi_data = uefi_data_ref;
	_ASM("mov %%rsp, %0" : "=r"(rsp));
	#endif

	GDT_Init();
	if (!Memory::initialize('UEFI', (byte*)(&uefi_data.memory_map))) HALT();
	SetupIdentityPageTable();
	
	cons_init();

	#ifdef _UEFI
	ploginfo("Ciallo %lf, rsp=%[x]", 2025.09, rsp);
	#endif



	// IVT and Device
	InterruptControl APIC(_IMM(mem.allocate(256 * sizeof(gate_t))));
	APIC.Reset(SegCo64, 0x00000000);

	usb::xhci::Controller& xhc = *reinterpret_cast<usb::xhci::Controller*>(_BUF_xhc);
	if (!PCI_Init(pci)) {
		plogerro("No devices on PCI or PCI init failed.");
	}
	APIC[IRQ_xHCI].setModeRupt(reinterpret_cast<uint64>(Handint_XHCI), SegCo64);
	//[USB Mouse]
	usb::HIDMouseDriver::default_observer = hand_mouse;
	if (auto xhc_dev = Mouse_Init_USB(pci, &xhc)) {
		ploginfo("xHC-USB-Mouse has been found: %[8H].%[8H].%[8H]", xhc_dev->bus, xhc_dev->device, xhc_dev->function);
	}
	//[LAPIC]
	lapic_timer.Reset();
	lapic_timer.Ento();

	Memory::pagebmap->dump_avail_memory();

	_ASM("UD2");

	APIC.enAble(true);
	stduint elapsed_span = lapic_timer.Read();
	lapic_timer.Endo();
	//
	ploginfo("There are %[u] layers, f=%[x], l=%[x]", global_layman.Count(), global_layman.subf, global_layman.subl);
	ploginfo("Kernel Ready in 0x%[x] ticks", elapsed_span);

	while (true) {
		APIC.enAble(false);
		if (!message_queue.Count()) {
			APIC.enAble(true);
			HALT();
			continue;
		}
		SysMessage msg;
		message_queue.Dequeue(msg);
		APIC.enAble(true);
		switch (msg.type) {
		case SysMessage::RUPT_xHCI:
			if (auto err = xhc.ProcessEvents()) {
				plogerro("Error while ProcessEvent: %s at %s:%d", err.Name(), err.File(), err.Line());
			}
			break;
		default:
			plogerro("Unknown message type: %d", msg.type);
			break;
		}
	}
}



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

