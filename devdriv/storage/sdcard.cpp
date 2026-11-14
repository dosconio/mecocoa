// ASCII CPP-ISO11 TAB4 LF
// ModuTitle: Disk - Secure Digital Card (SDMMC1)
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"

#if (_MCCA & 0xFFFF) == 0x2032
#include <cpp/Device/SD.hpp>
#include <c/format/filesys/FAT.h>
#if defined(_MCU_STM32H7x)
#include "../../prehost/stm32h743X-ac7m/unicfg.hpp"
#endif

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

#if defined(_MCU_STM32H7x)
static DeviceNode* sd_host_node = nullptr;
static DeviceNode* sd_card_node = nullptr;

static void SdmmcUpdateClock(DeviceNode* node, const SDMMC_t& host) {
	if (node) node->fields.rcc_state = host.isAble() ? DeviceRccState::Enabled : DeviceRccState::Disabled;
}

static DeviceNode* SdmmcRegisterHost(const char* name, SDMMC_t& host, bool configured) {
	auto* node = Devsman::RegisterPlatformDevice(name, configured ? "stm32-sdmmc" : nullptr, &host);
	if (!node) return nullptr;
	SdmmcUpdateClock(node, host);
	if (!configured) return node;

	constexpr auto register_size = (_IMM(SDReg::IPVR) + 1) * sizeof(uint32);
	const bool ready = Devsman::AddMemoryResource(node, 0, host.getAddress(), register_size) &&
		Devsman::AddResource(node, DeviceResource{uint16(DeviceResourceType::IrqLine),
			DeviceResourceFlag_None, 0, uint64(_IMM(host.getRequest())), 1, 0});
	if (ready) return node;
	Devsman::UnregisterDevice(node);
	return nullptr;
}
#endif

