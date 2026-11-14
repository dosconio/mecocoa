// ASCII g++ TAB4 LF
// ModuTitle: Disk - SCSI
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include "../../include/devsman-storage.hpp"
#include <c/storage/harddisk.h>
#include <cpp/Device/Bus/PCI.hpp>
#include <cpp/Device/Storage/SCSI/BusLogic.hpp>
#include <cpp/Device/Storage/SCSI/LsiLogic-Para.hpp>
#include <cpp/Device/Storage/SCSI/LsiLogic-SAS.hpp>
#include <cpp/vector>
#include <cpp/ISO_IEC_STD/memory>
#include "../../mecocoa/devsman-acpi-gsi.h"

#if (_MCCA & 0xFF00) == 0x8600

namespace {
	static constexpr uint16 PciCommandIoSpace = 1u << 0;
	static constexpr uint16 PciCommandBusMaster = 1u << 2;
	static constexpr uint16 PciCommandInterruptDisable = 1u << 10;
	stduint g_scsi_controller_count = 0;

	// BusLogic mailboxes/CCBs and MPI 32-bit request/reply/SGE formats use low DMA addresses on x64 too.
	static constexpr Devs::StorageDmaRequirements ScsiDmaRequirements{12, 0xFFFFFFFFull, 0, true};

	struct ScsiAsyncState {
		ACPIGSIBinding interrupt{};
		Devs::StorageWaitContext completion_wait;
		Devs::StorageTimer service_timer;
		// Serialize BusLogic interrupt-register acknowledgement against polling administrative commands on another CPU.
		Spinlock interrupt_register_lock;
	};

	template <typename Controller>
	struct ScsiAsyncHost {
		static bool LockInterrupt(void* context) {
			return static_cast<Controller*>(context)->async.interrupt_register_lock.Acquire();
		}
		static void UnlockInterrupt(void* context, bool previous_if) {
			static_cast<Controller*>(context)->async.interrupt_register_lock.Release(previous_if);
		}
		static uint32 Milliseconds(void*) {
			return uint32(uint64(stduint(tick)) * 1000 / CONFIG_SysTickFreq);
		}

		static bool AllocateDma(void* context, stduint bytes, uni::SCSI::CommandDma& buffer) {
			auto* controller = static_cast<Controller*>(context);
			auto* region = new (std::nothrow_t{}) Devs::StorageDmaBuffer();
			if (!region) return false;
			if (!region->Ensure(controller->node, bytes, ScsiDmaRequirements)) {
				delete region;
				return false;
			}
			region->MarkInFlight();
			buffer = {region->virt, uint32(region->device_address), region->size, region};
			return true;
		}

		static void ReleaseDma(void*, uni::SCSI::CommandDma& buffer) {
			auto* region = static_cast<Devs::StorageDmaBuffer*>(buffer.token);
			if (!region) return;
			// CommandRequest releases only before submission or after quiescence; pinned requests never reach here.
			region->MarkQuiesced();
			if (!region->Release()) {
				plogwarn("[SCSI] retaining DMA mapping after failed invalidation");
				return;
			}
			delete region;
			buffer = {};
		}

		static void Completed(void* context, stduint origin) {
			auto& controller = *static_cast<Controller*>(context);
			Devs::NotifyStorageWait(controller.async.completion_wait, origin);
		}

		static void Recover(Controller& controller) {
			const auto recovery = controller.adapter.RecoverDetailed();
			// The queue retires/pins old requests; recovery may restart DMA into shared workspaces.
			if (!recovery.can_submit) {
				plogwarn("[SCSI] recovery unavailable: request DMA quiesced=%u engine stopped=%u",
					(unsigned)recovery.requests_quiesced, (unsigned)recovery.engine_stopped);
			}
		}

		static void ArmDeadline(Controller& controller) {
			uint32 milliseconds = controller.adapter.NextTimeout();
			if (milliseconds == 0xFFFFFFFFu) {
				controller.async.service_timer.Cancel();
				return;
			}
			// WaitStorage supplies polling wakeups; this service timer only guards the earliest request deadline.
			stduint delay = stduint((uint64(milliseconds) * CONFIG_SysTickFreq + 999) / 1000);
			if (!delay) delay = 1;
			if (!controller.async.service_timer.ArmService(delay, _IMM(&controller), Deadline)) {
				plogwarn("[SCSI] cannot arm request deadline; resetting before abandoning DMA ownership");
				Recover(controller);
			}
		}

		static void Deadline(pureptr_t timeout, ...) {
			Letpara(arguments, timeout);
			const stduint context = para_next(arguments, stduint);
			para_endo(arguments);
			auto& controller = *reinterpret_cast<Controller*>(context);
			MutexLocal lock(&controller.command_lock);
			if (!controller.async.service_timer.Consume(_IMM(&controller), Deadline)) return;
			controller.adapter.Service();
			ArmDeadline(controller);
		}

		static bool DispatchInterrupt(void* context) {
			auto& controller = *static_cast<Controller*>(context);
			if (!controller.adapter.OnInterrupt()) return false;
			// Never take the command mutex in IRQ context; a sleeping caller can also service the completion FIFO.
			Devs::NotifyStorageWait(controller.async.completion_wait);
			return true;
		}

