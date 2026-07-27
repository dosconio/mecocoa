// ASCII g++ TAB4 LF
// ModuTitle: Disk - SCSI
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include <c/storage/harddisk.h>
#include <c/format/filesys.h>
#include <cpp/Device/Bus/PCI.hpp>

#if _MCCA == 0x8632

namespace {
	enum : byte {
		BLOGIC_CNTRL_REG = 0,
		BLOGIC_STATUS_REG = 0,
		BLOGIC_CMD_PARM_REG = 1,
		BLOGIC_DATAIN_REG = 1,
		BLOGIC_INT_REG = 2,
		BLOGIC_GET_BOARD_ID = 0x04,
		BLOGIC_INQ_DEV0TO7 = 0x0A,
		BLOGIC_INQ_CONFIG = 0x0B,
		BLOGIC_INQ_SETUPINFO = 0x0D,
		BLOGIC_INQ_DEV8TO15 = 0x23,
		BLOGIC_INQ_DEV = 0x24,
		BLOGIC_EXEC_MBOX_CMD = 0x02,
		BLOGIC_INIT_EXT_MBOX = 0x81,
		BLOGIC_SETCCB_FMT = 0x96,
		BLOGIC_INQ_FWVER_D3 = 0x84,
		BLOGIC_INQ_FWVER_LETTER = 0x85,
		BLOGIC_INQ_PCI_INFO = 0x86,
		BLOGIC_INQ_MODELNO = 0x8B,
		BLOGIC_INQ_EXTSETUP = 0x8D,
	};

	enum : byte {
		BLOGIC_CTRL_INT_RESET = 1u << 5,
		BLOGIC_CTRL_SOFT_RESET = 1u << 6,
		BLOGIC_CTRL_HARD_RESET = 1u << 7,
	};

	enum : byte {
		BLOGIC_STAT_CMD_INVALID = 1u << 0,
		BLOGIC_STAT_DATAIN_READY = 1u << 2,
		BLOGIC_STAT_CMD_BUSY = 1u << 3,
		BLOGIC_STAT_ADAPTER_READY = 1u << 4,
		BLOGIC_STAT_INIT_REQUIRED = 1u << 5,
		BLOGIC_STAT_DIAG_FAILED = 1u << 6,
		BLOGIC_STAT_DIAG_ACTIVE = 1u << 7,
	};

	enum : byte {
		BLOGIC_INT_CMD_COMPLETE = 1u << 2,
	};

	enum : byte {
		BLOGIC_MBOX_START = 0x01,
		BLOGIC_INBOX_FREE = 0x00,
		BLOGIC_CMD_COMPLETE_GOOD = 0x01,
		BLOGIC_CMD_ABORT_BY_HOST = 0x02,
		BLOGIC_CMD_NOTFOUND = 0x03,
		BLOGIC_CMD_COMPLETE_ERROR = 0x04,
		BLOGIC_INVALID_CCB = 0x05,
		BLOGIC_CCB_INITIATOR = 0x00,
		BLOGIC_DIR_DATAIN = 0x01,
		BLOGIC_DIR_DATAOUT = 0x02,
		BLOGIC_DIR_NOTX = 0x03,
		BLOGIC_LEGACY_LUN_CCB = 0x00,
		BLOGIC_EXT_LUN_CCB = 0x01,
		BLOGIC_ADAPTER_STATUS_OK = 0x00,
		BLOGIC_TARGET_STATUS_GOOD = 0x00,
		SCSI_CMD_INQUIRY = 0x12,
		SCSI_CMD_READ_CAPACITY10 = 0x25,
	};

	struct BusLogicBoardId {
		byte type = 0;
		byte custom_features = 0;
		byte fw_ver_digit1 = 0;
		byte fw_ver_digit2 = 0;
	};

	struct BusLogicConfig {
		byte byte0 = 0;
		byte byte1 = 0;
		byte byte2 = 0;
	};

	struct BusLogicSetupInfo {
		byte bytes[34] = {};
	};

	struct BusLogicExtSetup {
		byte bytes[14] = {};
	};

	struct BusLogicPciInfo {
		byte isa_port = 0;
		byte irq_channel = 0;
		byte termination = 0;
		byte reserved = 0;
	};

	_PACKED(struct) BusLogicExtMailboxInitReq {
		byte mbox_count;
		uint32 base_mbox_addr;
	};

	_PACKED(struct) BusLogicOutbox {
		uint32 ccb;
		byte reserved[3];
		byte action;
	};

	_PACKED(struct) BusLogicInbox {
		uint32 ccb;
		byte adapter_status;
		byte target_status;
		byte reserved;
		byte completion_code;
	};

	_PACKED(struct) BusLogicCcb32 {
		byte opcode;
		byte control;
		byte cdb_length;
		byte sense_length;
		uint32 data_length;
		uint32 data_addr;
		byte reserved12;
		byte reserved13;
		byte adapter_status;
		byte target_status;
		byte target_id;
		byte lun_legacy;
		byte cdb[12];
		byte reserved30;
		byte reserved31;
		uint32 host_reserved;
		uint32 sense_addr;
	};

	_PACKED(struct) ScsiInquiryReply {
		byte peripheral;
		byte rmb;
		byte version;
		byte response_format;
		byte additional_length;
		byte flags5;
		byte flags6;
		byte flags7;
		char vendor[8];
		char product[16];
		char revision[4];
	};