class SdCardStorage : public uni::StorageTrait {
public:
	uni::StorageTrait& base;
	bool online = false;
	SdCardStorage(uni::StorageTrait& stor) : base(stor) { Block_Size = stor.Block_Size; }
#if defined(_MCU_STM32H7x)
	HD_Info partitions{};
	bool layout_ready = false;
	bool scanning_layout = false;
	bool read_failed = false;
	uni::PartitionSlice getSlice(stduint dev) override {
		return GetPartitionSlice(partitions, dev);
	}
#endif
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
#if defined(_MCU_STM32H7x)
		if (scanning_layout && (BlockIden >= base.getUnits() || Times > base.getUnits() - BlockIden)) {
			MemSet(Dest, 0, Times * Block_Size);
			return false;
		}
#endif
		if (!online) return false;
		if (base.Read(BlockIden, Dest, Times)) {
			sd_strikes = 0;
			return true;
		}
#if defined(_MCU_STM32H7x)
		if (scanning_layout) {
			read_failed = true;
			MemSet(Dest, 0, Times * Block_Size);
		}
#endif
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

#if defined(_MCU_STM32H7x)
static void SdCardSetNodeState(DriverBindingState state) {
	if (!sd_card_node) return;
	sd_card_node->fields.binding.state = _IMM(state);
	for (auto* child = sd_card_node->link.subf; child; child = child->next)
		cast<DeviceNode*>(child)->fields.binding.state = _IMM(state);
}

static void SdCardRemoveNodes() {
	if (!sd_card_node) return;
	// A mounted volume keeps its source node and partition adapter across card recovery.
	if (sd_mounted) {
		SdCardSetNodeState(DriverBindingState::Failed);
		return;
	}
	const bool interrupts_enabled = IC.TryMaskInterrupt();
	while (auto* child = cast<DeviceNode*>(sd_card_node->link.subf)) {
		auto* part = static_cast<uni::StorageTrait*>(child->fields.binding.driver_data);
		if (!Devsman::UnregisterDevice(child)) break;
		delete part;
	}
	if (Devsman::UnregisterDevice(sd_card_node)) sd_card_node = nullptr;
	if (interrupts_enabled) IC.enInterrupt(true);
}

static void SdCardRegisterNode() {
	if (!sd_host_node || sd_card_node) return;
	const bool interrupts_enabled = IC.TryMaskInterrupt();
	sd_card_node = Devsman::RegisterStorageDevice(sd_host_node, "card@0",
		DeviceBusType::Platform, "sdcard", static_cast<uni::StorageTrait*>(sd_storage));
	if (sd_card_node) {
		Devsman::AddConfigurationResource(sd_card_node, 0, "block-size", sd_storage->Block_Size);
		Devsman::AddConfigurationResource(sd_card_node, 1, "block-count", sd_storage->getUnits());
	}
	if (interrupts_enabled) IC.enInterrupt(true);
}

static bool SdCardHasFatBootSector(const byte* sector, stduint blocks) {
	const auto& boot = *reinterpret_cast<const FAT_BootSector32*>(sector);
	if ((sector[510] != 0x55 || sector[511] != 0xAA) ||
		(boot.jmpBoot[0] != 0xEB && boot.jmpBoot[0] != 0xE9) ||
		uint16(boot.bytes_per_sector) != 512 || !boot.reserved_sectors || !boot.fat_count ||
		!boot.sectors_per_cluster || boot.sectors_per_cluster > 128 ||
		(boot.sectors_per_cluster & (boot.sectors_per_cluster - 1))) return false;
	const uint32 sectors = boot.total_sectors_16 ? uint16(boot.total_sectors_16) : boot.total_sectors_32;
	const uint32 fat_sectors = boot.sectors_per_fat_16 ? boot.sectors_per_fat_16 : boot.sectors_per_fat_32;
	const uint64 first_data = boot.reserved_sectors + uint64(boot.fat_count) * fat_sectors +
		(uint32(uint16(boot.root_entries)) * 32 + 511) / 512;
	return sectors && sectors <= blocks && fat_sectors && first_data < sectors;
}

static bool SdCardScanLayout() {
	auto& storage = *sd_storage;
	auto& table = storage.partitions;
	table.ResetUnifiedParts();
	table.whole_disk = {};
	table.whole_disk.length = storage.getUnits();
	table.scheme_kind = PartitionSchemeKind::Unknown;
	if (!storage.Read(0, sd_sec_buf)) return false;
	if (SdCardHasFatBootSector(sd_sec_buf, table.whole_disk.length)) return true;
	if (sd_sec_buf[510] != 0x55 || sd_sec_buf[511] != 0xAA) return true;

	storage.read_failed = false;
	storage.scanning_layout = true;
	uni::DiscPartition::Partition(storage, table, sd_sec_buf, 0);
	storage.scanning_layout = false;
	if (storage.read_failed) return false;
	for (stduint part = 1; part <= table.part_count; ++part) {
		auto& slice = table.parts[part - 1];
		if (!slice.address || slice.address >= table.whole_disk.length ||
			slice.length > table.whole_disk.length - slice.address ||
			slice.sys_id == ClassicFilesysType::EXT || slice.sys_id == ClassicFilesysType::GPT_PROTECTIVE) slice = {};
	}
	return true;
}
#endif

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
#if defined(_MCU_STM32H7x)
	if (!ConfigDeviceGPIN(DeviceType::Sdmmc1)) return 0;
	SDCard1.auto_setmode_apio = false;
	const bool ready = SDCard1.setMode();
	SdmmcUpdateClock(sd_host_node, SDCard1);
	if (!ready) return 0;
#else
	if (!SDCard1.setMode()) return 0;
#endif
	NVIC.setPriority(IRQ_SDMMC1, 6);// above SVCall(15): the blocking SD read spins inside that handler, an equal priority irq would never run there
	SDCard1.Block_Size = 512;
	SDCard1.storage_method = IOMethod::DMA;
	if (sd_mounted) {
		if (!SdCardSameCard()) return -1;
		if (sd_fs) sd_fs->current_sector = 0xFFFFFFFF;
		if (sd_storage) sd_storage->online = true;
		sd_online = true;
		sd_strikes = 0;
#if defined(_MCU_STM32H7x)
		SdCardSetNodeState(DriverBindingState::Started);
#endif
		ploginfo("[SD] card back online, units=%u block=%u", SDCard1.getUnits(), SDCard1.Block_Size);
		return 1;
	}
	for0(i, 4) sd_cid[i] = SDCard1.CID[i];
	return 2;
}