		static void ConfigureInterrupt(Controller& controller) {
			const auto* irq = Devsman::FindResource(controller.node, DeviceResourceType::IrqLine, 0);
			if (!irq || irq->start >= 0xFFu) {
				plogwarn("[SCSI] no PCI INTx route; polling fallback");
				return;
			}
			const ACPIGSIConfiguration configuration{uint32(irq->start), true, true, true};
			const auto status = Devs::BindACPIGSI(configuration, DispatchInterrupt, &controller, controller.async.interrupt);
			if (status != ACPIGSIStatus::Success) {
				plogwarn("[SCSI] PCI INTx GSI%u binding failed status=%u; polling fallback", (unsigned)irq->start, unsigned(status));
				return;
			}
			auto* node = controller.node;
			const uint16 command = uint16(uni::PCI::read_config_register(node->fields.pci_segment,
				node->fields.pci_bus, node->fields.pci_device, node->fields.pci_function, 0x04));
			uni::PCI::write_config_register(node->fields.pci_segment, node->fields.pci_bus,
				node->fields.pci_device, node->fields.pci_function, 0x04, command & uint16(~PciCommandInterruptDisable));
			const uint16 actual = uint16(uni::PCI::read_config_register(node->fields.pci_segment,
				node->fields.pci_bus, node->fields.pci_device, node->fields.pci_function, 0x04));
			if (actual & PciCommandInterruptDisable) {
				(void)Devs::UnbindACPIGSI(controller.async.interrupt);
				plogwarn("[SCSI] PCI INTx remains masked; polling fallback");
				return;
			}
			controller.adapter.SetInterrupts(true);
			if (!controller.adapter.IsInterruptDriven()) {
				(void)Devs::UnbindACPIGSI(controller.async.interrupt);
				plogwarn("[SCSI] adapter interrupts unavailable; polling fallback");
				return;
			}
			ploginfo("[SCSI] %s INTx GSI%u, asynchronous command queue enabled",
				node->link.addr ? node->link.addr : "(unnamed)", controller.async.interrupt.gsi);
		}

		// Execute retains the request handle; IRQs never retain this stack-local probe context.
		struct CommandWait {
			Controller* controller;
			uni::SCSI::CommandRequest* request;
		};

		static Devs::StorageWaitStatus ProbeCompletion(void* context) {
			auto& wait = *static_cast<CommandWait*>(context);
			MutexLocal lock(&wait.controller->command_lock);
			if (!wait.request->IsDone()) {
				wait.controller->adapter.Service();
				ArmDeadline(*wait.controller);
			}
			return wait.request->IsDone() ? Devs::StorageWaitStatus::Completed : Devs::StorageWaitStatus::Pending;
		}

		static bool Execute(Controller& controller, stduint target, byte lun, const byte* cdb, byte cdb_length,
			void* data, stduint bytes, bool data_in, byte& completion, byte& host, byte& target_status) {
			completion = byte(uni::SCSI::Completion::Failed);
			host = byte(uni::SCSI::HostStatus::Error);
			target_status = 0xFF;
			if (bytes && !data) return false;
			const stduint tid = Taskman::CurrentTID();
			uni::SCSI::CommandHandle request;
			{
				MutexLocal lock(&controller.command_lock);
				request = controller.adapter.SubmitScsi(target, lun, cdb, cdb_length, data, bytes, data_in, 60000, tid);
				if (!request) return false;
				ArmDeadline(controller);
			}
			CommandWait wait{&controller, request.get()};
			const auto outcome = Devs::WaitStorage(controller.async.completion_wait, ProbeCompletion, &wait,
				60 * CONFIG_SysTickFreq, 10000000u, controller.adapter.IsInterruptDriven());
			if (outcome != Devs::StorageWaitStatus::Completed) {
				MutexLocal lock(&controller.command_lock);
				// Drain a completion racing the timeout before resetting; never abandon device-owned DMA.
				controller.adapter.Service();
				if (!request->IsDone()) Recover(controller);
				ArmDeadline(controller);
			}
			return request->CopyResult(data, completion, host, target_status);
		}
	};

	// Common SCSI storage integration: no adapter registers, DMA descriptors or mailbox status codes.
	struct ScsiController {
		stduint controller_slot = 0;
		bool supports_capacity16 = false;

		struct DiskSlot {
			stduint target = 0;
			byte lun = 0;
			DeviceNode* node = nullptr;
			uni::Harddisk_SCSI disk = {};
		};

		struct CdromSlot {
			stduint target = 0;
			byte lun = 0;
			DeviceNode* node = nullptr;
			uni::CDROM_SCSI cdrom = {};
		};

		DeviceNode* node = nullptr;
		void* context = nullptr;
		uni::SCSI::ExecuteCommandFn execute = nullptr;
		bool (*is_available)(void* context) = nullptr;
		bool (*is_target_present)(void* context, stduint target) = nullptr;
		bool (*is_sata_target)(void* context, stduint target) = nullptr;
		stduint target_count = 0;
		stduint lun_count = 0;
		byte* sector_buf = nullptr;
		stduint sector_buf_size = 0;
		// Nodes retain pointers into these objects; allocating each slot separately keeps its address stable.
		uni::Vector<uni::SharedPtr<DiskSlot>> disk_slots;
		uni::Vector<uni::SharedPtr<CdromSlot>> cdrom_slots;

		bool EnsureSectorBuffer(stduint block_size) {
			if (!block_size) return false;
			if (!sector_buf || sector_buf_size < block_size) {
				auto* replacement = (byte*)mempool.allocate(block_size, 12);
				if (!replacement) return false;
				if (sector_buf) mempool.deallocate(sector_buf, sector_buf_size);
				sector_buf = replacement;
				sector_buf_size = block_size;
			}
			MemSet(sector_buf, 0, block_size);
			return true;
		}