	_PACKED(struct) ScsiReadCapacity10Reply {
		byte last_lba[4];
		byte block_size[4];
	};

	struct DmaRegion {
		byte* virt = nullptr;
		uint32 phys = 0;
		stduint size = 0;

		bool Ensure(stduint want_size) {
			if (virt && size >= want_size) return true;
			byte* buf = (byte*)mempool.allocate(want_size, 12);
			if (!buf) return false;
			virt = buf;
			phys = uint32(_IMM(buf));
			size = want_size;
			MemSet(virt, 0, want_size);
			return true;
		}
	};

	static void scsi_io_pause() {
		for (volatile stduint spin = 0; spin < 256; ++spin) {
			_ASM volatile("" ::: "memory");
		}
	}

	struct ScsiController {
		static constexpr byte kControllerSlot = 0;
		static constexpr byte kMaxTargets = 16;
		static constexpr byte kMaxLuns = 8;
		static constexpr stduint kMaxDiskSlots = 8;
		static constexpr stduint kMaxCdromSlots = 4;

		struct DiskSlot {
			bool used = false;
			byte target = 0;
			byte lun = 0;
			DeviceNode* node = nullptr;
			uni::Harddisk_SCSI disk = {};
		};

		struct CdromSlot {
			bool used = false;
			byte target = 0;
			byte lun = 0;
			DeviceNode* node = nullptr;
			uni::CDROM_SCSI cdrom = {};
		};

		DeviceNode* node = nullptr;
		const DeviceResource* bar0_io = nullptr;
		const DeviceResource* bar0_mmio = nullptr;
		const DeviceResource* irq = nullptr;
		word io_base = 0;
		BusLogicOutbox* outboxes = nullptr;
		BusLogicInbox* inboxes = nullptr;
		stduint mailbox_count = 0;
		stduint next_outbox_index = 0;
		DmaRegion mailbox_region{};
		DmaRegion probe_ccb_region{};
		DmaRegion probe_data_region{};
		DmaRegion probe_sense_region{};
		byte* sector_buf = nullptr;
		stduint sector_buf_size = 0;
		DiskSlot disk_slots[kMaxDiskSlots] = {};
		CdromSlot cdrom_slots[kMaxCdromSlots] = {};

		bool has_io() const { return io_base != 0; }

		byte ReadStatus() const {
			return innpb(io_base + BLOGIC_STATUS_REG);
		}

		byte ReadInterrupt() const {
			return innpb(io_base + BLOGIC_INT_REG);
		}

		byte ReadDataIn() const {
			return innpb(io_base + BLOGIC_DATAIN_REG);
		}

		void WriteControl(byte value) const {
			outpb(io_base + BLOGIC_CNTRL_REG, value);
		}

		void WriteCommand(byte value) const {
			outpb(io_base + BLOGIC_CMD_PARM_REG, value);
		}

		void InterruptReset() const {
			WriteControl(BLOGIC_CTRL_INT_RESET);
		}

		void ClearAdapterInterruptState() const {
			byte intreg = ReadInterrupt();
			if (intreg & 0x80u) {
				InterruptReset();
			}
		}

		void ClearLocalCommandState() const {
			stduint drain_limit = 64;
			while (drain_limit--) {
				byte status = ReadStatus();
				if ((status & BLOGIC_STAT_DATAIN_READY) == 0) break;
				(void)ReadDataIn();
			}
			ClearAdapterInterruptState();
		}

		bool HardwareReset(bool hard_reset) {
			if (!has_io()) return false;

			WriteControl(hard_reset ? BLOGIC_CTRL_HARD_RESET : BLOGIC_CTRL_SOFT_RESET);

			stduint timeout = 50000;
			while (timeout--) {
				byte status = ReadStatus();
				if (status & BLOGIC_STAT_DIAG_ACTIVE) break;
				scsi_io_pause();
			}
			if (!timeout) return false;

			timeout = 100000;
			while (timeout--) {
				byte status = ReadStatus();
				if ((status & BLOGIC_STAT_DIAG_ACTIVE) == 0) break;
				scsi_io_pause();
			}
			if (!timeout) return false;

			timeout = 10000;
			while (timeout--) {
				byte status = ReadStatus();
				if (status & (BLOGIC_STAT_DIAG_FAILED | BLOGIC_STAT_ADAPTER_READY | BLOGIC_STAT_DATAIN_READY)) {
					if ((status & BLOGIC_STAT_DIAG_FAILED) || !(status & BLOGIC_STAT_ADAPTER_READY)) {
						return false;
					}
					if (status & BLOGIC_STAT_DATAIN_READY) {
						(void)ReadDataIn();
					}
					ploginfo("[SCSI] reset %s ok status=%[8H]",
						hard_reset ? "hard" : "soft", (unsigned)status);
					return true;
				}
				scsi_io_pause();
			}
			return false;
		}

		bool WaitUntilReady(stduint limit = 100000) const {
			while (limit--) {
				byte status = ReadStatus();
				if ((status & (BLOGIC_STAT_ADAPTER_READY | BLOGIC_STAT_CMD_BUSY)) == BLOGIC_STAT_ADAPTER_READY) {
					return true;
				}
				scsi_io_pause();
			}
			return false;
		}

