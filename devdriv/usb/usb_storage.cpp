// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] Device Management
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"
// #include <cpp/Device/Bus/ISA.hpp>
// #include <cpp/Device/Bus/PCI.hpp>
// #include <c/storage/AHCI.h>
// #include <c/storage/NVMe.h>
// #include <cpp/System/Audiosys/AudioManager.hpp>
#if ((_MCCA & 0xFF00) == 0x8600 || defined(_MCU_STM32H7x)) && CONFIG_ENABLE_USB
#include <cpp/Device/USB/USB.hpp>
#include <cpp/Device/USB/USBHost-MSC.hpp>
#include <c/format/filesys/FAT.h>
#if defined(_MCU_STM32H7x)
#include <cpp/Device/USB/OTGHostDevice.hpp>
#include "../../prehost/stm32h743X-ac7m/unicfg.hpp"
#endif
#endif
#if (_MCCA & 0xFF00) == 0x8600
#include "c/proctrl/IAx86_64.ext.h"
#include "c/proctrl/IAx86_64.msr.h"
#endif

#if ((_MCCA & 0xFF00) == 0x8600 || defined(_MCU_STM32H7x)) && CONFIG_ENABLE_USB
namespace Devs {
	namespace {
		using USBHostDevice = uni::device::SpaceUSB::USBHostDevice;
		using USBHost_MSC = uni::device::SpaceUSB::USBHost_MSC;

		constexpr stduint USBMSCCommandTimeoutTicks = 5 * CONFIG_SysTickFreq;
		constexpr stduint USBMSCMaximumTransferBytes = 64 * 1024;
		constexpr stduint USBMSCDisconnectRetryTicks = CONFIG_SysTickFreq ?
			CONFIG_SysTickFreq : 1;

		DeviceNode* FindUSBAncestor(DeviceNode* node, DeviceNodeType type) {
			for (auto* current = node; current;
				current = reinterpret_cast<DeviceNode*>(current->link.getParent())) {
				if (DeviceNodeType(current->fields.node_type) == type) return current;
			}
			return nullptr;
		}

		stduint FindUSBControllerIndex(DeviceNode* interface_node) {
			auto* bus = FindUSBAncestor(interface_node, DeviceNodeType::UsbBus);
			if (!bus || !bus->fields.binding.driver_name ||
				!bus->fields.binding.driver_name[0]) return stduint(-1);
			auto* parent = reinterpret_cast<DeviceNode*>(bus->link.getParent());
			if (!parent) return stduint(-1);
			stduint index = 0;
			for (auto* node = reinterpret_cast<DeviceNode*>(parent->link.subf); node;
				node = reinterpret_cast<DeviceNode*>(node->link.next)) {
				if (DeviceNodeType(node->fields.node_type) != DeviceNodeType::UsbBus ||
					!node->fields.binding.driver_name ||
					StrCompare(node->fields.binding.driver_name, bus->fields.binding.driver_name)) continue;
				if (node == bus) return index;
				index++;
			}
			return stduint(-1);
		}

		uint8 FindUSBDeviceID(DeviceNode* interface_node) {
			auto* device = FindUSBAncestor(interface_node, DeviceNodeType::UsbDevice);
			const auto* location = Devsman::FindResource(device,
				DeviceResourceType::UsbLocation, 0);
			return location ? uint8(location->extra) : 0;
		}

		enum class USBMSCWaitState : byte {
			Idle,
			Pending,
			Completed,
			TimedOut,
			Offline,
		};

		#if defined(_MCU_STM32H7x)
		enum class USBMSCServiceState : byte { Idle, Queued, Active, Cancelling, Quarantined, Finished };
		uni::Atomic<uint32> usb_msc_request_sequence = 0;
		#endif

		enum class USBMSCStorageState : byte {
			Initializing,
			Online,
			Disconnecting,
			Offline,
		};

		ClassicFilesysType WholeDiskFATType(const byte* sector, stduint block_size, uint32 blocks) {
			if (block_size < 512 || block_size > 4096 || (block_size & (block_size - 1)) ||
				sector[510] != 0x55 || sector[511] != 0xAA) return ClassicFilesysType::None;
			const auto& boot = *reinterpret_cast<const FAT_BootSector32*>(sector);
			if ((boot.jmpBoot[0] != 0xEB && boot.jmpBoot[0] != 0xE9) ||
				uint16(boot.bytes_per_sector) != block_size || !boot.reserved_sectors ||
				!boot.fat_count || !boot.sectors_per_cluster || boot.sectors_per_cluster > 128 ||
				(boot.sectors_per_cluster & (boot.sectors_per_cluster - 1))) return ClassicFilesysType::None;
			const uint32 sectors = boot.total_sectors_16 ? uint16(boot.total_sectors_16) : boot.total_sectors_32;
			const uint32 fat_sectors = boot.sectors_per_fat_16 ? boot.sectors_per_fat_16 : boot.sectors_per_fat_32;
			const uint32 root_sectors = (uint32(uint16(boot.root_entries)) * 32 + block_size - 1) / block_size;
			const uint64 first_data = boot.reserved_sectors + uint64(boot.fat_count) * fat_sectors + root_sectors;
			if (!sectors || sectors > blocks || !fat_sectors || first_data >= sectors)
				return ClassicFilesysType::None;
			// Microsoft FAT specification: FAT Type Determination uses the data cluster count.
			const uint32 clusters = (sectors - uint32(first_data)) / boot.sectors_per_cluster;
			const unsigned bits = clusters < 4085 ? 12 : clusters < 65525 ? 16 : 32;
			if (!clusters || uint64(fat_sectors) * block_size * 8 / bits < uint64(clusters) + 2)
				return ClassicFilesysType::None;
			if (bits == 32) {
				if (boot.sectors_per_fat_16 || uint16(boot.root_entries) || boot.fat_version ||
					clusters >= 0x0FFFFFF5u || boot.root_cluster < 2 ||
					boot.root_cluster >= uint64(clusters) + 2) return ClassicFilesysType::None;
				return ClassicFilesysType::FAT32_LBA;
			}
			if (!boot.sectors_per_fat_16 || !uint16(boot.root_entries)) return ClassicFilesysType::None;
			return bits == 12 ? ClassicFilesysType::FAT12 : ClassicFilesysType::FAT16_LBA;
		}

		struct USBMSCPartition {
			uni::PartitionSlice slice = {};

			bool operator==(const USBMSCPartition& other) const {
				return slice.address == other.slice.address &&
					slice.length == other.slice.length &&
					slice.sys_id == other.slice.sys_id;
			}
		};

		struct USBMSCPartitionNode {
			stduint device = 0;
			DeviceNode* node = nullptr;
			#if defined(_MCU_STM32H7x)
			uni::SharedPtr<uni::VFSMountState> mount_state;
			#endif

			bool operator==(const USBMSCPartitionNode& other) const {
				return device == other.device && node == other.node;
			}
		};