		void RegisterStorageNode(DiskSlot& slot) {
			if (!node || slot.node) return;
			String name;
			if (slot.lun == 0) name.Format("scsi-disk@%u:%u", (stduint)controller_slot, (stduint)slot.target);
			else name.Format("scsi-disk@%u:%u:%u", (stduint)controller_slot, (stduint)slot.target, (stduint)slot.lun);
			slot.node = Devsman::RegisterStorageDevice(
				node, name.reference(), DeviceBusType::PCI, "scsi-disk", &slot.disk);
			String alias;
			if (slot.lun == 0) alias.Format("scsi%u-%u", (stduint)controller_slot, (stduint)slot.target);
			else alias.Format("scsi%u-%u-%u", (stduint)controller_slot, (stduint)slot.target, (stduint)slot.lun);
			Devsman::RegisterDevAlias(slot.node, alias.reference());
		}

		void RegisterCdromNode(CdromSlot& slot) {
			if (!node || slot.node) return;
			String name;
			if (slot.lun == 0) name.Format("scsi-cdrom@%u:%u", (stduint)controller_slot, (stduint)slot.target);
			else name.Format("scsi-cdrom@%u:%u:%u", (stduint)controller_slot, (stduint)slot.target, (stduint)slot.lun);
			slot.node = Devsman::RegisterStorageDevice(
				node, name.reference(), DeviceBusType::PCI, "scsi-cdrom", &slot.cdrom);
			String alias;
			if (slot.lun == 0) alias.Format("sr-scsi%u-%u", (stduint)controller_slot, (stduint)slot.target);
			else alias.Format("sr-scsi%u-%u-%u", (stduint)controller_slot, (stduint)slot.target, (stduint)slot.lun);
			Devsman::RegisterDevAlias(slot.node, alias.reference());
		}

		bool ReadSector0(DiskSlot& slot) {
			if (!slot.node) return false;
			stduint block_size = 0;
			if (Devsman::Ctrl(slot.node, (stduint)DeviceCtrlCommand::GetBlockSize, &block_size) != 0) {
				plogwarn("[SCSI] ctrl t%ul%u get block size failed",
					(unsigned)slot.target, (unsigned)slot.lun);
				return false;
			}
			if (!block_size || !EnsureSectorBuffer(block_size)) return false;
			if (Devsman::Read(slot.node, sector_buf, block_size, 0, 0) != (stdsint)block_size) {
				plogwarn("[SCSI] READ(10) t%ul%u LBA0 failed",
					(unsigned)slot.target, (unsigned)slot.lun);
				return false;
			}
			return true;
		}

		void ParsePartitions(DiskSlot& slot) {
			if (slot.disk.hd_info_valid) return;
			DiscPartition::Partition(slot.disk, slot.disk.hd_info, sector_buf, 0);
			slot.disk.hd_info_valid = true;
		}

		void RegisterPartitionNodes(DiskSlot& slot) {
			if (!slot.node) return;
			auto& hdinfo = slot.disk.hd_info;
			for (stduint part_dev = 1; part_dev <= hdinfo.part_count; ++part_dev) {
				uni::PartitionSlice slice = GetPartitionSlice(hdinfo, part_dev);
				if (slice.sys_id == ClassicFilesysType::None || slice.sys_id == ClassicFilesysType::EXT || slice.length == 0) continue;
				String name;
				name.Format("partition@%u", part_dev);
				Devsman::RegisterStoragePartition(slot.node, name.reference(), slot.disk, part_dev);
			}
		}

		void MountPartitions(DiskSlot& slot) {
			auto& hdinfo = slot.disk.hd_info;
			for (stduint part_dev = 1; part_dev <= hdinfo.part_count; ++part_dev) {
				uni::PartitionSlice slice = GetPartitionSlice(hdinfo, part_dev);
				if (slice.sys_id == ClassicFilesysType::None) continue;
				if (slice.sys_id == ClassicFilesysType::EXT) continue;
				String node_name;
				node_name.Format("partition@%u", part_dev);
				DeviceNode* part_node = Devsman::RegisterStoragePartition(
					slot.node, node_name.reference(), slot.disk, part_dev);
				String lab;
				// Preserve the first controller's paths; other controllers need a distinct mount namespace.
				if (controller_slot) lab.Format("/mnt/scsi%u-%u-%u.%u", (stduint)controller_slot, (stduint)slot.target, (stduint)slot.lun, part_dev);
				else if (slot.lun == 0) lab.Format("/mnt/scsi%u.%u", (stduint)slot.target, part_dev);
				else lab.Format("/mnt/scsi%u-%u.%u", (stduint)slot.target, (stduint)slot.lun, part_dev);
				if (auto fs = Filesys::Mount(slot.disk, part_dev, lab.reference(), part_node)) {
					ploginfo("[SCSI] mount %s on %s", fs->name, lab.reference());
				}
			}
		}

		void MountCdromWholeDisk(CdromSlot& slot) {
			String lab;
			if (controller_slot) lab.Format("/mnt/scsi%u-%u-%u.0", (stduint)controller_slot, (stduint)slot.target, (stduint)slot.lun);
			else if (slot.lun == 0) lab.Format("/mnt/scsi%u.0", (stduint)slot.target);
			else lab.Format("/mnt/scsi%u-%u.0", (stduint)slot.target, (stduint)slot.lun);
			if (auto fs = Filesys::Mount(slot.cdrom, 0, lab.reference(), slot.node)) {
				ploginfo("[SCSI] mount %s on %s", fs->name, lab.reference());
			}
			else {
				plogwarn("[SCSI] no filesystem recognized on scsi cdrom %u:%u",
					(stduint)slot.target, (stduint)slot.lun);
			}
		}