		int Command(byte opcode, const void* param_data, stduint param_len, void* reply_data, stduint reply_len) const {
			if (!has_io()) return -1;
			ClearLocalCommandState();
			if (!WaitUntilReady()) return -2;

			const byte* param = reinterpret_cast<const byte*>(param_data);
			byte* reply = reinterpret_cast<byte*>(reply_data);
			stduint reply_bytes = 0;
			WriteCommand(opcode);

			stduint param_timeout = 100000;
			while (param_len && param_timeout--) {
				scsi_io_pause();
				byte intreg = ReadInterrupt();
				byte status = ReadStatus();
				if (intreg & BLOGIC_INT_CMD_COMPLETE) break;
				if (status & BLOGIC_STAT_DATAIN_READY) break;
				if (status & BLOGIC_STAT_CMD_BUSY) continue;
				WriteCommand(*param++);
				--param_len;
			}
			if (param_len) {
				ClearLocalCommandState();
				return -3;
			}

			stduint reply_timeout = 100000;
			if (opcode == BLOGIC_INQ_DEV0TO7 ||
				opcode == BLOGIC_INQ_DEV8TO15 ||
				opcode == BLOGIC_INQ_DEV) {
				reply_timeout = 6000000;
			}
			while (reply_timeout--) {
				byte intreg = ReadInterrupt();
				byte status = ReadStatus();
				if (status & BLOGIC_STAT_DATAIN_READY) {
					byte value = ReadDataIn();
					if (reply && reply_bytes < reply_len) {
						reply[reply_bytes] = value;
					}
					++reply_bytes;
					continue;
				}
				if (intreg & BLOGIC_INT_CMD_COMPLETE) {
					InterruptReset();
					if (status & BLOGIC_STAT_CMD_INVALID) {
						ClearLocalCommandState();
						return -4;
					}
					return int(reply_bytes);
				}
				scsi_io_pause();
			}
			ClearLocalCommandState();
			return -5;
		}

		bool QueryBoardId(BusLogicBoardId& out) const {
			MemSet(&out, 0, sizeof(out));
			return Command(BLOGIC_GET_BOARD_ID, nullptr, 0, &out, sizeof(out)) == sizeof(out);
		}

		bool QueryConfig(BusLogicConfig& out) const {
			MemSet(&out, 0, sizeof(out));
			return Command(BLOGIC_INQ_CONFIG, nullptr, 0, &out, sizeof(out)) == sizeof(out);
		}

		bool QuerySetupInfo(BusLogicSetupInfo& out) const {
			byte reply_len = sizeof(out.bytes);
			MemSet(&out, 0, sizeof(out));
			return Command(BLOGIC_INQ_SETUPINFO, &reply_len, 1, out.bytes, sizeof(out.bytes)) == sizeof(out.bytes);
		}

		bool QueryExtSetup(BusLogicExtSetup& out) const {
			byte reply_len = sizeof(out.bytes);
			MemSet(&out, 0, sizeof(out));
			return Command(BLOGIC_INQ_EXTSETUP, &reply_len, 1, out.bytes, sizeof(out.bytes)) == sizeof(out.bytes);
		}

		bool QueryPciInfo(BusLogicPciInfo& out) const {
			MemSet(&out, 0, sizeof(out));
			return Command(BLOGIC_INQ_PCI_INFO, nullptr, 0, &out, sizeof(out)) == sizeof(out);
		}

		bool QueryFirmwareExtra(byte& digit3, byte& letter) const {
			digit3 = 0;
			letter = 0;
			const bool ok3 = Command(BLOGIC_INQ_FWVER_D3, nullptr, 0, &digit3, 1) == 1;
			const bool ok4 = Command(BLOGIC_INQ_FWVER_LETTER, nullptr, 0, &letter, 1) == 1;
			return ok3 && ok4;
		}

		bool QueryModelNumber(char model[6]) const {
			MemSet(model, 0, 6);
			return Command(BLOGIC_INQ_MODELNO, nullptr, 0, model, 5) == 5;
		}

		bool QueryInstalledTargets(uint16& out_mask) const {
			out_mask = 0;
			return Command(BLOGIC_INQ_DEV, nullptr, 0, &out_mask, sizeof(out_mask)) == sizeof(out_mask);
		}

		bool QueryInstalledTargetsLegacy(byte out_luns0to7[8]) const {
			MemSet(out_luns0to7, 0, 8);
			return Command(BLOGIC_INQ_DEV0TO7, nullptr, 0, out_luns0to7, 8) == 8;
		}

		bool InitializeMailbox(byte requested_count) {
			if (!has_io()) return false;
			if (mailbox_region.virt && mailbox_count == requested_count) return true;

			const stduint total_size =
				stduint(requested_count) * (sizeof(BusLogicOutbox) + sizeof(BusLogicInbox));
			if (!mailbox_region.Ensure(total_size)) return false;

			MemSet(mailbox_region.virt, 0, total_size);
			outboxes = reinterpret_cast<BusLogicOutbox*>(mailbox_region.virt);
			inboxes = reinterpret_cast<BusLogicInbox*>(
				mailbox_region.virt + requested_count * sizeof(BusLogicOutbox));

			BusLogicExtMailboxInitReq req{};
			req.mbox_count = requested_count;
			req.base_mbox_addr = mailbox_region.phys;
			if (Command(BLOGIC_INIT_EXT_MBOX, &req, sizeof(req), nullptr, 0) < 0) {
				return false;
			}
			mailbox_count = requested_count;
			next_outbox_index = 0;
			ploginfo("[SCSI] mailbox init count=%u base=%[32H]",
				(unsigned)mailbox_count, req.base_mbox_addr);
			return true;
		}

