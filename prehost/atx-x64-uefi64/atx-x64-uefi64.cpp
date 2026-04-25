// ASCII g++ TAB4 CRLF
// Attribute: Arch(AMD64)
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

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

struct Syscall {
	static void Initialize() {
		setMSR(x86MSR::EFER, 0x0501);
		setMSR(x86MSR::LSTAR, mglb(Handint_SYSCALL_Entry));
		setMSR(x86MSR::STAR, (_IMM(SegCo64) << 32) | (_IMM(SegCo32 | _IMM(RING_U)) << 48));
		setMSR(x86MSR::FMASK, 0x200);
	}
};

// ---- Kernel

extern "C" //__attribute__((ms_abi))
void mecocoa(const UefiData& uefi_data_ref)
{
	#ifdef _UEFI
	stduint rsp;
	uefi_data = uefi_data_ref;
	_ASM("mov %%rsp, %0" : "=r"(rsp));
	#endif

	_call_serious = kernel_fail;
	
	if (!Memory::initialize('UEFI', (byte*)(&uefi_data.memory_map))) HALT();
	cons_init();
	//{} Cache_t::enAble();
	Taskman::Initialize();
	SysTimer::Initialize();
	Filesys::Initialize();

	#ifdef _UEFI
	ploginfo("Ciallo %lf, rsp=%[x]", 2025.09, rsp);
	#endif

	// IVT and Device
	new (&IC) InterruptControl(mglb(mem.allocate(256 * sizeof(gate_t))));
	IC.Reset(SegCo64, 0xFFFFFFFFC0000000ull);

	Syscall::Initialize();

	// Deivce Driver
	auto& xhc = *reinterpret_cast<uni::device::SpaceUSB3::HostController*>(_BUF_xhc);
	if (!PCI_Init(pci)) {
		plogerro("No devices on PCI or PCI init failed.");
	}
	IC[IRQ_xHCI].setModeRupt(mglb(Handint_XHCI_Entry), SegCo64);
	//[TIM.LAPIC] -> sys-delay
	ACPI::Assert(*(const ACPI::RSDP*)uefi_data.acpi_table);
	IC[IRQ_LAPICTimer].setModeRupt(mglb(Handint_LAPICT_Entry), SegCo64);
	lapic_timer.Reset();
	lapic_timer.Reset(lapic_timer.Frequency / SysTickFreq);
	//[USB Mouse&Keyboard]
	uni::device::SpaceUSB::HIDMouseDriver::default_observer = hand_mouse;
	uni::device::SpaceUSB::HIDKeyboardDriver::default_observer = hand_kboard;
	if (auto xhc_dev = Mouse_Init_USB(pci, &xhc)) {
		ploginfo("xHC-USB-Mouse has been found: %[8H].%[8H].%[8H]", xhc_dev->bus, xhc_dev->device, xhc_dev->function);
	}

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