		DiskSlot* AcquireDiskSlot(stduint target, byte lun) {
			for (auto& slot : disk_slots) {
				if (slot->target == target && slot->lun == lun) return slot.get();
			}
			auto owner = uni::make_shared<DiskSlot>();
			if (!owner) return nullptr;
			owner->target = target;
			owner->lun = lun;
			if (!disk_slots.Append(owner)) return nullptr;
			return owner.get();
		}

		CdromSlot* AcquireCdromSlot(stduint target, byte lun) {
			for (auto& slot : cdrom_slots) {
				if (slot->target == target && slot->lun == lun) return slot.get();
			}
			auto owner = uni::make_shared<CdromSlot>();
			if (!owner) return nullptr;
			owner->target = target;
			owner->lun = lun;
			if (!cdrom_slots.Append(owner)) return nullptr;
			return owner.get();
		}

		bool ProbeReadCapacity(stduint target, byte lun, uni::SCSI::ReadCapacity10Reply& out_cap) {
			uni::SCSI::CommandResult result{};
			if (!uni::SCSI::ReadCapacity10(execute, context, target, lun, out_cap, result)) {
				plogwarn("[SCSI] readcap t%ul%u timeout comp=%[8H] host=%[8H] tgt=%[8H]",
					(unsigned)target, (unsigned)lun,
					(unsigned)result.completion, (unsigned)result.host_status, (unsigned)result.target_status);
				return false;
			}
			// ploginfo("[SCSI] readcap t0l0 comp=%[8H] host=%[8H] tgt=%[8H]",
			// 	(unsigned)result.completion, (unsigned)result.host_status, (unsigned)result.target_status);
			if (!result.Succeeded()) {
				plogwarn("[SCSI] readcap t%ul%u failed comp=%[8H] host=%[8H] tgt=%[8H]",
					(unsigned)target, (unsigned)lun,
					(unsigned)result.completion, (unsigned)result.host_status, (unsigned)result.target_status);
				return false;
			}
			return true;
		}

		bool SetupDiskTarget(stduint target, byte lun, const uni::SCSI::ReadCapacity10Reply& cap) {
			uint64 last_lba = ReadBigEndian32(cap.last_lba);
			uint32 block_size = ReadBigEndian32(cap.block_size);
			// SBC: a saturated READ CAPACITY(10) last LBA requires READ CAPACITY(16).
			if (supports_capacity16 && last_lba == 0xFFFFFFFFull) {
				uni::SCSI::ReadCapacity16Reply capacity{};
				uni::SCSI::CommandResult result{};
				if (!uni::SCSI::ReadCapacity16(execute, context, target, lun, capacity, result) || !result.Succeeded()) {
					plogwarn("[SCSI] READ CAPACITY(16) t%ul%u failed comp=%[8H] host=%[8H] tgt=%[8H]",
						(unsigned)target, (unsigned)lun,
						(unsigned)result.completion, (unsigned)result.host_status, (unsigned)result.target_status);
					return false;
				}
				last_lba = (uint64(ReadBigEndian32(capacity.last_lba)) << 32) | ReadBigEndian32(capacity.last_lba + 4);
				block_size = ReadBigEndian32(capacity.block_size);
				// The device-node byte capacity must fit before publishing this disk.
				if (!block_size || last_lba < 0xFFFFFFFFull || last_lba >= ~uint64(0) / block_size) {
					plogwarn("[SCSI] READ CAPACITY(16) t%ul%u returned an invalid or unrepresentable capacity",
						(unsigned)target, (unsigned)lun);
					return false;
				}
			}
			auto* slot = AcquireDiskSlot(target, lun);
			if (!slot) {
				plogwarn("[SCSI] no disk slot for t%u:%u", (unsigned)target, (unsigned)lun);
				return false;
			}
			slot->disk.Bind(target, lun, context, execute);
			slot->disk.UpdateCapacity(last_lba, block_size);
			const uint64 block_count = last_lba + 1;
			const uint64 total_bytes = block_count * uint64(block_size);
			ploginfo("[SCSI] t%ul%u capacity blocks=%[64H] block-size=%u bytes=%[64H]",
				(unsigned)target, (unsigned)lun,
				total_bytes ? block_count : 0,
				(unsigned)block_size,
				total_bytes);
			RegisterStorageNode(*slot);
			if (!ReadSector0(*slot)) return false;
			ParsePartitions(*slot);
			RegisterPartitionNodes(*slot);
			MountPartitions(*slot);
			return true;
		}

		bool SetupCdromTarget(stduint target, byte lun, const uni::SCSI::ReadCapacity10Reply& cap) {
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
			slot->cdrom.Bind(target, lun, context, execute);
			slot->cdrom.UpdateCapacity(last_lba, block_size);
			ploginfo("[SCSI] t%ul%u cdrom capacity blocks=%u block-size=%u",
				(unsigned)target, (unsigned)lun,
				(unsigned)slot->cdrom.getUnits(), (unsigned)slot->cdrom.Block_Size);
			RegisterCdromNode(*slot);
			MountCdromWholeDisk(*slot);
			return true;
		}