		class USBMSCStorage final : public uni::StorageTrait {
			USBHost_MSC* driver_ = nullptr;
			USBHostDevice* device_ = nullptr;
			DeviceNode* interface_node_ = nullptr;
			DeviceNode* storage_node_ = nullptr;
			bool nodes_registered_ = false;
			Mutex transaction_;
			uni::Atomic<uint32> references_ = 1;
			uni::Atomic<byte> state_ = byte(USBMSCStorageState::Initializing);
			uni::Atomic<byte> wait_state_ = byte(USBMSCWaitState::Idle);
			uni::Atomic<byte> wait_armed_ = 0;
			uni::Atomic<uint32> pending_command_id_ = 0;
			uni::Atomic<uint32> completed_command_id_ = 0;
			uni::Atomic<byte> completed_result_ = byte(USBHost_MSC::Result::TransferFailed);
			uni::Atomic<ThreadBlock*> waiter_ = nullptr;
			uni::Atomic<byte> partitions_ready_ = 0;
			uni::Vector<USBMSCPartition> partitions_;
			uni::Vector<USBMSCPartitionNode> partition_nodes_;
			PartitionSchemeKind partition_scheme_ = PartitionSchemeKind::Unknown;
			ClassicFilesysType whole_disk_type_ = ClassicFilesysType::None;
			uint32 partition_overflow_ = 0;
			bool partition_scan_active_ = false;
			bool partition_scan_failed_ = false;
			uint32 block_count_ = 0;
			stduint maximum_transfer_bytes_ = USBMSCMaximumTransferBytes;
			uint8 interface_number_ = 0;
			uint8 lun_ = 0;
			stduint controller_index_ = stduint(-1);
			String controller_name_;
			#if defined(_MCU_STM32H7x)
			uni::Atomic<byte> service_state_ = byte(USBMSCServiceState::Idle);
			uint32 service_sequence_ = 0;
			stduint service_source_ = 0, service_block_ = 0, service_count_ = 0;
			byte* transfer_buffer_ = nullptr;
			bool service_command_started_ = false;
			bool service_write_ = false, service_submitting_ = false, service_completed_ = false;
			stduint service_start_tick_ = 0;
			#endif
			uint8 device_id_ = 0;
			stduint cached_block_ = stduint(-1);
			USBMSCStorage* registry_next_ = nullptr;
			USBMSCStorage* disconnect_next_ = nullptr;

			bool IsOnline() const {
				return USBMSCStorageState(state_.load(uni::MemoryOrder_Acquire)) ==
					USBMSCStorageState::Online;
			}

			void ReleaseDeviceNodes() {
				for (stduint i = 0; i < partition_nodes_.Count(); ++i) {
					auto* node = partition_nodes_[i].node;
					if (!node || node == storage_node_) continue;
					auto* partition = static_cast<uni::DiscPartition*>(
						node->fields.binding.driver_data);
					node->fields.binding.driver_data = nullptr;
					node->fields.ops = nullptr;
					delete partition;
				}
				partition_nodes_.Clear();
				if (storage_node_) {
					storage_node_->fields.binding.driver_data = nullptr;
					storage_node_->fields.ops = nullptr;
				}
				storage_node_ = nullptr;
				interface_node_ = nullptr;
			}

			#if defined(_MCU_STM32H7x)
			void FinishServiceRequest() {
				const bool restore = IC.TryMaskInterrupt();
				byte expected = service_state_.load(uni::MemoryOrder_Acquire);
				if ((expected == byte(USBMSCServiceState::Active) || expected == byte(USBMSCServiceState::Cancelling)) &&
					service_state_.compare_exchange(expected, byte(USBMSCServiceState::Finished),
					uni::MemoryOrder_Acq_Rel, uni::MemoryOrder_Acquire)) {
					if (wait_armed_.load(uni::MemoryOrder_Acquire)) {
						Systimex::CancelThreadWake(service_source_);
						(void)Taskman::UnblockThread(service_source_, ThreadBlock::BlockReason::BR_Resting);
					}
					Release(); // The service releases the reference acquired before enqueueing.
				}
				if (restore) IC.enInterrupt(true);
			}

			bool TransferServiceChunk(stduint block, void* buffer, stduint count, bool write) {
				auto* thread = Taskman::CurrentTB();
				if (!thread || !thread->parent_process || thread->parent_process->ring != RING_M ||
					thread->tid == Task_Devsman || thread->parent_process->pid == Task_Usb_Serv ||
					!driver_ || !IsOnline() || !transfer_buffer_ || !buffer || !count || !Block_Size ||
					count > maximum_transfer_bytes_ / Block_Size || count > 0xFFFFu ||
					service_state_.load(uni::MemoryOrder_Acquire) != byte(USBMSCServiceState::Idle)) return false;
				const stduint bytes = count * Block_Size;
				if (write) MemCopyN(transfer_buffer_, buffer, bytes);
				const bool restore = IC.TryMaskInterrupt();
				if (!restore) return false;
				if (!IsOnline() || !driver_ || service_state_.load(uni::MemoryOrder_Acquire) != byte(USBMSCServiceState::Idle)) {
					IC.enInterrupt(true);
					return false;
				}
				const stduint start = tick;
				if (!Systimex::AppendThreadWake(USBMSCCommandTimeoutTicks, thread->tid)) {
					IC.enInterrupt(true);
					return false;
				}
				service_source_ = thread->tid;
				service_block_ = block;
				service_count_ = count;
				service_command_started_ = false;
				service_write_ = write;
				service_start_tick_ = start;
				service_completed_ = false;
				service_sequence_ = usb_msc_request_sequence.fetch_add(1, uni::MemoryOrder_Acq_Rel) + 1;
				if (!service_sequence_) service_sequence_ = usb_msc_request_sequence.fetch_add(1, uni::MemoryOrder_Acq_Rel) + 1;
				pending_command_id_.store(0, uni::MemoryOrder_Release);
				completed_command_id_.store(0, uni::MemoryOrder_Release);
				completed_result_.store(byte(USBHost_MSC::Result::Busy), uni::MemoryOrder_Release);
				wait_state_.store(byte(USBMSCWaitState::Pending), uni::MemoryOrder_Release);
				wait_armed_.store(1, uni::MemoryOrder_Release);
				service_state_.store(byte(USBMSCServiceState::Queued), uni::MemoryOrder_Release);
				Retain(); // Keep the adapter alive until the waiting call returns.
				Retain(); // The queued message transfers this reference to the USB service.
				UsbMSCRequest request{this, service_sequence_};
				const auto error = syssend_async(Task_Usb_Serv, &request, sizeof(request),
					_IMM(DevsmanMsg::USB_MSC_REQUEST));
				if (error) {
					service_state_.store(byte(USBMSCServiceState::Idle), uni::MemoryOrder_Release);
					wait_state_.store(byte(USBMSCWaitState::Idle), uni::MemoryOrder_Release);
					wait_armed_.store(0, uni::MemoryOrder_Release);
					Systimex::CancelThreadWake(thread->tid);
					Release();
					Release();
					IC.enInterrupt(true);
					return false;
				}
				IC.enInterrupt(true);

				for (;;) {
					const bool resume = IC.TryMaskInterrupt();
					const byte service_state = service_state_.load(uni::MemoryOrder_Acquire);
					if (service_state == byte(USBMSCServiceState::Finished) || service_state == byte(USBMSCServiceState::Quarantined)) {
						if (resume) IC.enInterrupt(true);
						break;
					}
					if (stduint(stduint(tick) - start) >= USBMSCCommandTimeoutTicks) {
						byte expected = byte(USBMSCWaitState::Pending);
						if (wait_state_.compare_exchange(expected, byte(USBMSCWaitState::TimedOut),
							uni::MemoryOrder_Acq_Rel, uni::MemoryOrder_Acquire)) {
							byte online = byte(USBMSCStorageState::Online);
							(void)state_.compare_exchange(online, byte(USBMSCStorageState::Offline),
								uni::MemoryOrder_Acq_Rel, uni::MemoryOrder_Acquire);
							plogwarn("USB MSC service timeout controller=%s device=%u sequence=%u",
								controller_name_.reference(), (stduint)device_id_, (stduint)service_sequence_);
						}
					}
					// The USB service stops the transport before completing cancellation.
					thread->Block(ThreadBlock::BlockReason::BR_Resting);
					if (resume) IC.enInterrupt(true);
					if (thread->state == ThreadBlock::State::Pended) Taskman::Schedule(true);
				}

				const bool cleanup = IC.TryMaskInterrupt();
				wait_armed_.store(0, uni::MemoryOrder_Release);
				Systimex::CancelThreadWake(thread->tid);
				const bool success = service_state_.load(uni::MemoryOrder_Acquire) == byte(USBMSCServiceState::Finished) &&
					wait_state_.load(uni::MemoryOrder_Acquire) == byte(USBMSCWaitState::Completed) &&
					pending_command_id_.load(uni::MemoryOrder_Acquire) == completed_command_id_.load(uni::MemoryOrder_Acquire) &&
					completed_result_.load(uni::MemoryOrder_Acquire) == byte(USBHost_MSC::Result::Ok) && IsOnline();
				if (service_state_.load(uni::MemoryOrder_Acquire) == byte(USBMSCServiceState::Finished))
					service_state_.store(byte(USBMSCServiceState::Idle), uni::MemoryOrder_Release);
				wait_state_.store(byte(IsOnline() ? USBMSCWaitState::Idle : USBMSCWaitState::Offline), uni::MemoryOrder_Release);
				if (cleanup) IC.enInterrupt(true);
				if (success && !write) MemCopyN(buffer, transfer_buffer_, bytes);
				Release();
				return success;
			}
			#endif

