// ASCII CPP-ISO11 TAB4 LF
// ModuTitle: Disk - Secure Digital Card (SDMMC1)
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include <cpp/MCU/ST/STM32H7>
#include <cpp/Device/SD.hpp>
#include <c/format/filesys.h>
#include <c/format/filesys/FAT.h>

_ESYM_C void R_SDCARD_INIT();

#if (_MCCA & 0xFFFF) == 0x2032

extern file_system_type fs_fat;// defined in meccoa/filesys.cpp, the FAT probe type

// The x86 storage drivers keep their bring-up in a driver starter that the
// Devsman service thread calls, and never create a thread of their own. On ARM
// that same thread is reached through the timer service: SysTick collects the
// expired action (Systimex::CollectExpired) and the Devsman service thread runs
// it (Systimex::DispatchExpired). The RMOD walk itself must not do the work: it
// runs with interrupts masked, where SysTick::getTick() is frozen and every wait
// inside SDCard1 would spin forever.
__attribute__((section(".init.rmod")))
extern const RMOD_LIST RMOD_LIST_SDCARD{
	R_SDCARD_INIT,
	"SD-SDMMC1",
	{}
};

// The card object is a StorageTrait itself, as in the 91-ImageShow demo, so the
// trait overload SDCard1.Read(lba, buf, n) is the call to use: with
// storage_method == IOMethod::DMA it waits through ReadDMA_Blocking
// (SDCard.cpp:172), while the explicit-method overload Read(buf, lba, n, DMA)
// only starts the transfer and returns at once. Polling is not usable here:
// services and user programs share RING_M, so one tick hands the CPU to a
// spinning program and the receive FIFO overruns (fb=32 SDMMC_ERROR_RX_OVERRUN).
static byte* sd_stack_base = nullptr;
static byte* sd_stack_top = nullptr;

static void SdCardMount();

static void SdCardBringUp(void*, ...) {
	// paint the unused part of the service stack, the peak is read back below: this
	// bring-up is the deepest thing that runs on the Devsman thread
	auto* th = Taskman::CurrentTB();
	if (th && th->stack_lineaddr) {
		sd_stack_base = th->stack_lineaddr;
		sd_stack_top = th->stack_lineaddr + th->stack_size;
		stduint sp_now = 0;
		_ASM volatile("mrs %0, psp" : "=r"(sp_now));
		byte* free_end = (byte*)sp_now - 0x40;
		for (byte* q = sd_stack_base; q < free_end; q++) *q = 0xA5;
	}
	SdCardMount();
	if (sd_stack_base) {
		byte* deepest = sd_stack_base;
		while (deepest < sd_stack_top && *deepest == 0xA5) deepest++;
		ploginfo("[SD] stack base=%p top=%p peak=%u canary=%08X",
			sd_stack_base, sd_stack_top,
			(stduint)(sd_stack_top - deepest), *(stduint*)sd_stack_base);
	}
}

static void SdCardMount() {
	ploginfo("[SD] setMode ...");
	// defaults of SD.hpp: SDMMC1, PLL1Q, 4-bit bus, no flow control
	if (!SDCard1.setMode()) {
		plogerro("[SD] SDMMC1 init failed, no card?");
		return;
	}
	// setMode() enables IRQ_SDMMC1 at priority 0: bring it down to the scheduler
	// level so no SD interrupt can preempt the PendSV context switch
	ploginfo("[SD] setMode ok");
	NVIC.setPriority(IRQ_SDMMC1, 15);
	SDCard1.Block_Size = 512;
	SDCard1.storage_method = IOMethod::DMA;
	ploginfo("[SD] buffers ...");
	stduint* probe = (stduint*)mempool.allocate(512, 9);
	byte* fat_buf = (byte*)mempool.allocate(512, 9);
	if (!probe || !fat_buf) {
		plogerro("[SD] no memory for the FAT buffers");
		return;
	}
	byte* sec_buf = (byte*)probe;
	// the boot sector of a FAT volume starts with the jump EB 58 90
	for (stduint i = 0; i < 2; i++) {
		MemSet(sec_buf, 0x22, 512);
		bool ok = SDCard1.Read(0, sec_buf, 1);
		ploginfo("[SD] read%u ok=%u data=%08X %08X %08X",
			(unsigned)i, (unsigned)ok, probe[0], probe[1], probe[2]);
	}
	// fat_type 0 = auto detect. loadfs() reads LBA 0 as the boot sector, so this
	// covers cards that carry the FAT boot sector directly, without a partition table
	uni::FilesysFAT* fs = new uni::FilesysFAT(0, SDCard1, sec_buf, fat_buf);
	ploginfo("[SD] loadfs ...");
	if (fs->loadfs()) {
		fs->allow_allocate = true;
		if (Filesys::MountFilesys(fs, &fs_fat, "/mnt/sd0.0")) {
			ploginfo("[SD] mounted FAT%u at /mnt/sd0.0, units=%u block=%u",
				(unsigned)fs->fat_type, SDCard1.getUnits(), SDCard1.Block_Size);
			return;
		}
		plogerro("[SD] MountFilesys failed");
		return;
	}
	plogerro("[SD] loadfs err=%u units=%u lba0=%08X %08X %08X %08X %08X",
		(unsigned)fs->error_number, SDCard1.getUnits(), probe[0], probe[1], probe[2], probe[3], probe[4]);
	delete fs;
	// partitioned cards: the generic probe walks the MBR partition table through
	// the same card object
	if (Filesys::Mount(SDCard1, 0, "/mnt/sd0.0")) {
		ploginfo("[SD] mounted FAT at /mnt/sd0.0, units=%u", SDCard1.getUnits());
		return;
	}
	plogerro("[SD] no FAT filesystem to mount");
}

void R_SDCARD_INIT() {
	// register only: the bring-up itself runs in the Devsman service thread
	if (!Systimex::AppendDeferredCallback(10, 0, &SdCardBringUp)) {
		plogerro("[SD] cannot append the bring-up action");
	}
}

#endif