		bool ProbeInquiry(stduint target, byte lun, uni::SCSI::InquiryReply& out_inq) {
			uni::SCSI::CommandResult result{};
			return uni::SCSI::Inquiry(execute, context, target, lun, out_inq, result) && result.Succeeded();
		}

		static bool InquiryIsAbsent(const uni::SCSI::InquiryReply& inq) {
			const byte devtype = inq.peripheral & 0x1Fu;
			const byte qual = (inq.peripheral >> 5) & 0x07u;
			return qual == 0x03u || devtype == 0x1Fu;
		}

		void ProbeSataIdentify(stduint target, byte lun) {
			if (!is_sata_target || !is_sata_target(context, target)) return;
			if (!EnsureSectorBuffer(uni::SCSI::AtaIdentifyDataSize)) {
				plogwarn("[SCSI-SATA] t%ul%u IDENTIFY skipped: no probe buffer", (unsigned)target, (unsigned)lun);
				return;
			}
			uni::SCSI::AtaIdentifyInfo info{};
			uni::SCSI::CommandResult result{};
			if (!uni::SCSI::IdentifyAtaDevice(execute, context, target, lun, sector_buf, sector_buf_size, info, result)) {
				plogwarn("[SCSI-SATA] t%ul%u IDENTIFY unavailable comp=%[8H] host=%[8H] tgt=%[8H]",
					(unsigned)target, (unsigned)lun,
					(unsigned)result.completion, (unsigned)result.host_status, (unsigned)result.target_status);
				return;
			}
			ploginfo("[SCSI-SATA] t%ul%u IDENTIFY model=\"%s\" SMART=%u LBA48=%u ATA-TRIM=%u",
				(unsigned)target, (unsigned)lun, info.model,
				(unsigned)info.smart, (unsigned)info.lba48, (unsigned)info.trim);
		}

		void ProbeScsiTarget(stduint target, byte lun) {
			if (!is_available || !is_available(context)) return;
			uni::SCSI::InquiryReply inq{};
			if (!ProbeInquiry(target, lun, inq) || InquiryIsAbsent(inq)) return;
			char vendor[9]{}, product[17]{}, revision[5]{};
			MemCopyN(vendor, inq.vendor, 8);
			MemCopyN(product, inq.product, 16);
			MemCopyN(revision, inq.revision, 4);
			const byte devtype = inq.peripheral & 0x1Fu;
			ploginfo("[SCSI] t%ul%u inquiry devtype=%u qual=%u vendor=\"%s\" product=\"%s\" rev=\"%s\"",
				(unsigned)target, (unsigned)lun, (unsigned)devtype,
				(unsigned)((inq.peripheral >> 5) & 0x07u), vendor, product, revision);
			uni::SCSI::ReadCapacity10Reply cap{};
			if (!ProbeReadCapacity(target, lun, cap)) return;
			if (devtype == 0) {
				// Optional SAT diagnostics follow normal disk setup and do not determine mount success.
				if (SetupDiskTarget(target, lun, cap)) ProbeSataIdentify(target, lun);
			}
			else if (devtype == 5) (void)SetupCdromTarget(target, lun, cap);
		}

		static uint32 ReadBigEndian32(const byte* p) {
			return (uint32(p[0]) << 24) | (uint32(p[1]) << 16) | (uint32(p[2]) << 8) | p[3];
		}

		bool ReadReportedLuns(stduint target, uni::Vector<byte>& luns) {
			// SPC REPORT LUNS (0xA0): an eight-byte header followed by eight-byte SAM LUN addresses.
			byte cdb[12]{};
			cdb[0] = 0xA0;
			stduint bytes = 16;
			for0(pass, 2) {
				if (!is_available || !is_available(context) || !EnsureSectorBuffer(bytes)) return false;
				cdb[6] = byte(bytes >> 24);
				cdb[7] = byte(bytes >> 16);
				cdb[8] = byte(bytes >> 8);
				cdb[9] = byte(bytes);
				uni::SCSI::CommandResult result{};
				if (!execute(context, target, 0, cdb, sizeof(cdb), sector_buf, bytes, true,
					result.completion, result.host_status, result.target_status) || !result.Succeeded()) return false;
				const uint32 list_bytes = ReadBigEndian32(sector_buf);
				if (list_bytes & 7) return false;
				const uint64 needed = uint64(list_bytes) + 8;
				if (needed > 0xFFFFFFFFull || needed > ~stduint(0)) return false;
				if (needed > bytes) {
					bytes = stduint(needed);
					continue;
				}
				// Copy addresses before probing: partition reads and mounts reuse sector_buf.
				for0(i, list_bytes / 8) {
					const byte* entry = sector_buf + 8 + i * 8;
					bool flat = entry[0] == 0 && entry[1] < lun_count;
					for (stduint j = 2; j < 8; ++j) if (entry[j]) flat = false;
					if (!flat) {
						plogwarn("[SCSI] t%u LUN addressing unsupported by this transport", (unsigned)target);
						continue;
					}
					bool duplicate = false;
					for (byte existing : luns) if (existing == entry[1]) duplicate = true;
					if (!duplicate && !luns.Append(entry[1])) return false;
				}
				return true;
			}
			return false;
		}