			bool TransferChunkLocked(stduint block, void* buffer, stduint count,
				bool write) {
				#if defined(_MCU_STM32H7x)
				return TransferServiceChunk(block, buffer, count, write);
				#else
				auto* thread = Taskman::CurrentTB();
				if (!thread || Taskman::CurrentTID() == Task_Devsman || !driver_ ||
					!IsOnline()) return false;
				if (thread->parent_process &&
					thread->parent_process->pid == Task_Usb_Serv) return false;

				waiter_.store(thread, uni::MemoryOrder_Release);
				pending_command_id_.store(0, uni::MemoryOrder_Release);
				completed_command_id_.store(0, uni::MemoryOrder_Release);
				completed_result_.store(byte(USBHost_MSC::Result::Busy), uni::MemoryOrder_Release);
				wait_armed_.store(0, uni::MemoryOrder_Release);
				wait_state_.store(byte(USBMSCWaitState::Pending), uni::MemoryOrder_Release);

				auto error = write
					? driver_->WriteBlocks(uint32(block), uint16(count), buffer)
					: driver_->ReadBlocks(uint32(block), uint16(count), buffer);
				if (error) {
					wait_state_.store(byte(USBMSCWaitState::Idle), uni::MemoryOrder_Release);
					waiter_.store(nullptr, uni::MemoryOrder_Release);
					plogwarn("USB MSC %s submit failed interface=%u lba=%u blocks=%u error=%s",
						write ? "write" : "read", (stduint)driver_->InterfaceNumber(),
						block, count, error.Name());
					return false;
				}

				const uint32 command_id = driver_->CommandID();
				pending_command_id_.store(command_id, uni::MemoryOrder_Release);
				wait_armed_.store(1, uni::MemoryOrder_Release);
				bool timer_added = false;
				if (wait_state_.load(uni::MemoryOrder_Acquire) == byte(USBMSCWaitState::Pending)) {
					timer_added = Systimex::AppendThreadWake(USBMSCCommandTimeoutTicks, thread->tid);
					if (!timer_added) {
						byte expected = byte(USBMSCWaitState::Pending);
						(void)wait_state_.compare_exchange(expected, byte(USBMSCWaitState::TimedOut),
							uni::MemoryOrder_Acq_Rel, uni::MemoryOrder_Acquire);
					}
				}

				if (wait_state_.load(uni::MemoryOrder_Acquire) == byte(USBMSCWaitState::Pending)) {
					thread->Block(ThreadBlock::BlockReason::BR_Resting);
					if (thread->state == ThreadBlock::State::Pended) Taskman::Schedule(true);
				}

				wait_armed_.store(0, uni::MemoryOrder_Release);
				if (timer_added) Systimex::CancelThreadWake(thread->tid);
				byte expected = byte(USBMSCWaitState::Pending);
				(void)wait_state_.compare_exchange(expected,
					byte(USBMSCWaitState::TimedOut), uni::MemoryOrder_Acq_Rel,
					uni::MemoryOrder_Acquire);
				const auto final_state = USBMSCWaitState(wait_state_.load(uni::MemoryOrder_Acquire));
				const bool timed_out = final_state == USBMSCWaitState::TimedOut;
				const uint32 completed_id = completed_command_id_.load(uni::MemoryOrder_Acquire);
				const auto result = USBHost_MSC::Result(completed_result_.load(uni::MemoryOrder_Acquire));
				const bool success = final_state == USBMSCWaitState::Completed &&
					completed_id == command_id && result == USBHost_MSC::Result::Ok &&
					IsOnline();

				if (timed_out) state_.store(byte(USBMSCStorageState::Offline),
					uni::MemoryOrder_Release);
				waiter_.store(nullptr, uni::MemoryOrder_Release);
				pending_command_id_.store(0, uni::MemoryOrder_Release);
				wait_state_.store(byte(IsOnline() ?
					USBMSCWaitState::Idle : USBMSCWaitState::Offline), uni::MemoryOrder_Release);
				if (timed_out) {
					driver_->AbortCommand();
					plogwarn("USB MSC %s timed out interface=%u command=%u lba=%u blocks=%u",
						write ? "write" : "read", (stduint)interface_number_,
						(stduint)command_id, block, count);
				}
				return success;
				#endif
			}