		bool SetCcbFormat(byte fmt) const {
			return Command(BLOGIC_SETCCB_FMT, &fmt, 1, nullptr, 0) >= 0;
		}

		bool EnsureProbeBuffers() {
			if (!probe_ccb_region.Ensure(512)) return false;
			if (!probe_data_region.Ensure(512)) return false;
			if (!probe_sense_region.Ensure(256)) return false;
			MemSet(probe_ccb_region.virt, 0, 512);
			MemSet(probe_data_region.virt, 0, 512);
			MemSet(probe_sense_region.virt, 0, 256);
			return true;
		}

		bool EnsureSectorBuffer(stduint block_size) {
			if (!block_size) return false;
			if (!sector_buf || sector_buf_size < block_size) {
				sector_buf = (byte*)mempool.allocate(block_size, 12);
				if (!sector_buf) return false;
				sector_buf_size = block_size;
			}
			MemSet(sector_buf, 0, block_size);
			return true;
		}

		static bool ExecScsiCommandThunk(void* context,
			byte target_id, byte lun,
			const byte* cdb, byte cdb_length,
			void* data_buf, stduint data_length, bool data_in,
			byte& completion_code, byte& adapter_status, byte& target_status) {
			auto* ctlr = reinterpret_cast<ScsiController*>(context);
			if (!ctlr) return false;
			return ctlr->ExecuteMailboxScsi(target_id, lun, cdb, cdb_length,
				data_buf, data_length, data_in,
				completion_code, adapter_status, target_status);
		}

		bool ExecuteMailboxScsi(byte target_id, byte lun, const byte* cdb, byte cdb_length,
			void* data_buf, stduint data_length, bool data_in,
			byte& completion_code, byte& adapter_status, byte& target_status) {
			if (!mailbox_count || !outboxes || !inboxes || !EnsureProbeBuffers()) return false;
			if (!cdb || cdb_length == 0 || cdb_length > 12) return false;
			ClearLocalCommandState();
			ClearAdapterInterruptState();
			if (!WaitUntilReady()) {
				plogwarn("[SCSI] exec mailbox while adapter not ready status=%[8H] int=%[8H]",
					(unsigned)ReadStatus(), (unsigned)ReadInterrupt());
				return false;
			}

			const stduint outbox_index = next_outbox_index % mailbox_count;
			auto* outbox = &outboxes[outbox_index];
			auto* probe_ccb = reinterpret_cast<BusLogicCcb32*>(probe_ccb_region.virt);
			byte* probe_sense = probe_sense_region.virt;
			uint32 data_phys = 0;
			if (data_buf) {
				if (data_buf == probe_data_region.virt) data_phys = probe_data_region.phys;
				else data_phys = uint32(_IMM(data_buf));
			}
			MemSet(outbox, 0, sizeof(*outbox));
			for0(i, mailbox_count) {
				MemSet(&inboxes[i], 0, sizeof(inboxes[i]));
			}
			MemSet(probe_ccb, 0, sizeof(*probe_ccb));

			probe_ccb->opcode = BLOGIC_CCB_INITIATOR;
			probe_ccb->control = byte((data_length == 0 ? BLOGIC_DIR_NOTX :
				(data_in ? BLOGIC_DIR_DATAIN : BLOGIC_DIR_DATAOUT)) << 3);
			probe_ccb->cdb_length = cdb_length;
			probe_ccb->sense_length = 18;
			probe_ccb->data_length = uint32(data_length);
			probe_ccb->data_addr = data_phys;
			probe_ccb->target_id = target_id;
			probe_ccb->lun_legacy = byte(lun & 0x1Fu);
			MemCopyN(probe_ccb->cdb, cdb, cdb_length);
			probe_ccb->sense_addr = probe_sense_region.phys;

			outbox->ccb = probe_ccb_region.phys;
			outbox->action = BLOGIC_MBOX_START;
			next_outbox_index = (outbox_index + 1) % mailbox_count;
			// ploginfo("[SCSI] exec ccb=%[32H] data=%[32H] sense=%[32H] outbox=%[32H]",
			// 	(unsigned)probe_ccb_region.phys,
			// 	(unsigned)data_phys,
			// 	(unsigned)probe_sense_region.phys,
			// 	(unsigned)(mailbox_region.phys + stduint((byte*)outbox - mailbox_region.virt)));
			_ASM volatile("" ::: "memory");
			WriteCommand(BLOGIC_EXEC_MBOX_CMD);

			stduint timeout = 1000000;
			while (timeout--) {
				for0(i, mailbox_count) {
					auto* inbox = &inboxes[i];
					if (inbox->completion_code == BLOGIC_INBOX_FREE) continue;
					if (inbox->ccb != probe_ccb_region.phys) continue;
					completion_code = inbox->completion_code;
					adapter_status = inbox->adapter_status;
					target_status = inbox->target_status;
					ClearAdapterInterruptState();
					inbox->completion_code = BLOGIC_INBOX_FREE;
					outbox->action = 0;
					return true;
				}
				const byte intreg = ReadInterrupt();
				if (intreg & 0x80u) {
					InterruptReset();
					if ((intreg & BLOGIC_INT_CMD_COMPLETE) && outbox->action != 0) {
						byte diag[8] = {};
						stduint diag_len = 0;
						while (diag_len < numsof(diag) && (ReadStatus() & BLOGIC_STAT_DATAIN_READY)) {
							diag[diag_len++] = ReadDataIn();
						}
						plogwarn("[SCSI] exec cmd-complete before mailbox consume status=%[8H] diag0=%[8H] diag1=%[8H] diaglen=%u",
							(unsigned)ReadStatus(),
							(unsigned)diag[0],
							(unsigned)diag[1],
							(unsigned)diag_len);
						ClearLocalCommandState();
						return false;
					}
				}
				scsi_io_pause();
			}
			completion_code = 0;
			adapter_status = 0;
			target_status = 0;
			for0(i, mailbox_count) {
				auto* inbox = &inboxes[i];
				if (inbox->ccb != probe_ccb_region.phys) continue;
				completion_code = inbox->completion_code;
				adapter_status = inbox->adapter_status;
				target_status = inbox->target_status;
				break;
			}
			plogwarn("[SCSI] exec timeout status=%[8H] int=%[8H] outbox-action=%[8H]",
				(unsigned)ReadStatus(), (unsigned)ReadInterrupt(), (unsigned)outbox->action);
			ClearAdapterInterruptState();
			return false;
		}