static bool SdCardMountVolume(uni::StorageTrait& volume, DeviceNode* source, stduint part) {
	auto* fs = new uni::FilesysFAT(0, volume, sd_sec_buf, sd_fat_buf);
	if (!fs) return false;
	if (!fs->loadfs()) {
		plogtrac("[SD] loadfs err=%u partition=%u", (unsigned)fs->error_number, (unsigned)part);
		delete fs;
		return false;
	}
	fs->allow_allocate = true;
	if (!Filesys::MountFilesys(fs, &fs_fat, "/mnt/sd0.0", source, part,
		VFSMountOwnership::Filesystem)) {
		plogerro("[SD] MountFilesys failed");
		return false;
	}
	sd_fs = fs;
	sd_mounted = true;
	ploginfo("[SD] mounted FAT%u at /mnt/sd0.0, units=%u block=%u",
		(unsigned)fs->fat_type, SDCard1.getUnits(), SDCard1.Block_Size);
	return true;
}

#if defined(_MCU_STM32H7x)
static bool SdCardMountLayout() {
	auto& storage = *sd_storage;
	SdCardRegisterNode();
	if (!storage.layout_ready) storage.layout_ready = SdCardScanLayout();
	if (!storage.layout_ready) return false;
	auto& table = storage.partitions;
	if (!table.part_count) {
		if (!storage.Read(0, sd_sec_buf) || !SdCardHasFatBootSector(sd_sec_buf, table.whole_disk.length))
			return false;
		return SdCardMountVolume(storage, sd_card_node, 0);
	}
	if (!sd_card_node) return false;
	bool mounted = false;
	for (stduint part = 1; part <= table.part_count; ++part) {
		const auto slice = storage.getSlice(part);
		if (!slice.length || slice.sys_id == ClassicFilesysType::None) continue;
		const auto name = String::newFormat("partition@%u", part);
		const bool interrupts_enabled = IC.TryMaskInterrupt();
		auto* node = Devsman::RegisterStoragePartition(sd_card_node, name.reference(), storage, part);
		if (node) {
			Devsman::AddConfigurationResource(node, 0, "start-lba", slice.address);
			Devsman::AddConfigurationResource(node, 1, "block-count", slice.length);
		}
		if (interrupts_enabled) IC.enInterrupt(true);
		if (!node || mounted || !sd_online) continue;
		auto& volume = *static_cast<uni::StorageTrait*>(node->fields.binding.driver_data);
		if (volume.Read(0, sd_sec_buf) && SdCardHasFatBootSector(sd_sec_buf, slice.length))
			mounted = SdCardMountVolume(volume, node, part);
	}
	return mounted;
}
#endif

static bool SdCardMount() {
#if defined(_MCU_STM32H7x)
	if (sd_online) return SdCardMountLayout();
#endif
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
	if (!sd_storage) return false;
#if defined(_MCU_STM32H7x)
	sd_storage->layout_ready = false;
#endif
	SdCardOnline(true);
#if defined(_MCU_STM32H7x)
	return SdCardMountLayout();
#else
	if (SdCardMountVolume(*sd_storage, nullptr, 0)) return true;
	SdCardOnline(false);
	return false;
#endif
}

static stduint sd_last_work = 0;

static void SdCardPoll(void*, ...) {
	stduint now = tick;
	if (!sd_last_work || (now - sd_last_work) >= CONFIG_SysTickFreq) {
		sd_last_work = now;
#if defined(_MCU_STM32H7x)
		if (!sd_online) SdCardRemoveNodes();
		const bool ready = sd_online && sd_mounted;
#else
		const bool ready = sd_online;
#endif
		if (ready) {
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
#if defined(_MCU_STM32H7x)
	sd_host_node = SdmmcRegisterHost("sdmmc1", SDCard1, true);
	if (!sd_host_node) plogwarn("[SD] SDMMC1 node registration failed");
	if (!SdmmcRegisterHost("sdmmc2", SDCard2, false)) plogwarn("[SD] SDMMC2 node registration failed");
#endif
	if (!Systimex::AppendDeferredCallback(10, 0, (_tocall_ft)SdCardPoll)) {
		plogerro("[SD] cannot append the bring-up action");
	}
}

#endif