		public:
			explicit USBMSCStorage(USBHost_MSC& driver, DeviceNode* interface_node)
				: driver_(&driver), device_(driver.ParentDevice()),
					interface_node_(interface_node),
					block_count_(driver.BlockCount()),
					interface_number_(uint8(driver.InterfaceNumber())),
					lun_(driver.Lun()),
					controller_index_(FindUSBControllerIndex(interface_node)),
					device_id_(FindUSBDeviceID(interface_node)) {
				auto* bus = FindUSBAncestor(interface_node, DeviceNodeType::UsbBus);
				auto* controller = bus ? bus->fields.controller : nullptr;
				if (controller && DeviceNodeType(controller->fields.node_type) == DeviceNodeType::PlatformDevice && controller->link.addr) {
					controller_name_ = String::newFormat("%s", controller->link.addr);
				} else if (bus && bus->fields.binding.driver_name && controller_index_ != stduint(-1)) {
					controller_name_ = String::newFormat("%s%u", bus->fields.binding.driver_name, controller_index_);
				}
				#if defined(_MCU_STM32H7x)
				maximum_transfer_bytes_ = static_cast<uni::device::SpaceUSB::OTGHostDevice*>(device_)->getBulkTransferLimit();
				if (maximum_transfer_bytes_) transfer_buffer_ = new byte[maximum_transfer_bytes_];
				#endif
				Block_Size = driver.BlockSize();
				Block_buffer = Block_Size && Block_Size <= maximum_transfer_bytes_ ?
					new byte[Block_Size] : nullptr;
				readable = Block_buffer && block_count_;
				#if defined(_MCU_STM32H7x)
				readable = readable && transfer_buffer_;
				writable = readable && BoardConfig::Usb::Otg2::MscWriteEnabled;
				#else
				writable = readable;
				#endif
				state_.store(byte(readable ? USBMSCStorageState::Online :
					USBMSCStorageState::Offline), uni::MemoryOrder_Release);
			}

			~USBMSCStorage() override {
				#if defined(_MCU_STM32H7x)
				delete[] transfer_buffer_;
				#endif
				delete[] static_cast<byte*>(Block_buffer);
			}

			bool Valid() const {
				return readable && Block_buffer && Block_Size && block_count_ && driver_ && device_ &&
					interface_node_ && controller_index_ != stduint(-1) && controller_name_.getByteCount() && device_id_;
			}

			USBHost_MSC* Driver() const { return driver_; }
			USBHostDevice* Device() const { return device_; }
			USBMSCStorage* RegistryNext() const { return registry_next_; }
			void SetRegistryNext(USBMSCStorage* next) { registry_next_ = next; }
			USBMSCStorage* DisconnectNext() const { return disconnect_next_; }
			void SetDisconnectNext(USBMSCStorage* next) { disconnect_next_ = next; }
			DeviceNode* USBDeviceNode() const {
				return FindUSBAncestor(interface_node_, DeviceNodeType::UsbDevice);
			}

			void Retain() {
				(void)references_.fetch_add(1, uni::MemoryOrder_Acq_Rel);
			}

			void Release() {
				if (references_.fetch_sub(1, uni::MemoryOrder_Acq_Rel) == 1) delete this;
			}

			#if defined(_MCU_STM32H7x)
			void PumpServiceRequest() {
				byte current = service_state_.load(uni::MemoryOrder_Acquire);
				const bool expired = stduint(stduint(tick) - service_start_tick_) >= USBMSCCommandTimeoutTicks;
				if (current == byte(USBMSCServiceState::Queued)) {
					if (!expired && IsOnline()) return;
					service_state_.store(byte(USBMSCServiceState::Active), uni::MemoryOrder_Release);
					current = byte(USBMSCServiceState::Active);
				}
				if (current == byte(USBMSCServiceState::Active)) {
					const bool failed = service_completed_ &&
						completed_result_.load(uni::MemoryOrder_Acquire) != byte(USBHost_MSC::Result::Ok);
					if (!expired && !failed && IsOnline()) return;
					if (expired) {
						byte expected = byte(USBMSCWaitState::Pending);
						(void)wait_state_.compare_exchange(expected, byte(USBMSCWaitState::TimedOut),
							uni::MemoryOrder_Acq_Rel, uni::MemoryOrder_Acquire);
					}
					byte online = byte(USBMSCStorageState::Online);
					(void)state_.compare_exchange(online, byte(USBMSCStorageState::Offline),
						uni::MemoryOrder_Acq_Rel, uni::MemoryOrder_Acquire);
					if (!service_command_started_) {
						FinishServiceRequest();
						return;
					}
					service_state_.store(byte(USBMSCServiceState::Cancelling), uni::MemoryOrder_Release);
					current = byte(USBMSCServiceState::Cancelling);
				}
				if (current != byte(USBMSCServiceState::Cancelling)) return;
				const auto stopped = Devsman::StopOTGTransfers(device_);
				if (stopped == UsbTransferStopResult::Pending) return;
				if (stopped == UsbTransferStopResult::Failed) {
					const bool restore = IC.TryMaskInterrupt();
					service_state_.store(byte(USBMSCServiceState::Quarantined), uni::MemoryOrder_Release);
					if (wait_armed_.load(uni::MemoryOrder_Acquire)) {
						Systimex::CancelThreadWake(service_source_);
						(void)Taskman::UnblockThread(service_source_, ThreadBlock::BlockReason::BR_Resting);
					}
					if (restore) IC.enInterrupt(true);
					// Keep the service reference and owned buffer until hardware quiescence is certified.
					plogerro("USB MSC transfer buffer retained controller=%s device=%u sequence=%u",
						controller_name_.reference(), (stduint)device_id_, (stduint)service_sequence_);
					return;
				}
				if (driver_) driver_->AbortCommand();
				completed_result_.store(byte(USBHost_MSC::Result::TransferFailed), uni::MemoryOrder_Release);
				FinishServiceRequest();
			}

			void SubmitServiceRequest(uint32 sequence, stduint source) {
				if (sequence != service_sequence_ || source != service_source_ ||
					service_state_.load(uni::MemoryOrder_Acquire) != byte(USBMSCServiceState::Queued)) return;
				service_state_.store(byte(USBMSCServiceState::Active), uni::MemoryOrder_Release);
				if (!IsOnline() || !driver_ || driver_->IsBusy() ||
					wait_state_.load(uni::MemoryOrder_Acquire) != byte(USBMSCWaitState::Pending)) {
					Complete(0, USBHost_MSC::Result::TransferFailed);
					return;
				}
				service_command_started_ = true;
				service_submitting_ = true;
				const auto error = service_write_
					? driver_->WriteBlocks(uint32(service_block_), uint16(service_count_), transfer_buffer_)
					: driver_->ReadBlocks(uint32(service_block_), uint16(service_count_), transfer_buffer_);
				pending_command_id_.store(driver_->CommandID(), uni::MemoryOrder_Release);
				service_submitting_ = false;
				// StartCommand can complete synchronously on submission failure.
				if (service_completed_) {
					if (wait_state_.load(uni::MemoryOrder_Acquire) == byte(USBMSCWaitState::Completed) &&
						completed_result_.load(uni::MemoryOrder_Acquire) == byte(USBHost_MSC::Result::Ok)) FinishServiceRequest();
					else PumpServiceRequest();
				} else if (error) Complete(driver_->CommandID(), USBHost_MSC::Result::TransferFailed);
			}
			#endif