		void RegisterStorageNode(DiskSlot& slot) {
			if (!node || slot.node) return;
			String name;
			if (slot.lun == 0) name.Format("scsi-disk@%u:%u", (stduint)kControllerSlot, (stduint)slot.target);
			else name.Format("scsi-disk@%u:%u:%u", (stduint)kControllerSlot, (stduint)slot.target, (stduint)slot.lun);
			slot.node = Devsman::RegisterStorageDevice(
				node, name.reference(), DeviceBusType::PCI, "scsi-disk", &slot.disk);
		}

		void RegisterCdromNode(CdromSlot& slot) {
			if (!node || slot.node) return;
			String name;
			if (slot.lun == 0) name.Format("scsi-cdrom@%u:%u", (stduint)kControllerSlot, (stduint)slot.target);
			else name.Format("scsi-cdrom@%u:%u:%u", (stduint)kControllerSlot, (stduint)slot.target, (stduint)slot.lun);
			slot.node = Devsman::RegisterStorageDevice(
				node, name.reference(), DeviceBusType::PCI, "scsi-cdrom", &slot.cdrom);
		}

		bool ReadSector0(uni::Harddisk_SCSI& disk, byte target, byte lun) {
			if (!EnsureSectorBuffer(disk.Block_Size)) return false;
			if (!disk.Read(0, sector_buf)) {
				plogwarn("[SCSI] READ(10) t%ul%u LBA0 failed",
					(unsigned)target, (unsigned)lun);
				return false;
			}
			return true;
		}

		void ParsePartitions(DiskSlot& slot) {
			if (slot.disk.hd_info_valid) return;
			DiscPartition::Partition(slot.disk, slot.disk.hd_info, sector_buf, 0);
			slot.disk.hd_info_valid = true;
		}

		void MountPartitions(DiskSlot& slot) {
			auto& hdinfo = slot.disk.hd_info;
			for (stduint part_dev = 1; part_dev <= hdinfo.part_count; ++part_dev) {
				uni::PartitionSlice slice = GetPartitionSlice(hdinfo, part_dev);
				if (slice.sys_id == 0x00) continue;
				if (slice.sys_id == FILESYS_EXT) continue;
				String lab;
				if (slot.lun == 0) lab.Format("/mnt/scsi%u.%u", (stduint)slot.target, part_dev);
				else lab.Format("/mnt/scsi%u-%u.%u", (stduint)slot.target, (stduint)slot.lun, part_dev);
				if (auto fs = Filesys::Mount(slot.disk, part_dev, lab.reference())) {
					ploginfo("[SCSI] mount %s on %s", fs->name, lab.reference());
				}
			}
		}

		void MountCdromWholeDisk(CdromSlot& slot) {
			String lab;
			if (slot.lun == 0) lab.Format("/mnt/scsi%u.0", (stduint)slot.target);
			else lab.Format("/mnt/scsi%u-%u.0", (stduint)slot.target, (stduint)slot.lun);
			if (auto fs = Filesys::Mount(slot.cdrom, 0, lab.reference())) {
				ploginfo("[SCSI] mount %s on %s", fs->name, lab.reference());
			}
			else {
				plogwarn("[SCSI] no filesystem recognized on scsi cdrom %u:%u",
					(stduint)slot.target, (stduint)slot.lun);
			}
		}

		DiskSlot* AcquireDiskSlot(byte target, byte lun) {
			for0(i, kMaxDiskSlots) {
				if (disk_slots[i].used &&
					disk_slots[i].target == target &&
					disk_slots[i].lun == lun) return &disk_slots[i];
			}
			for0(i, kMaxDiskSlots) {
				if (disk_slots[i].used) continue;
				disk_slots[i].used = true;
				disk_slots[i].target = target;
				disk_slots[i].lun = lun;
				return &disk_slots[i];
			}
			return nullptr;
		}

		CdromSlot* AcquireCdromSlot(byte target, byte lun) {
			for0(i, kMaxCdromSlots) {
				if (cdrom_slots[i].used &&
					cdrom_slots[i].target == target &&
					cdrom_slots[i].lun == lun) return &cdrom_slots[i];
			}
			for0(i, kMaxCdromSlots) {
				if (cdrom_slots[i].used) continue;
				cdrom_slots[i].used = true;
				cdrom_slots[i].target = target;
				cdrom_slots[i].lun = lun;
				return &cdrom_slots[i];
			}
			return nullptr;
		}