		void ProbeScsiTargets() {
			for0(target, target_count) {
				if (is_target_present && !is_target_present(context, target)) continue;
				uni::Vector<byte> luns;
				if (ReadReportedLuns(target, luns)) {
					for (byte lun : luns) ProbeScsiTarget(target, lun);
				}
				else {
					uni::SCSI::InquiryReply inq{};
					if (!ProbeInquiry(target, 0, inq)) continue;
					// Old targets may not implement REPORT LUNS; scan the transport's representable flat-LUN range.
					for0(lun, lun_count) ProbeScsiTarget(target, byte(lun));
				}
				if (!is_available || !is_available(context)) return;
			}
		}
	};

	struct BusLogicController {
		DeviceNode* node = nullptr;
		const DeviceResource* bar0_io = nullptr;
		const DeviceResource* bar0_mmio = nullptr;
		const DeviceResource* irq = nullptr;
		word io_base = 0;
		// Protect queue/mailbox changes against other callers and deferred completions, not the whole wait.
		Mutex command_lock;
		ScsiAsyncState async;
		Devs::StorageDmaBuffer regions[stduint(uni::SCSI::BusLogic::DmaSlot::Count)]{};
		uni::SCSI::BusLogic adapter;
		ScsiController storage;
		uint16 target_mask = 0;
		bool target_mask_valid = false;

		static bool EnsureDma(void* context, uni::SCSI::BusLogic::DmaSlot slot, stduint bytes,
			uni::SCSI::BusLogic::DmaBuffer& buffer) {
			auto* controller = static_cast<BusLogicController*>(context);
			const stduint index = stduint(slot);
			if (index >= numsof(controller->regions)) return false;
			auto& region = controller->regions[index];
			// Only called with no command in flight; never replace a buffer still owned by the adapter.
			region.MarkQuiesced();
			const bool ready = region.Ensure(controller->node, bytes, ScsiDmaRequirements);
			if (ready) region.MarkInFlight();
			// Also return empty/old metadata on failure so the controller never keeps a freed CPU address.
			buffer = {region.virt, uint32(region.device_address), region.size};
			return ready;
		}

		static void DisableDma(void* context) {
			auto* controller = static_cast<BusLogicController*>(context);
			auto* node = controller->node;
			const uint16 command = uint16(uni::PCI::read_config_register(node->fields.pci_segment,
				node->fields.pci_bus, node->fields.pci_device, node->fields.pci_function, 0x04));
			uni::PCI::write_config_register(node->fields.pci_segment, node->fields.pci_bus,
				node->fields.pci_device, node->fields.pci_function, 0x04,
				(command & uint16(~PciCommandBusMaster)) | PciCommandInterruptDisable);
		}

		static bool ExecScsiCommandThunk(void* context, stduint target_id, byte lun,
			const byte* cdb, byte cdb_length, void* data_buf, stduint data_length, bool data_in,
			byte& completion_code, byte& adapter_status, byte& target_status) {
			auto* controller = static_cast<BusLogicController*>(context);
			return ScsiAsyncHost<BusLogicController>::Execute(*controller, target_id, lun, cdb, cdb_length,
				data_buf, data_length, data_in, completion_code, adapter_status, target_status);
		}

		static bool IsAvailable(void* context) {
			auto* controller = static_cast<BusLogicController*>(context);
			MutexLocal lock(&controller->command_lock);
			return controller->adapter.IsReady();
		}

		static bool IsTargetPresent(void* context, stduint target) {
			auto* controller = static_cast<BusLogicController*>(context);
			MutexLocal lock(&controller->command_lock);
			return target < uni::SCSI::BusLogic::MaxTargets &&
				(!controller->target_mask_valid || (controller->target_mask & (uint16(1u) << target)));
		}

		bool Bind(DeviceNode* scsi_node) {
			if (!scsi_node || adapter.IsFaulted()) return false;
			if (node && node != scsi_node) return false;
			if (scsi_node->fields.vendor_id != uni::SCSI::BusLogic::VendorId ||
				scsi_node->fields.device_id != uni::SCSI::BusLogic::DeviceId) return false;
			bar0_io = Devsman::FindResource(scsi_node, DeviceResourceType::PciBarIo, 0);
			bar0_mmio = Devsman::FindResource(scsi_node, DeviceResourceType::PciBarMmio, 0);
			irq = Devsman::FindResource(scsi_node, DeviceResourceType::IrqLine, 0);
			if (!bar0_io || !bar0_io->start || bar0_io->start > 0xFFFCu || bar0_io->length < 4) return false;
			node = scsi_node;
			io_base = word(bar0_io->start);
			const uint16 command = uint16(uni::PCI::read_config_register(node->fields.pci_segment,
				node->fields.pci_bus, node->fields.pci_device, node->fields.pci_function, 0x04));
			// PCI Local Bus Specification, Command register: require I/O and DMA; try masking INTx if supported.
			const uint16 requested = command | PciCommandIoSpace | PciCommandBusMaster | PciCommandInterruptDisable;
			uni::PCI::write_config_register(node->fields.pci_segment, node->fields.pci_bus,
				node->fields.pci_device, node->fields.pci_function, 0x04, requested);
			const uint16 actual = uint16(uni::PCI::read_config_register(node->fields.pci_segment,
				node->fields.pci_bus, node->fields.pci_device, node->fields.pci_function, 0x04));
			const uint16 required = PciCommandIoSpace | PciCommandBusMaster;
			if ((actual & required) != required) {
				plogwarn("[SCSI] PCI I/O or bus mastering unavailable command=%[16H]", (stduint)actual);
				return false;
			}
			using Host = ScsiAsyncHost<BusLogicController>;
			const uni::SCSI::BusLogic::Services services{this, EnsureDma, DisableDma,
				{this, Host::AllocateDma, Host::ReleaseDma, Host::Milliseconds, Host::Completed}, Host::LockInterrupt, Host::UnlockInterrupt};
			return adapter.Bind(io_base, services);
		}