			void Complete(uint32 command_id, USBHost_MSC::Result result) {
				#if defined(_MCU_STM32H7x)
				if (service_state_.load(uni::MemoryOrder_Acquire) != byte(USBMSCServiceState::Active)) return;
				#endif
				const uint32 pending_id = pending_command_id_.load(uni::MemoryOrder_Acquire);
				if (pending_id && pending_id != command_id) return;
				completed_command_id_.store(command_id, uni::MemoryOrder_Release);
				completed_result_.store(byte(result), uni::MemoryOrder_Release);
				byte expected = byte(USBMSCWaitState::Pending);
				#if defined(_MCU_STM32H7x)
				const bool expired = stduint(stduint(tick) - service_start_tick_) >= USBMSCCommandTimeoutTicks;
				(void)wait_state_.compare_exchange(expected, byte(expired ? USBMSCWaitState::TimedOut : USBMSCWaitState::Completed),
					uni::MemoryOrder_Acq_Rel, uni::MemoryOrder_Acquire);
				if (expired) state_.store(byte(USBMSCStorageState::Offline), uni::MemoryOrder_Release);
				service_completed_ = true;
				if (!service_submitting_) {
					if (!expired && result == USBHost_MSC::Result::Ok &&
						wait_state_.load(uni::MemoryOrder_Acquire) == byte(USBMSCWaitState::Completed)) FinishServiceRequest();
					else PumpServiceRequest();
				}
				#else
				if (!wait_state_.compare_exchange(expected, byte(USBMSCWaitState::Completed),
					uni::MemoryOrder_Acq_Rel, uni::MemoryOrder_Acquire)) return;
				if (!wait_armed_.load(uni::MemoryOrder_Acquire)) return;
				auto* waiter = waiter_.load(uni::MemoryOrder_Acquire);
				if (!waiter) return;
				Systimex::CancelThreadWake(waiter->tid);
				waiter->Unblock(ThreadBlock::BlockReason::BR_Resting);
				#endif
			}

			void BeginDisconnect() {
				state_.store(byte(USBMSCStorageState::Disconnecting),
					uni::MemoryOrder_Release);
				partitions_ready_.store(0, uni::MemoryOrder_Release);
				cached_block_ = stduint(-1);
				byte expected = byte(USBMSCWaitState::Pending);
				if (!wait_state_.compare_exchange(expected, byte(USBMSCWaitState::Offline),
					uni::MemoryOrder_Acq_Rel, uni::MemoryOrder_Acquire)) {
					wait_state_.store(byte(USBMSCWaitState::Offline), uni::MemoryOrder_Release);
					return;
				}
				if (!wait_armed_.load(uni::MemoryOrder_Acquire)) return;
				auto* waiter = waiter_.load(uni::MemoryOrder_Acquire);
				if (!waiter) return;
				Systimex::CancelThreadWake(waiter->tid);
				waiter->Unblock(ThreadBlock::BlockReason::BR_Resting);
			}

			#if defined(_MCU_STM32H7x)
			void DetachTransport() {
				// The controller has certified quiescence before this observer and buffer are released.
				if (driver_) {
					driver_->SetObserver(nullptr, nullptr);
					driver_->AbortCommand();
				}
				const bool restore = IC.TryMaskInterrupt();
				driver_ = nullptr;
				device_ = nullptr;
				const byte current = service_state_.load(uni::MemoryOrder_Acquire);
				if (current == byte(USBMSCServiceState::Queued) || current == byte(USBMSCServiceState::Active) ||
					current == byte(USBMSCServiceState::Cancelling) || current == byte(USBMSCServiceState::Quarantined)) {
					service_state_.store(byte(USBMSCServiceState::Cancelling), uni::MemoryOrder_Release);
					completed_result_.store(byte(USBHost_MSC::Result::TransferFailed), uni::MemoryOrder_Release);
					FinishServiceRequest();
				}
				if (restore) IC.enInterrupt(true);
			}
			#endif

			void FinishDisconnect() {
				ReleaseDeviceNodes();
				driver_ = nullptr;
				device_ = nullptr;
				state_.store(byte(USBMSCStorageState::Offline),
					uni::MemoryOrder_Release);
			}

			bool QueueProbe(stduint delay = 0) {
				Retain();
				if (delay) {
					if (Systimex::AppendDriverMessage(delay, Task_FileSys,
						_IMM(FilemanMsg::USB_MSC_PROBE), reinterpret_cast<stduint>(this))) return true;
					Release();
					return false;
				}
				USBMSCStorage* context = this;
				if (!syssend_async(Task_FileSys, &context, sizeof(context),
					_IMM(FilemanMsg::USB_MSC_PROBE))) return true;
				Release();
				return false;
			}

			bool ProbeLBA0() {
				if (!IsOnline()) {
					plogwarn("USB MSC LBA0 probe skipped: device offline interface=%u",
						(stduint)interface_number_);
					return false;
				}
				if (!Read(0, Block_buffer, 1)) {
					plogwarn("USB MSC LBA0 read failed interface=%u result=%u",
						(stduint)interface_number_,
						(stduint)completed_result_.load(uni::MemoryOrder_Acquire));
					return false;
				}
				auto* data = static_cast<const byte*>(Block_buffer);
				const uint16 signature = Block_Size >= 512 ?
					uint16(data[510] | (uint16(data[511]) << 8)) : 0;
				uint32 first = 0;
				for (stduint i = 0; i < Block_Size && i < sizeof(first); ++i) {
					first |= uint32(data[i]) << (i * 8);
				}
				plogtrac("USB MSC LBA0 read ok interface=%u block-size=%u first=%[32H] mbr-signature=%[16H]",
					(stduint)interface_number_, Block_Size, (stduint)first,
					(stduint)signature);
				return true;
			}

			bool ScanPartitions() {
				if (!IsOnline() || !Block_buffer || !Block_Size)
					return false;
				partitions_ready_.store(0, uni::MemoryOrder_Release);
				if (Block_Size < 512 || !Read(0, Block_buffer)) return false;
				whole_disk_type_ = WholeDiskFATType(static_cast<const byte*>(Block_buffer), Block_Size, block_count_);
				if (whole_disk_type_ != ClassicFilesysType::None) {
					partitions_.Clear();
					partition_scheme_ = PartitionSchemeKind::Unknown;
					partition_overflow_ = 0;
					partitions_ready_.store(1, uni::MemoryOrder_Release);
					return true;
				}
				auto* table = new HD_Info{};
				if (!table) {
					plogwarn("USB MSC partition table allocation failed interface=%u",
						(stduint)interface_number_);
					return false;
				}
				table->whole_disk.length = block_count_;
				partition_scan_failed_ = false;
				partition_scan_active_ = true;
				uni::DiscPartition::Partition(*this, *table,
					static_cast<byte*>(Block_buffer), 0);
				partition_scan_active_ = false;
				if (partition_scan_failed_ || !IsOnline()) {
					delete table;
					return false;
				}
				partitions_.Clear();
				for (stduint device = 1; device <= table->part_count; ++device) {
					USBMSCPartition partition = {};
					partition.slice = GetPartitionSlice(*table, device);
					partitions_.Append(partition);
				}
				partition_scheme_ = table->scheme_kind;
				partition_overflow_ = table->part_overflow;
				delete table;
				partitions_ready_.store(1, uni::MemoryOrder_Release);
				// ploginfo("USB MSC partitions parsed interface=%u scheme=%u partitions=%u overflow=%u",
				// 	(stduint)interface_number_, (stduint)partition_scheme_,
				// 	partitions_.Count(), (stduint)partition_overflow_);
				return true;
			}