		bool ProbeReadCapacity(byte target, byte lun, ScsiReadCapacity10Reply& out_cap) {
			if (!mailbox_count) return false;
			if (!EnsureProbeBuffers()) {
				plogwarn("[SCSI] probe buffer allocation failed");
				return false;
			}
			byte* probe_data = probe_data_region.virt;
			byte* probe_sense = probe_sense_region.virt;

			byte cdb[10] = {};
			cdb[0] = SCSI_CMD_READ_CAPACITY10;

			byte completion_code = 0xFF;
			byte adapter_status = 0xFF;
			byte target_status = 0xFF;
			if (!ExecuteMailboxScsi(target, lun, cdb, 10, probe_data, sizeof(ScsiReadCapacity10Reply), true,
				completion_code, adapter_status, target_status)) {
				plogwarn("[SCSI] readcap t%ul%u timeout comp=%[8H] host=%[8H] tgt=%[8H]",
					(unsigned)target, (unsigned)lun,
					(unsigned)completion_code, (unsigned)adapter_status, (unsigned)target_status);
				return false;
			}

			// ploginfo("[SCSI] readcap t0l0 comp=%[8H] host=%[8H] tgt=%[8H]",
			// 	(unsigned)completion_code, (unsigned)adapter_status, (unsigned)target_status);
			if (completion_code != BLOGIC_CMD_COMPLETE_GOOD ||
				adapter_status != BLOGIC_ADAPTER_STATUS_OK ||
				target_status != BLOGIC_TARGET_STATUS_GOOD) {
				plogwarn("[SCSI] readcap t%ul%u failed sense0=%[8H]",
					(unsigned)target, (unsigned)lun, (unsigned)probe_sense[0]);
				return false;
			}
			MemCopyN(&out_cap, probe_data, sizeof(out_cap));
			return true;
		}

		bool SetupDiskTarget(byte target, byte lun, const ScsiReadCapacity10Reply& cap) {
			auto* slot = AcquireDiskSlot(target, lun);
			if (!slot) {
				plogwarn("[SCSI] no disk slot for t%u:%u", (unsigned)target, (unsigned)lun);
				return false;
			}
			const uint32 last_lba =
				(uint32(cap.last_lba[0]) << 24) |
				(uint32(cap.last_lba[1]) << 16) |
				(uint32(cap.last_lba[2]) << 8) |
				uint32(cap.last_lba[3]);
			const uint32 block_size =
				(uint32(cap.block_size[0]) << 24) |
				(uint32(cap.block_size[1]) << 16) |
				(uint32(cap.block_size[2]) << 8) |
				uint32(cap.block_size[3]);
			slot->disk.Bind(target, lun, this, ExecScsiCommandThunk);
			slot->disk.UpdateCapacity(last_lba, block_size);
			const uint64 block_count = uint64(last_lba) + 1;
			const uint64 total_bytes = block_count * uint64(block_size);
			ploginfo("[SCSI] t%ul%u capacity blocks=%[64H] block-size=%u bytes=%[64H]",
				(unsigned)target, (unsigned)lun,
				total_bytes ? block_count : 0,
				(unsigned)block_size,
				total_bytes);
			RegisterStorageNode(*slot);
			if (!ReadSector0(slot->disk, target, lun)) return false;
			ParsePartitions(*slot);
			MountPartitions(*slot);
			return true;
		}

		bool SetupCdromTarget(byte target, byte lun, const ScsiReadCapacity10Reply& cap) {
			auto* slot = AcquireCdromSlot(target, lun);
			if (!slot) {
				plogwarn("[SCSI] no cdrom slot for t%u:%u", (unsigned)target, (unsigned)lun);
				return false;
			}
			const uint32 last_lba =
				(uint32(cap.last_lba[0]) << 24) |
				(uint32(cap.last_lba[1]) << 16) |
				(uint32(cap.last_lba[2]) << 8) |
				uint32(cap.last_lba[3]);
			const uint32 block_size =
				(uint32(cap.block_size[0]) << 24) |
				(uint32(cap.block_size[1]) << 16) |
				(uint32(cap.block_size[2]) << 8) |
				uint32(cap.block_size[3]);
			slot->cdrom.Bind(target, lun, this, ExecScsiCommandThunk);
			slot->cdrom.UpdateCapacity(last_lba, block_size);
			ploginfo("[SCSI] t%ul%u cdrom capacity blocks=%u block-size=%u",
				(unsigned)target, (unsigned)lun,
				(unsigned)slot->cdrom.getUnits(), (unsigned)slot->cdrom.Block_Size);
			RegisterCdromNode(*slot);
			MountCdromWholeDisk(*slot);
			return true;
		}