		void DumpSummary() const {
			if (!node) return;
			plogtrac("[SCSI] ctlr %02x:%02x.%u vend=%[16H] dev=%[16H] class=%[8H].%[8H].%[8H]",
				(unsigned)node->fields.pci_bus,
				(unsigned)node->fields.pci_device,
				(unsigned)node->fields.pci_function,
				(unsigned)node->fields.vendor_id,
				(unsigned)node->fields.device_id,
				(unsigned)node->fields.class_base,
				(unsigned)node->fields.class_sub,
				(unsigned)node->fields.class_if);
			if (bar0_io) {
				plogtrac("[SCSI] BAR0 IO=%[64H]", bar0_io->start);
			}
			if (bar0_mmio) {
				plogtrac("[SCSI] BAR0 MMIO=%[64H] len=%[64H]",
					bar0_mmio->start, bar0_mmio->length);
			}
			if (irq) {
				plogtrac("[SCSI] IRQ line=%u pin=%u",
					(unsigned)irq->start, (unsigned)irq->extra);
			}
			else {
				plogwarn("[SCSI] IRQ resource missing; polling fallback");
			}
		}

		bool Start() {
			if (adapter.IsReady()) return true;
			DumpSummary();
			const uint16 command = uint16(uni::PCI::read_config_register(node->fields.pci_segment,
				node->fields.pci_bus, node->fields.pci_device, node->fields.pci_function, 0x04));
			if (!adapter.Initialize(bool(command & PciCommandInterruptDisable))) return false;
			storage.controller_slot = g_scsi_controller_count++;
			target_mask_valid = adapter.QueryInstalledTargets(target_mask);
			ScsiAsyncHost<BusLogicController>::ConfigureInterrupt(*this);
			storage.node = node;
			storage.context = this;
			storage.execute = ExecScsiCommandThunk;
			storage.is_available = IsAvailable;
			storage.is_target_present = IsTargetPresent;
			storage.target_count = uni::SCSI::BusLogic::MaxTargets;
			storage.lun_count = uni::SCSI::BusLogic::MaxLuns;
			storage.ProbeScsiTargets();
			return adapter.IsReady();
		}
	};

	template <typename Adapter>
	struct LsiLogicController {
		DeviceNode* node = nullptr;
		// Serialize queue/FIFO changes only; release this mutex while individual requests wait for completion.
		Mutex command_lock;
		ScsiAsyncState async;
		Devs::StorageDmaBuffer regions[stduint(Adapter::DmaSlot::Count)]{};
		Adapter adapter;
		ScsiController storage;

		static bool EnsureDma(void* context, typename Adapter::DmaSlot slot, stduint bytes,
			typename Adapter::DmaBuffer& buffer) {
			auto* controller = static_cast<LsiLogicController*>(context);
			const stduint index = stduint(slot);
			if (index >= numsof(controller->regions)) return false;
			auto& region = controller->regions[index];
			// Initialize/reset has stopped DMA before the adapter asks to reuse these shared regions.
			region.MarkQuiesced();
			const bool allocated = region.Ensure(controller->node, bytes, ScsiDmaRequirements);
			if (allocated) region.MarkInFlight();
			buffer = {region.virt, uint32(region.device_address), region.size};
			return allocated;
		}

		static void DisableDma(void* context) {
			const auto* node = static_cast<LsiLogicController*>(context)->node;
			const uint16 command = uint16(uni::PCI::read_config_register(node->fields.pci_segment,
				node->fields.pci_bus, node->fields.pci_device, node->fields.pci_function, 0x04));
			uni::PCI::write_config_register(node->fields.pci_segment, node->fields.pci_bus,
				node->fields.pci_device, node->fields.pci_function, 0x04,
				(command & uint16(~PciCommandBusMaster)) | PciCommandInterruptDisable);
		}

		static bool ExecScsiCommandThunk(void* context, stduint target_id, byte lun,
			const byte* cdb, byte cdb_length, void* data_buf, stduint data_length, bool data_in,
			byte& completion_code, byte& adapter_status, byte& target_status) {
			auto* controller = static_cast<LsiLogicController*>(context);
			return ScsiAsyncHost<LsiLogicController>::Execute(*controller, target_id, lun, cdb, cdb_length,
				data_buf, data_length, data_in, completion_code, adapter_status, target_status);
		}

		static bool IsAvailable(void* context) {
			auto* controller = static_cast<LsiLogicController*>(context);
			MutexLocal lock(&controller->command_lock);
			return controller->adapter.IsReady();
		}

		static bool IsTargetPresent(void* context, stduint target) {
			auto* controller = static_cast<LsiLogicController*>(context);
			MutexLocal lock(&controller->command_lock);
			return controller->adapter.IsTargetPresent(target);
		}

		static bool IsSataTarget(void* context, stduint target) {
			if constexpr (uni::IsSame<Adapter, uni::SCSI::LsiLogicSAS>::value) {
				auto* controller = static_cast<LsiLogicController*>(context);
				MutexLocal lock(&controller->command_lock);
				return controller->adapter.IsSataTarget(target);
			}
			return false;
		}