			bool QueueRegistration() {
				Retain();
				USBMSCStorage* context = this;
				if (!syssend_async(Task_Devsman, &context, sizeof(context),
					_IMM(DevsmanMsg::USB_MSC_READY))) return true;
				Release();
				return false;
			}

			bool QueueMount() {
				Retain();
				USBMSCStorage* context = this;
				if (!syssend_async(Task_FileSys, &context, sizeof(context),
					_IMM(FilemanMsg::USB_MSC_MOUNT))) return true;
				Release();
				return false;
			}

			bool PartitionsReady() const {
				return IsOnline() && partitions_ready_.load(uni::MemoryOrder_Acquire);
			}

			bool RegisterNodes() {
				if (!IsOnline() ||
					!partitions_ready_.load(uni::MemoryOrder_Acquire) ||
					!interface_node_) return false;
				if (nodes_registered_) return true;
				auto storage_name = String::newFormat("usb-storage@%u", (stduint)lun_);
				storage_node_ = Devsman::RegisterStorageDevice(interface_node_,
					storage_name.reference(), DeviceBusType::USB, "usb-mass-storage", this);
				if (!storage_node_ || !Devsman::AttachStorageOps(storage_node_, this))
					return false;
				auto alias = interface_number_ || lun_
					? String::newFormat("%s-%u-if%u-lun%u", controller_name_.reference(),
						(stduint)device_id_, (stduint)interface_number_, (stduint)lun_)
					: String::newFormat("%s-%u", controller_name_.reference(),
						(stduint)device_id_);
				if (!Devsman::RegisterDevAlias(storage_node_, alias.reference()))
					return false;
				if (whole_disk_type_ != ClassicFilesysType::None) {
					nodes_registered_ = partition_nodes_.Append(USBMSCPartitionNode{0, storage_node_});
					return nodes_registered_;
				}
				stduint registered = 0;
				for (stduint i = 0; i < partitions_.Count(); ++i) {
					const auto slice = partitions_[i].slice;
					if (!slice.length || !_IMM(slice.sys_id) || slice.sys_id == ClassicFilesysType::EXT)
						continue;
					auto name = String::newFormat("partition@%u", i + 1);
					auto* node = Devsman::RegisterStoragePartition(storage_node_,
						name.reference(), *this, i + 1);
					if (!node) return false;
					bool recorded = false;
					for (stduint j = 0; j < partition_nodes_.Count(); ++j)
						if (partition_nodes_[j].node == node) { recorded = true; break; }
					if (!recorded && !partition_nodes_.Append(USBMSCPartitionNode{i + 1, node})) return false;
					registered++;
					plogtrac("USB MSC partition registered interface=%u partition=%u lba=%u blocks=%u type=%[8H]",
						(stduint)interface_number_, i + 1, slice.address,
						slice.length, (stduint)slice.sys_id);
				}
				plogtrac("USB MSC storage registered interface=%u blocks=%u block-size=%u partitions=%u registered=%u",
					(stduint)interface_number_, (stduint)block_count_, Block_Size,
					partitions_.Count(), registered);
				nodes_registered_ = true;
				return true;
			}

			bool MountPartitions() {
				if (!IsOnline() || !storage_node_)
					return false;
				const char* mount_name = controller_name_.reference();
				if (StrCompareN(mount_name, "usb-otg", 7) == 0) mount_name += 4;
				stduint mounted = 0;
				for (stduint i = 0; i < partition_nodes_.Count(); ++i) {
					auto& entry = partition_nodes_[i];
					if (!entry.node || !IsOnline()) continue;
					auto mount_path = interface_number_ || lun_
						? String::newFormat("/mnt/%s.%u.i%u.l%u.%u",
							mount_name, (stduint)device_id_,
							(stduint)interface_number_, (stduint)lun_, entry.device)
						: String::newFormat("/mnt/%s.%u.%u",
							mount_name, (stduint)device_id_, entry.device);
					if (Filesys::CountMountsForSourceNode(entry.node)) {
						mounted++;
						continue;
					}
					if (auto* filesystem = Filesys::Mount(*this, entry.device,
						mount_path.reference(), entry.node)) {
						#if defined(_MCU_STM32H7x)
						if (auto* root = Filesys::Index(mount_path.reference())) {
							if (root->d_mounts) root = root->d_mounts;
							if (root->d_inode) entry.mount_state = root->d_inode->mount_state;
						}
						#endif
						mounted++;
						ploginfo("USB MSC mount %s on %s interface=%u partition=%u",
							filesystem->name, mount_path.reference(),
							(stduint)interface_number_, entry.device);
					} else {
						const auto slice = getSlice(entry.device);
						plogwarn("USB MSC cannot mount interface=%u partition=%u type=%[8H] on %s",
							(stduint)interface_number_, entry.device,
							(stduint)slice.sys_id, mount_path.reference());
					}
				}
				return mounted != 0;
			}

			bool UnmountPartitions() {
				for (stduint i = 0; i < partition_nodes_.Count(); ++i) {
					auto& entry = partition_nodes_[i];
					if (!entry.node) continue;
					while (Filesys::CountMountsForSourceNode(entry.node)) {
						auto path = Filesys::GetFirstMountPathForSourceNode(entry.node);
						if (!path.getByteCount() || !Filesys::Unmount(path.reference())) {
							plogwarn("USB MSC cannot unmount controller=%u slot=%u partition=%u",
								controller_index_, (stduint)device_id_, entry.device);
							return false;
						}
						ploginfo("USB MSC unmounted %s controller=%u slot=%u partition=%u",
							path.reference(), controller_index_, (stduint)device_id_,
							entry.device);
					}
					#if defined(_MCU_STM32H7x)
					// Detached open files still own their backend and its raw adapter pointer.
					if (entry.mount_state.use_count() > 1) return false;
					entry.mount_state.reset();
					#endif
				}
				return true;
			}

			bool Read(stduint block, void* destination, stduint count = 1) override {
				if (!destination || !count || !readable || !Block_Size ||
					block >= block_count_ || count > block_count_ - block) {
					if (partition_scan_active_) partition_scan_failed_ = true;
					return false;
				}
				const stduint maximum_blocks = maximum_transfer_bytes_ / Block_Size;
				if (!maximum_blocks) {
					if (partition_scan_active_) partition_scan_failed_ = true;
					return false;
				}
				MutexLocal guard(&transaction_);
				if (!IsOnline()) {
					if (partition_scan_active_) partition_scan_failed_ = true;
					return false;
				}
				stduint completed = 0;
				while (completed < count) {
					stduint chunk = count - completed;
					if (chunk > maximum_blocks) chunk = maximum_blocks;
					if (chunk > 0xFFFFu) chunk = 0xFFFFu;
					auto* target = static_cast<byte*>(destination) + completed * Block_Size;
					if (!TransferChunkLocked(block + completed, target, chunk, false)) {
						if (partition_scan_active_) partition_scan_failed_ = true;
						return false;
					}
					completed += chunk;
				}
				auto* last = static_cast<byte*>(destination) + (count - 1) * Block_Size;
				if (Block_buffer != last) MemCopyN(Block_buffer, last, Block_Size);
				cached_block_ = block + count - 1;
				return true;
			}