		bool ProbeInquiry(byte target, byte lun, ScsiInquiryReply& out_inq) {
			if (!mailbox_count) return false;
			if (!EnsureProbeBuffers()) {
				plogwarn("[SCSI] probe buffer allocation failed");
				return false;
			}
			byte* probe_data = probe_data_region.virt;
			byte* probe_sense = probe_sense_region.virt;

			byte cdb[6] = {};
			cdb[0] = SCSI_CMD_INQUIRY;
			cdb[4] = sizeof(ScsiInquiryReply);

			byte completion_code = 0xFF;
			byte adapter_status = 0xFF;
			byte target_status = 0xFF;
			if (!ExecuteMailboxScsi(target, lun, cdb, 6, probe_data, sizeof(ScsiInquiryReply), true,
				completion_code, adapter_status, target_status)) {
				return false;
			}

			if (completion_code != BLOGIC_CMD_COMPLETE_GOOD ||
				adapter_status != BLOGIC_ADAPTER_STATUS_OK ||
				target_status != BLOGIC_TARGET_STATUS_GOOD) {
				return false;
			}
			MemCopyN(&out_inq, probe_data, sizeof(out_inq));
			return true;
		}

		static bool InquiryIsAbsent(const ScsiInquiryReply& inq) {
			const byte devtype = inq.peripheral & 0x1Fu;
			const byte qual = (inq.peripheral >> 5) & 0x07u;
			return qual == 0x03u || devtype == 0x1Fu;
		}

		void ProbeScsiTargets() {
			byte target_present[kMaxTargets] = {};
			uint16 target_mask = 0;
			if (QueryInstalledTargets(target_mask)) {
				for0(t, kMaxTargets) target_present[t] = byte((target_mask >> t) & 1u);
			}
			else {
				for0(t, kMaxTargets) target_present[t] = 1;
			}

			for0(target, kMaxTargets) {
				if (!target_present[target]) continue;
				for0(lun, kMaxLuns) {
					ScsiInquiryReply inq = {};
					if (!ProbeInquiry((byte)target, (byte)lun, inq)) {
						continue;
					}
					if (InquiryIsAbsent(inq)) continue;

					char vendor[9] = {};
					char product[17] = {};
					char revision[5] = {};
					MemCopyN(vendor, inq.vendor, 8);
					MemCopyN(product, inq.product, 16);
					MemCopyN(revision, inq.revision, 4);
					const byte devtype = inq.peripheral & 0x1Fu;
					ploginfo("[SCSI] t%ul%u inquiry devtype=%u qual=%u vendor=\"%s\" product=\"%s\" rev=\"%s\"",
						(unsigned)target, (unsigned)lun,
						(unsigned)devtype,
						(unsigned)((inq.peripheral >> 5) & 0x07u),
						vendor, product, revision);

					ScsiReadCapacity10Reply cap = {};
					if (!ProbeReadCapacity((byte)target, (byte)lun, cap)) continue;
					if (devtype == 0) {
						(void)SetupDiskTarget((byte)target, (byte)lun, cap);
					}
					else if (devtype == 5) {
						(void)SetupCdromTarget((byte)target, (byte)lun, cap);
					}
				}
			}
		}

		void DumpInquiryResults() {
			if (!has_io()) return;

			BusLogicBoardId board_id{};
			if (QueryBoardId(board_id)) {
				ploginfo("[SCSI] board id type=%[8H] fw=%c.%c custom=%[8H]",
					(unsigned)board_id.type,
					board_id.fw_ver_digit1 ? board_id.fw_ver_digit1 : '?',
					board_id.fw_ver_digit2 ? board_id.fw_ver_digit2 : '?',
					(unsigned)board_id.custom_features);
				if (board_id.fw_ver_digit1 == '4') {
					ploginfo("[SCSI] family=BusLogic MultiMaster C-series (BT-946C/956C/956CD class)");
				}
				else if (board_id.fw_ver_digit1 == '5') {
					ploginfo("[SCSI] family=BusLogic MultiMaster W-series (BT-948/958/958D class)");
				}
			}
			else {
				plogwarn("[SCSI] GET_BOARD_ID failed");
			}

			BusLogicConfig config{};
			if (QueryConfig(config)) {
				// ploginfo("[SCSI] config dma=%[8H] irq=%[8H] hostid=%u",
				// 	(unsigned)config.byte0,
				// 	(unsigned)config.byte1,
				// 	(unsigned)(config.byte2 & 0x0Fu));
			}
			else {
				plogwarn("[SCSI] INQ_CONFIG failed");
			}

			BusLogicSetupInfo setup{};
			if (QuerySetupInfo(setup)) {
				(void)setup;
				// ploginfo("[SCSI] setup parity=%u sync=%u tx=%u preempt=%u timeoff=%u mbox=%u base=%[32H]",
				// 	(unsigned)((flags >> 1) & 1u),
				// 	(unsigned)(flags & 1u),
				// 	(unsigned)setup.bytes[1],
				// 	(unsigned)setup.bytes[2],
				// 	(unsigned)setup.bytes[3],
				// 	(unsigned)setup.bytes[4],
				// 	mbox_addr);
			}
			else {
				plogwarn("[SCSI] INQ_SETUPINFO failed");
			}

			BusLogicExtSetup ext{};
			byte ext_mbox_count = 0;
			if (QueryExtSetup(ext)) {
				ext_mbox_count = ext.bytes[4];
				(void)ext;
				// ploginfo("[SCSI] extsetup bus=%[8H] bios=%[8H] sg-limit=%u mbox=%u fw=%c%c%c features=%[8H]",
				// 	(unsigned)ext.bytes[0],
				// 	(unsigned)ext.bytes[1],
				// 	(unsigned)(uint16(ext.bytes[2]) | (uint16(ext.bytes[3]) << 8)),
				// 	(unsigned)ext.bytes[4],
				// 	ext.bytes[10] ? ext.bytes[10] : '?',
				// 	ext.bytes[11] ? ext.bytes[11] : '?',
				// 	ext.bytes[12] ? ext.bytes[12] : '?',
				// 	(unsigned)feature);
				// ploginfo("[SCSI] caps wide=%u differential=%u scam=%u ultra=%u smart-term=%u",
				// 	(unsigned)((feature >> 0) & 1u),
				// 	(unsigned)((feature >> 1) & 1u),
				// 	(unsigned)((feature >> 2) & 1u),
				// 	(unsigned)((feature >> 3) & 1u),
				// 	(unsigned)((feature >> 4) & 1u));
			}
			else {
				plogwarn("[SCSI] INQ_EXTSETUP failed");
			}

			if (ext_mbox_count) {
				const byte init_mbox_count = ext_mbox_count > 32 ? 32 : ext_mbox_count;
				if (!InitializeMailbox(init_mbox_count)) {
					plogwarn("[SCSI] INIT_EXT_MBOX failed");
				}
				else {
					if (SetCcbFormat(BLOGIC_EXT_LUN_CCB)) {
						ploginfo("[SCSI] ccb format=extended");
					}
					else {
						plogwarn("[SCSI] SETCCB_FMT failed");
					}
					BusLogicSetupInfo setup_after_mbox{};
					if (QuerySetupInfo(setup_after_mbox)) {
						(void)setup_after_mbox;
						// ploginfo("[SCSI] setup-after-mbox mbox=%u base=%[32H]",
						// 	(unsigned)setup_after_mbox.bytes[4], mbox_addr_after);
					}
				}
			}

			BusLogicPciInfo pci{};
			if (QueryPciInfo(pci)) {
				(void)pci;
				// ploginfo("[SCSI] pci-info isa-port=%u irq=%u low-term=%u high-term=%u valid=%u",
				// 	(unsigned)pci.isa_port,
				// 	(unsigned)pci.irq_channel,
				// 	(unsigned)(pci.termination & 0x01u),
				// 	(unsigned)((pci.termination >> 1) & 0x01u),
				// 	(unsigned)((pci.termination >> 7) & 0x01u));
			}
			else {
				plogwarn("[SCSI] INQ_PCI_INFO failed");
			}

			byte fw_digit3 = 0;
			byte fw_letter = 0;
			if (QueryFirmwareExtra(fw_digit3, fw_letter)) {
				(void)fw_digit3;
				(void)fw_letter;
				// ploginfo("[SCSI] firmware extra d3=%c letter=%c",
				// 	fw_digit3 ? fw_digit3 : '?',
				// 	fw_letter ? fw_letter : '?');
			}
			else {
				plogwarn("[SCSI] INQ_FWVER extra failed");
			}
			ProbeScsiTargets();
		}