		bool Bind(DeviceNode* scsi_node) {
			if (!scsi_node || adapter.IsFaulted() || (node && node != scsi_node)) return false;
			if (scsi_node->fields.vendor_id != Adapter::VendorId ||
				scsi_node->fields.device_id != Adapter::DeviceId) return false;
			if (adapter.IsReady()) return true;
			const auto* io = Devsman::FindResource(scsi_node, DeviceResourceType::PciBarIo, 0);
			if (!io || !io->start || io->start > 0xFFB8u || io->length < 0x48) {
				plogwarn("[%s] BAR0 I/O window missing or too small", Adapter::Name);
				return false;
			}
			node = scsi_node;
			const uint16 command = uint16(uni::PCI::read_config_register(node->fields.pci_segment,
				node->fields.pci_bus, node->fields.pci_device, node->fields.pci_function, 0x04));
			uni::PCI::write_config_register(node->fields.pci_segment, node->fields.pci_bus,
				node->fields.pci_device, node->fields.pci_function, 0x04,
				command | PciCommandIoSpace | PciCommandBusMaster | PciCommandInterruptDisable);
			const uint16 actual = uint16(uni::PCI::read_config_register(node->fields.pci_segment,
				node->fields.pci_bus, node->fields.pci_device, node->fields.pci_function, 0x04));
			const uint16 required = PciCommandIoSpace | PciCommandBusMaster;
			if ((actual & required) != required) {
				plogwarn("[%s] PCI I/O or bus mastering unavailable command=%[16H]", Adapter::Name, (unsigned)actual);
				return false;
			}
			ploginfo("[%s] controller %s IO=%[64H] initializing",
				Adapter::Name, node->link.addr ? node->link.addr : "(unnamed)", io->start);
			using Host = ScsiAsyncHost<LsiLogicController>;
			const typename Adapter::Services services{this, EnsureDma, DisableDma, Host::Milliseconds,
				{this, Host::AllocateDma, Host::ReleaseDma, Host::Milliseconds, Host::Completed}};
			return adapter.Bind(word(io->start), services);
		}

		bool Start() {
			if (adapter.IsReady()) return true;
			if (!adapter.Initialize()) return false;
			ScsiAsyncHost<LsiLogicController>::ConfigureInterrupt(*this);
			storage.controller_slot = g_scsi_controller_count++;
			storage.node = node;
			storage.context = this;
			storage.execute = ExecScsiCommandThunk;
			storage.is_available = IsAvailable;
			storage.is_target_present = IsTargetPresent;
			storage.target_count = adapter.getTargetCount();
			storage.lun_count = Adapter::MaxLuns;
			storage.supports_capacity16 = _MCCA == 0x8664;
			if constexpr (uni::IsSame<Adapter, uni::SCSI::LsiLogicSAS>::value)
				storage.is_sata_target = IsSataTarget;
			storage.ProbeScsiTargets();
			return adapter.IsReady();
		}
	};

	using LsiLogicParallelController = LsiLogicController<uni::SCSI::LsiLogicParallel>;
	using LsiLogicSASController = LsiLogicController<uni::SCSI::LsiLogicSAS>;

	// Registry owners are never erased: failed controllers may retain DMA buffers or published storage nodes.
	uni::Vector<uni::SharedPtr<BusLogicController>> g_buslogic_controllers;
	uni::Vector<uni::SharedPtr<LsiLogicParallelController>> g_lsi_parallel_controllers;
	uni::Vector<uni::SharedPtr<LsiLogicSASController>> g_lsi_sas_controllers;

	template <typename Controller>
	bool StartScsiController(DeviceNode* node, uni::Vector<uni::SharedPtr<Controller>>& controllers) {
		uni::SharedPtr<Controller> owner;
		for (auto& existing : controllers) {
			if (existing->node == node) {
				owner = existing;
				break;
			}
		}
		if (!owner) {
			owner = uni::make_shared<Controller>();
			if (!owner) {
				plogwarn("[SCSI] controller allocation failed for %s", node->link.addr ? node->link.addr : "(unnamed)");
				return false;
			}
			owner->node = node;
			// Retain ownership before Bind/Start can enable DMA or publish pointers; later vector growth never moves objects.
			if (!controllers.Append(owner)) return false;
		}
		if (!owner->Bind(node)) {
			plogwarn("[SCSI] Failed to bind controller %s", node->link.addr ? node->link.addr : "(unnamed)");
			return false;
		}
		if (!owner->Start()) return false;
		node->fields.binding.driver_data = owner.get();
		return true;
	}
}

static bool start_scsi_driver(DeviceNode* scsi_node) {
	if (!scsi_node) return false;
	if (scsi_node->fields.vendor_id == uni::SCSI::LsiLogicSAS::VendorId &&
		scsi_node->fields.device_id == uni::SCSI::LsiLogicSAS::DeviceId) {
		return StartScsiController(scsi_node, g_lsi_sas_controllers);
	}
	if (scsi_node->fields.vendor_id == uni::SCSI::LsiLogicParallel::VendorId &&
		scsi_node->fields.device_id == uni::SCSI::LsiLogicParallel::DeviceId) {
		return StartScsiController(scsi_node, g_lsi_parallel_controllers);
	}
	if (scsi_node->fields.vendor_id == uni::SCSI::BusLogic::VendorId &&
		scsi_node->fields.device_id == uni::SCSI::BusLogic::DeviceId) {
		return StartScsiController(scsi_node, g_buslogic_controllers);
	}
	return false;
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
