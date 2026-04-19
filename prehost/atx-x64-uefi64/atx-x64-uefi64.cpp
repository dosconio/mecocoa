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

void TaskB() {
	ploginfo("TaskB");
	stduint cnt = 0;
	extern uni::witch::control::Label* plabel_1;
	while (1) {
		// IC.enAble(false);
		plabel_1->text = String::newFormat("t%u", cnt++);
		plabel_1->doshow(0);
		// IC.enAble(true);
		HALT(); // SwitchTaskContext(&task_kernel_ctx, &task_b_ctx);
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
	cons_init();
	//{} Cache_t::enAble();
	Taskman::Initialize();

	#ifdef _UEFI
	ploginfo("Ciallo %lf, rsp=%[x]", 2025.09, rsp);
	#endif

	// IVT and Device
	new (&IC) InterruptControl(mglb(mem.allocate(256 * sizeof(gate_t))));
	IC.Reset(SegCo64, 0xFFFFFFFFC0000000ull);

	// Syscall
	setMSR(x86MSR::EFER, 0x0501);
	setMSR(x86MSR::LSTAR, mglb(Handint_SYSCALL_Entry));
	setMSR(x86MSR::STAR, (_IMM(SegCo64) << 32) | (_IMM(SegCo32 | 3) << 48));
	setMSR(x86MSR::FMASK, 0x200);
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
	SysTimer::Initialize();
	//[USB Mouse&Keyboard]
	uni::device::SpaceUSB::HIDMouseDriver::default_observer = hand_mouse;
	uni::device::SpaceUSB::HIDKeyboardDriver::default_observer = hand_kboard;
	if (auto xhc_dev = Mouse_Init_USB(pci, &xhc)) {
		ploginfo("xHC-USB-Mouse has been found: %[8H].%[8H].%[8H]", xhc_dev->bus, xhc_dev->device, xhc_dev->function);
	}

	tryUD();

	// Service
	Taskman::Create((void*)&serv_task_loop, RING_M);
	Taskman::Create((pureptr_t)TaskB, 0);

	// try priority_queue Dchain
	SysTimer::Append(250, 1);
	SysTimer::Append(100, 0);

	//{} "ls /"
	//{} "cat a.txt"
	if (1) {
		auto memdev_buffer = new byte[512];// new(std::align_val_t(64))
		auto fat_buffer = new byte[512];
		MemoryBlockDevice memdev({ _IMM(uefi_data.fatvhd_addr), 32 * 1024 * 1024 }, memdev_buffer);
		FilesysFAT fatvhd(32, memdev, fat_buffer, NULL);
		fatvhd.buffer_fatable = new byte[memdev.Block_Size];
		FAT_FileHandle* han;
		FAT_FileHandle filhan;
		FilesysSearchArgs args = { &filhan, nullptr, nullptr, nullptr };
		if (!fatvhd.loadfs()) {
			plogerro("FATVHD loadfs failed");
		}
		else {
			han = (FAT_FileHandle*)fatvhd.search("/", &args);
			fatvhd.enumer(han, NULL);
			if (han = (FAT_FileHandle*)fatvhd.search("appa.elf", &args)) {
				FileBlockBridge loop_device(&fatvhd, han, han->size, 512);
				if (auto pb = Taskman::CreateELF(&loop_device, RING_U)) {
					Taskman::Append(pb);
					Taskman::AppendThread(pb->main_thread);
					// pb->focus_tty_id = 0;{} TODO
					vttys[0]->type = pb->getID();
				}
				else plogerro("appa.elf: Fail to parse or load ELF");
			}
		}
		delete[] (fatvhd.buffer_fatable);
		delete[] (memdev_buffer);
		delete[] (fat_buffer);
	}

	syscall(syscall_t::OUTC, (usize)"Ohayou\n\r", 8);

	if (1) {
		// Memory::pagebmap->dump_avail_memory();
		mempool.dump_available();
		ploginfo("[Console] There are %[u] layers, f=%[x], l=%[x]", global_layman.Count(), global_layman.subf, global_layman.subl);
	}

	serv_sysmsg();
}
