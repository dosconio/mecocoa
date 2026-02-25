// ASCII g++ TAB4 CRLF
// Attribute: Arch(AMD64)
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#include <c/driver/mouse.h>// qemu only
#include <c/driver/timer.h>
#include <cpp/Device/ACPI.hpp>
#include <cpp/Device/Bus/PCI.hpp>
#include <cpp/Device/USB/xHCI/xHCI.hpp>
#include <cpp/Witch/Control/Control-Label.hpp>

extern VideoConsole* vcon0;

PCI pci;
byte _BUF_xhc[sizeof(uni::device::SpaceUSB3::HostController)];

alignas(16) byte kernel_stack[1024 * 1024];

UefiData uefi_data;

extern OstreamTrait* con0_out;



alignas(16) NormalTaskContext task_b_ctx, task_kernel_ctx;
stduint _TEMP task_id = 0;

void TaskB() {
	ploginfo("TaskB");
	while (1) {
		auto crt_tick = tick;
		extern uni::witch::control::Label* plabel_1;
		plabel_1->text = String::newFormat("t%u", crt_tick);
		plabel_1->doshow(0);
		SwitchTaskContext(&task_kernel_ctx, &task_b_ctx);
	}
}

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
	const unsigned mempool_lenN = 0x20000;
	mempool.Append(Slice{ _IMM(mem.allocate(mempool_lenN)), mempool_lenN });
	mempool.Append(Slice{ _IMM(mem.allocate(mempool_lenN)), mempool_lenN });

	
	cons_init();
	//{} Cache_t::enAble();
	Taskman::Initialize();

	#ifdef _UEFI
	ploginfo("Ciallo %lf, rsp=%[x]", 2025.09, rsp);
	#endif

	// IVT and Device
	new (&IC) InterruptControl(_IMM(mem.allocate(256 * sizeof(gate_t))));
	IC.Reset(SegCo64, 0x00000000);

	auto& xhc = *reinterpret_cast<uni::device::SpaceUSB3::HostController*>(_BUF_xhc);
	if (!PCI_Init(pci)) {
		plogerro("No devices on PCI or PCI init failed.");
	}
	IC[IRQ_xHCI].setModeRupt(_IMM(Handint_XHCI), SegCo64);
	//[TIM.LAPIC] -> sys-delay
	ACPI::Assert(*(const ACPI::RSDP*)uefi_data.acpi_table);
	IC[IRQ_LAPICTimer].setModeRupt(_IMM(Handint_LAPICT), SegCo64);
	lapic_timer.Reset();
	lapic_timer.Reset(lapic_timer.Frequency / SysTickFreq);
	SysTimer::Initialize();
	//[USB Mouse&Keyboard]
	uni::device::SpaceUSB::HIDMouseDriver::default_observer = hand_mouse;
	uni::device::SpaceUSB::HIDKeyboardDriver::default_observer = hand_kboard;
	if (auto xhc_dev = Mouse_Init_USB(pci, &xhc)) {
		ploginfo("xHC-USB-Mouse has been found: %[8H].%[8H].%[8H]", xhc_dev->bus, xhc_dev->device, xhc_dev->function);
	}

	tryUD();

	// try priority_queue Dchain
	SysTimer::Append(250, 1);
	SysTimer::Append(100, 0);

	// demo multitask
	MemSet(&task_b_ctx, 0, sizeof(task_b_ctx));
	task_b_ctx.IP = reinterpret_cast<uint64_t>(TaskB);
	task_b_ctx.DI = 1;
	task_b_ctx.SI = 42;
	task_b_ctx.CR3 = getCR3();
	task_b_ctx.FLAG = 0x202;
	task_b_ctx.CS = SegCo64;
	task_b_ctx.DS = SegData;
	task_b_ctx.ES = SegData;
	task_b_ctx.FS = SegData;
	task_b_ctx.GS = SegData;
	task_b_ctx.SS = SegData;
	task_b_ctx.SP = (_IMM(new byte[1024] + 1024) & ~0xflu) - 8;
	*reinterpret_cast<uint32_t*>(&task_b_ctx.floating_point_context[24]) = 0x1f80;

	IC.enAble(true);

	if (1) {
		Memory::pagebmap->dump_avail_memory();
		ploginfo("[Console] There are %[u] layers, f=%[x], l=%[x]", global_layman.Count(), global_layman.subf, global_layman.subl);
	}

	serv_sysmsg();
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