			bool Write(stduint block, const void* source, stduint count = 1) override {
				if (!source || !count || !writable || !Block_Size ||
					block >= block_count_ || count > block_count_ - block) return false;
				const stduint maximum_blocks = maximum_transfer_bytes_ / Block_Size;
				if (!maximum_blocks) return false;
				MutexLocal guard(&transaction_);
				if (!IsOnline()) return false;
				cached_block_ = stduint(-1);
				stduint completed = 0;
				while (completed < count) {
					stduint chunk = count - completed;
					if (chunk > maximum_blocks) chunk = maximum_blocks;
					if (chunk > 0xFFFFu) chunk = 0xFFFFu;
					auto* target = const_cast<byte*>(
						static_cast<const byte*>(source) + completed * Block_Size);
					if (!TransferChunkLocked(block + completed, target, chunk, true)) {
						return false;
					}
					completed += chunk;
				}
				// StorageTrait has no flush operation; WRITE(10) completion is not a cache flush.
				return true;
			}

			stduint getUnits() override {
				return IsOnline() ? block_count_ : 0;
			}

			uni::PartitionSlice getSlice(stduint device) override {
				uni::PartitionSlice slice = {};
				if (!device && IsOnline()) {
					slice.length = block_count_;
					slice.sys_id = whole_disk_type_;
					return slice;
				}
				if (!IsOnline() ||
					!partitions_ready_.load(uni::MemoryOrder_Acquire) ||
					device > partitions_.Count()) return slice;
				return partitions_[device - 1].slice;
			}

			int operator[](uint64 offset) override {
				if (!Block_Size || offset / Block_Size >= block_count_ || !Block_buffer || !readable)
					return -1;
				MutexLocal guard(&transaction_);
				if (!IsOnline()) return -1;
				const stduint block = stduint(offset / Block_Size);
				if (cached_block_ != block) {
					if (!TransferChunkLocked(block, Block_buffer, 1, false)) return -1;
					cached_block_ = block;
				}
				return static_cast<byte*>(Block_buffer)[stduint(offset % Block_Size)];
			}
		};

		struct USBMSCDisconnectContext {
			DeviceNode* device_node = nullptr;
			USBMSCStorage* storages = nullptr;
			uint32 unmount_attempts = 0;
		};

		Spinlock usb_msc_storage_lock;
		USBMSCStorage* usb_msc_storage = nullptr;

		#if defined(_MCU_STM32H7x)
		USBMSCStorage* AcquireUSBMSCStorage(void* context) {
			SpinlockLocal guard(&usb_msc_storage_lock);
			for (auto* storage = usb_msc_storage; storage; storage = storage->RegistryNext()) {
				if (storage != context) continue;
				storage->Retain();
				return storage;
			}
			return nullptr;
		}
		#endif

		USBMSCStorage* AcquireUSBMSCStorage(USBHost_MSC& driver) {
			SpinlockLocal guard(&usb_msc_storage_lock);
			for (auto* storage = usb_msc_storage; storage; storage = storage->RegistryNext()) {
				if (storage->Driver() != &driver) continue;
				storage->Retain();
				return storage;
			}
			return nullptr;
		}

		USBMSCStorage* AcquireUSBMSCStorage(USBHostDevice& device) {
			SpinlockLocal guard(&usb_msc_storage_lock);
			for (auto* storage = usb_msc_storage; storage; storage = storage->RegistryNext()) {
				if (storage->Device() != &device) continue;
				storage->Retain();
				return storage;
			}
			return nullptr;
		}

		bool RegisterUSBMSCStorage(USBMSCStorage& storage) {
			SpinlockLocal guard(&usb_msc_storage_lock);
			for (auto* current = usb_msc_storage; current; current = current->RegistryNext()) {
				if (current->Driver() == storage.Driver()) return false;
			}
			storage.SetRegistryNext(usb_msc_storage);
			usb_msc_storage = &storage;
			return true;
		}

		bool RemoveUSBMSCStorage(USBMSCStorage& storage) {
			SpinlockLocal guard(&usb_msc_storage_lock);
			USBMSCStorage* previous = nullptr;
			for (auto* current = usb_msc_storage; current; current = current->RegistryNext()) {
				if (current != &storage) {
					previous = current;
					continue;
				}
				if (previous) previous->SetRegistryNext(current->RegistryNext());
				else usb_msc_storage = current->RegistryNext();
				current->SetRegistryNext(nullptr);
				return true;
			}
			return false;
		}

		void DetachUSBMSCStorages(USBHostDevice& device,
			USBMSCDisconnectContext& context) {
			SpinlockLocal guard(&usb_msc_storage_lock);
			USBMSCStorage* previous = nullptr;
			auto* current = usb_msc_storage;
			while (current) {
				auto* next = current->RegistryNext();
				if (current->Device() != &device) {
					previous = current;
					current = next;
					continue;
				}
				if (previous) previous->SetRegistryNext(next);
				else usb_msc_storage = next;
				current->SetRegistryNext(nullptr);
				current->SetDisconnectNext(context.storages);
				context.storages = current;
				current = next;
			}
		}
	}

	void handle_usb_msc_event(uni::device::SpaceUSB::USBHost_MSC& driver,
		uni::device::SpaceUSB::USBHost_MSC::Event event, uint32 command_id,
		DeviceNode* interface_node) {
		if (event == uni::device::SpaceUSB::USBHost_MSC::Event::CommandComplete) {
			if (auto* storage = AcquireUSBMSCStorage(driver)) {
				storage->Complete(command_id, driver.LastResult());
				storage->Release();
			}
			return;
		}
		if (!driver.IsReady()) return;
		if (auto* existing = AcquireUSBMSCStorage(driver)) {
			existing->Release();
			return;
		}
		auto* storage = new USBMSCStorage(driver, interface_node);
		if (!storage || !storage->Valid()) {
			plogwarn("USB MSC storage unavailable interface=%u block-size=%u blocks=%u",
				(stduint)driver.InterfaceNumber(), (stduint)driver.BlockSize(),
				(stduint)driver.BlockCount());
			delete storage;
			return;
		}
		if (!RegisterUSBMSCStorage(*storage)) {
			storage->Release();
			return;
		}
		if (storage->QueueProbe()) return;
		(void)RemoveUSBMSCStorage(*storage);
		storage->BeginDisconnect();
		storage->FinishDisconnect();
		storage->Release();
		plogwarn("USB MSC LBA0 probe queue failed interface=%u",
			(stduint)driver.InterfaceNumber());
	}

	#if defined(_MCU_STM32H7x)
	void prepare_usb_msc_disconnect(uni::device::SpaceUSB::USBHostDevice& device) {
		auto* storage = AcquireUSBMSCStorage(device);
		while (storage) {
			USBMSCStorage* next = nullptr;
			{
				SpinlockLocal guard(&usb_msc_storage_lock);
				next = storage->RegistryNext();
				if (next) next->Retain();
			}
			if (storage->Device() == &device) storage->BeginDisconnect();
			storage->Release();
			storage = next;
		}
	}
	#endif

