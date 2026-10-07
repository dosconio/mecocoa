// ASCII CPP-ISO11 TAB4 LF
// ModuTitle: Disk - Secure Digital Card (SDMMC1)
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"

#if (_MCCA & 0xFFFF) == 0x2032
#include <cpp/Device/SD.hpp>
#include <c/format/filesys.h>
#include <c/format/filesys/FAT.h>

_ESYM_C void R_SDCARD_INIT();

extern file_system_type fs_fat;

__attribute__((section(".init.rmod")))
extern const RMOD_LIST RMOD_LIST_SDCARD{
	R_SDCARD_INIT,
	"SD-SDMMC1",
	{}
};

#define SD_POLL_INTERVAL CONFIG_SysTickFreq
#define SD_OFFLINE_WAIT 4
#define SD_MAX_STRIKES 3

static Mutex sd_lock;
static bool sd_online = false;
static stduint sd_strikes = 0;

class SdCardStorage : public uni::StorageTrait {
public:
	uni::StorageTrait& base;
	bool online = false;
	SdCardStorage(uni::StorageTrait& stor) : base(stor) { Block_Size = stor.Block_Size; }
	void ioFailed() {
		if (++sd_strikes < SD_MAX_STRIKES) return;
		online = false;
		sd_online = false;
		sd_strikes = 0;
		SDCard1.canMode();
		plogwarn("[SD] the card stopped answering, /mnt/sd0.0 is off until it comes back");
	}
	virtual bool Read(stduint BlockIden, void* Dest, stduint Times = 1) override {
		MutexLocal guard(&sd_lock);
		if (!online) return false;
		if (base.Read(BlockIden, Dest, Times)) {
			sd_strikes = 0;
			return true;
		}
		ioFailed();
		return false;
	}
	virtual bool Write(stduint BlockIden, const void* Sors, stduint Times = 1) override {
		MutexLocal guard(&sd_lock);
		if (!online) return false;
		if (base.Write(BlockIden, Sors, Times)) {
			sd_strikes = 0;
			return true;
		}
		ioFailed();
		return false;
	}
	virtual stduint getUnits() override { return base.getUnits(); }
	virtual int operator[](uint64 bytid) override { return -1; }
};

static uni::FilesysFAT* sd_fs = nullptr;
static SdCardStorage* sd_storage = nullptr;
static byte* sd_sec_buf = nullptr;
static byte* sd_fat_buf = nullptr;
static stduint sd_cid[4] = {};
static bool sd_mounted = false;
static bool sd_poll_armed = false;
static bool sd_foreign = false;
static stduint sd_offline_wait = 0;

static void SdCardPoll(void*, ...);

static bool SdCardSameCard() {
	for0(i, 4) if (sd_cid[i] != SDCard1.CID[i]) return false;
	return true;
}

static void SdCardOnline(bool up) {
	MutexLocal guard(&sd_lock);
	if (sd_storage) sd_storage->online = up;
	sd_online = up;
}

static int SdCardInit() {
	MutexLocal guard(&sd_lock);
	if (!SDCard1.setMode()) return 0;
	NVIC.setPriority(IRQ_SDMMC1, 15);
	SDCard1.Block_Size = 512;
	SDCard1.storage_method = IOMethod::DMA;
	if (sd_mounted) {
		if (!SdCardSameCard()) return -1;
		if (sd_fs) sd_fs->current_sector = 0xFFFFFFFF;
		if (sd_storage) sd_storage->online = true;
		sd_online = true;
		sd_strikes = 0;
		ploginfo("[SD] card back online, units=%u block=%u", SDCard1.getUnits(), SDCard1.Block_Size);
		return 1;
	}
	for0(i, 4) sd_cid[i] = SDCard1.CID[i];
	return 2;
}

static bool SdCardMount() {
	int state = SdCardInit();
	if (state == 1) return true;
	if (state < 0) {
		if (!sd_foreign) {
			sd_foreign = true;
			plogwarn("[SD] another card is in the slot, only the mounted one is accepted");
		}
		return false;
	}
	if (!state) {
		sd_foreign = false;
		return false;
	}
	if (!sd_sec_buf) sd_sec_buf = (byte*)mempool.allocate(512, 9);
	if (!sd_fat_buf) sd_fat_buf = (byte*)mempool.allocate(512, 9);
	if (!sd_sec_buf || !sd_fat_buf) {
		plogerro("[SD] no memory for the FAT buffers");
		return false;
	}
	if (!sd_storage) sd_storage = new SdCardStorage(SDCard1);
	uni::FilesysFAT* fs = new uni::FilesysFAT(0, *sd_storage, sd_sec_buf, sd_fat_buf);
	{
		MutexLocal guard(&sd_lock);
		sd_fs = fs;
	}
	SdCardOnline(true);
	if (fs->loadfs()) {
		fs->allow_allocate = true;
		if (Filesys::MountFilesys(fs, &fs_fat, "/mnt/sd0.0")) {
			sd_mounted = true;
			ploginfo("[SD] mounted FAT%u at /mnt/sd0.0, units=%u block=%u",
				(unsigned)fs->fat_type, SDCard1.getUnits(), SDCard1.Block_Size);
			return true;
		}
		plogerro("[SD] MountFilesys failed");
	}
	else {
		plogerro("[SD] loadfs err=%u units=%u", (unsigned)fs->error_number, SDCard1.getUnits());
	}
	SdCardOnline(false);
	delete fs;
	{
		MutexLocal guard(&sd_lock);
		sd_fs = nullptr;
	}
	return false;
}

static stduint sd_last_work = 0;

static void SdCardPoll(void*, ...) {
	stduint now = tick;
	if (!sd_last_work || (now - sd_last_work) >= CONFIG_SysTickFreq) {
		sd_last_work = now;
		if (sd_online) {
			sd_offline_wait = 0;
		}
		else if (sd_offline_wait) {
			--sd_offline_wait;
		}
		else if (!SdCardMount()) {
			sd_offline_wait = SD_OFFLINE_WAIT;
		}
	}
	sd_poll_armed = Systimex::AppendDeferredCallback(SD_POLL_INTERVAL, 0, (_tocall_ft)SdCardPoll);
	if (!sd_poll_armed) plogerro("[SD] cannot re-arm the card polling");
}

void R_SDCARD_INIT() {
	if (!Systimex::AppendDeferredCallback(10, 0, (_tocall_ft)SdCardPoll)) {
		plogerro("[SD] cannot append the bring-up action");
	}
}

#endif