		bool Bind(DeviceNode* scsi_node) {
			if (!scsi_node) return false;
			node = scsi_node;
			bar0_io = Devsman::FindResource(scsi_node, DeviceResourceType::PciBarIo, 0);
			bar0_mmio = Devsman::FindResource(scsi_node, DeviceResourceType::PciBarMmio, 0);
			irq = Devsman::FindResource(scsi_node, DeviceResourceType::IrqLine, 0);
			io_base = bar0_io ? word(bar0_io->start) : 0;
			return bar0_io || bar0_mmio;
		}

		void DumpSummary() const {
			if (!node) return;
			ploginfo("[SCSI] ctlr %02x:%02x.%u vend=%[16H] dev=%[16H] class=%[8H].%[8H].%[8H]",
				(unsigned)node->fields.pci_bus,
				(unsigned)node->fields.pci_device,
				(unsigned)node->fields.pci_function,
				(unsigned)node->fields.vendor_id,
				(unsigned)node->fields.device_id,
				(unsigned)node->fields.class_base,
				(unsigned)node->fields.class_sub,
				(unsigned)node->fields.class_if);
			if (bar0_io) {
				ploginfo("[SCSI] BAR0 IO=%[64H]", bar0_io->start);
			}
			if (bar0_mmio) {
				ploginfo("[SCSI] BAR0 MMIO=%[64H] len=%[64H]",
					bar0_mmio->start, bar0_mmio->length);
			}
			if (irq) {
				ploginfo("[SCSI] IRQ line=%u pin=%u",
					(unsigned)irq->start, (unsigned)irq->extra);
			}
			else {
				plogwarn("[SCSI] IRQ resource missing; stage0 will stay polling-only");
			}
		}
	};

	ScsiController g_scsi_controller;
}

static bool start_scsi_driver(DeviceNode* scsi_node) {
	if (!g_scsi_controller.Bind(scsi_node)) {
		plogwarn("[SCSI] Failed to bind controller %s",
			scsi_node && scsi_node->link.addr ? scsi_node->link.addr : "(unnamed)");
		return false;
	}
	g_scsi_controller.DumpSummary();
	if (!g_scsi_controller.HardwareReset(true)) {
		plogwarn("[SCSI] hard reset failed");
	}
	g_scsi_controller.DumpInquiryResults();
	scsi_node->fields.binding.driver_data = &g_scsi_controller;
	return true;
}

_ESYM_C void R_SCSI_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_SCSI{
	.init = R_SCSI_INIT,
	.name = "SCSI",
};

void R_SCSI_INIT() {
	Devsman::RegisterDriverStarter("scsi", start_scsi_driver);
	Devsman::StartKnownDrivers();
}

#endif