	bool disconnect_usb_msc_storage(uni::device::SpaceUSB::USBHostDevice& device) {
		auto* first = AcquireUSBMSCStorage(device);
		if (!first) return false;
		auto* context = new USBMSCDisconnectContext{};
		if (!context) {
			plogerro("USB MSC disconnect context allocation failed");
			USBMSCDisconnectContext fallback{};
			DetachUSBMSCStorages(device, fallback);
			for (auto* storage = fallback.storages; storage;
				storage = storage->DisconnectNext()) {
				storage->BeginDisconnect();
				#if defined(_MCU_STM32H7x)
				storage->DetachTransport();
				#endif
			}
			first->Release();
			return true;
		}
		context->device_node = first->USBDeviceNode();
		first->Release();
		DetachUSBMSCStorages(device, *context);
		for (auto* storage = context->storages; storage;
			storage = storage->DisconnectNext()) {
			storage->BeginDisconnect();
			#if defined(_MCU_STM32H7x)
			storage->DetachTransport();
			#endif
		}
		if (!context->storages) {
			delete context;
			return false;
		}
		USBMSCDisconnectContext* message = context;
		if (!syssend_async(Task_FileSys, &message, sizeof(message),
			_IMM(FilemanMsg::USB_MSC_UNMOUNT))) return true;
		if (Systimex::AppendDriverMessage(USBMSCDisconnectRetryTicks, Task_FileSys,
			_IMM(FilemanMsg::USB_MSC_UNMOUNT), reinterpret_cast<stduint>(context))) {
			plogwarn("USB MSC unmount queue delayed controller node=%s",
				context->device_node && context->device_node->link.addr ?
				context->device_node->link.addr : "unknown");
			return true;
		}
		plogerro("USB MSC unmount queue failed controller node=%s",
			context->device_node && context->device_node->link.addr ?
			context->device_node->link.addr : "unknown");
		return true;
	}

}

#if defined(_MCU_STM32H7x)
void Devsman::PumpUSBMSCRequests() {
	if (Taskman::CurrentTID() != Task_Usb_Serv) return;
	Devs::USBMSCStorage* storage = nullptr;
	{
		SpinlockLocal guard(&Devs::usb_msc_storage_lock);
		storage = Devs::usb_msc_storage;
		if (storage) storage->Retain();
	}
	while (storage) {
		Devs::USBMSCStorage* next = nullptr;
		{
			SpinlockLocal guard(&Devs::usb_msc_storage_lock);
			next = storage->RegistryNext();
			if (next) next->Retain();
		}
		storage->PumpServiceRequest();
		storage->Release();
		storage = next;
	}
}

void Devsman::ProcessUSBMSCRequest(const UsbMSCRequest& request, stduint source) {
	if (Taskman::CurrentTID() != Task_Usb_Serv) return;
	const bool restore = IC.TryMaskInterrupt();
	const auto* sender = Taskman::LocateThread(source);
	const bool trusted = sender && sender->parent_process && sender->parent_process->ring == RING_M;
	if (restore) IC.enInterrupt(true);
	if (!trusted) return;
	if (auto* storage = Devs::AcquireUSBMSCStorage(request.context)) {
		storage->SubmitServiceRequest(request.sequence, source);
		storage->Release();
	}
}
#endif

void Devsman::ProcessUSBMSCProbe(void* context) {
	auto* storage = static_cast<Devs::USBMSCStorage*>(context);
	if (!storage) return;
	if ((storage->PartitionsReady() || (storage->ProbeLBA0() && storage->ScanPartitions())) &&
		!storage->QueueRegistration()) {
		plogwarn("USB MSC storage registration queue failed");
	}
	storage->Release();
}

void Devsman::RegisterUSBMSCStorage(void* context) {
	auto* storage = static_cast<Devs::USBMSCStorage*>(context);
	if (!storage) return;
	if (!storage->RegisterNodes()) {
		#if defined(_MCU_STM32H7x)
		if (storage->PartitionsReady() && !storage->QueueProbe(Devs::USBMSCDisconnectRetryTicks))
			plogwarn("USB MSC storage registration retry queue failed");
		#else
		plogwarn("USB MSC storage node registration failed");
		#endif
	}
	else if (!storage->QueueMount()) {
		plogwarn("USB MSC mount queue failed");
	}
	storage->Release();
}

void Devsman::MountUSBMSCStorage(void* context) {
	auto* storage = static_cast<Devs::USBMSCStorage*>(context);
	if (!storage) return;
	if (!storage->MountPartitions()) {
		plogwarn("USB MSC no partition mounted");
	}
	storage->Release();
}

void Devsman::UnmountUSBMSCStorage(void* context) {
	auto* disconnect = static_cast<Devs::USBMSCDisconnectContext*>(context);
	if (!disconnect) return;
	for (auto* storage = disconnect->storages; storage;
		storage = storage->DisconnectNext()) {
		if (!storage->UnmountPartitions()) {
			disconnect->unmount_attempts++;
			if (disconnect->unmount_attempts == 1 ||
				(disconnect->unmount_attempts % 60) == 0) {
				plogwarn("USB MSC disconnected storage waiting for unmount attempt=%u",
					(stduint)disconnect->unmount_attempts);
			}
			if (!Systimex::AppendDriverMessage(Devs::USBMSCDisconnectRetryTicks, Task_FileSys,
				_IMM(FilemanMsg::USB_MSC_UNMOUNT), reinterpret_cast<stduint>(disconnect))) {
				plogerro("USB MSC cannot schedule unmount retry");
			}
			return;
		}
	}
	Devs::USBMSCDisconnectContext* message = disconnect;
	if (!syssend_async(Task_Devsman, &message, sizeof(message),
		_IMM(DevsmanMsg::USB_MSC_REMOVE))) return;
	if (!Systimex::AppendDriverMessage(Devs::USBMSCDisconnectRetryTicks, Task_Devsman,
		_IMM(DevsmanMsg::USB_MSC_REMOVE), reinterpret_cast<stduint>(disconnect))) {
		plogerro("USB MSC removal queue failed");
	}
}

void Devsman::RemoveUSBMSCStorage(void* context) {
	auto* disconnect = static_cast<Devs::USBMSCDisconnectContext*>(context);
	if (!disconnect) return;
	auto device_name = String::newFormat("%s",
		disconnect->device_node && disconnect->device_node->link.addr ?
		disconnect->device_node->link.addr : "");
	for (auto* storage = disconnect->storages; storage;
		storage = storage->DisconnectNext()) {
		storage->FinishDisconnect();
	}
	if (!disconnect->device_node ||
		!Devsman::RemoveUSBDevice(disconnect->device_node)) {
		plogwarn("USB MSC disconnected device node removal failed name=%s",
			device_name.getByteCount() ? device_name.reference() : "unknown");
	} else {
		ploginfo("USB MSC disconnected device removed name=%s",
			device_name.reference());
	}
	auto* storage = disconnect->storages;
	while (storage) {
		auto* next = storage->DisconnectNext();
		storage->SetDisconnectNext(nullptr);
		storage->Release();
		storage = next;
	}
	delete disconnect;
}

#endif
