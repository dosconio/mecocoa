// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: EHCI Bring-up
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"

#if _MCCA == 0x8664 && defined(_UEFI)
#include <cpp/Device/Bus/PCI.hpp>
#include <cpp/Device/USB/USBHost-MSC.hpp>
#include <cpp/Device/USB/eHCI/eHCI.hpp>

void TSC_Wait_MS(uint64_t ms);

namespace {
	using EhciHostController = uni::device::SpaceUSB2::HostController;
	using EhciHostDevice = uni::device::SpaceUSB2::USBHostDevice_v2;
	using EhciControllerError = uni::device::SpaceUSB2::ControllerError;
	using EhciRootPortStatus = uni::device::SpaceUSB2::RootPortStatus;
	using USBMassStorage = uni::device::SpaceUSB::USBHost_MSC;
	namespace USB = uni::device::SpaceUSB;

	constexpr uint8 EhciPciCommandOffset = 0x04u;
	constexpr uint16 EhciPciCommandMemorySpace = 1u << 1;
	constexpr uint16 EhciPciCommandBusMaster = 1u << 2;
	constexpr uint8 EhciControllerCapacity = 8;
	constexpr stduint EhciConfigurationCapacity = 512;
	constexpr stduint EhciStringCapacity = 64;
	constexpr uint16 EhciHotplugDebouncePolls = 10;
	constexpr uint16 EhciHotplugRetryPolls = 100;
	constexpr stduint EhciPollIntervalTicks = CONFIG_SysTickFreq >= 100 ?
		CONFIG_SysTickFreq / 100 : 1;

	struct EhciEnumeratedDevice {
		USB::DeviceDescriptor descriptor{};
		uint8 configuration[EhciConfigurationCapacity]{};
		char manufacturer[EhciStringCapacity]{};
		char product[EhciStringCapacity]{};
		char serial[EhciStringCapacity]{};
		uint16 configuration_length = 0;
		uint16 language_id = 0;
		uint8 port_index = 0;
		uint8 address = 0;
		uint8 max_packet_size = 0;
		uint8 mass_storage_interface = 0;
		uint8 bulk_in_endpoint = 0;
		uint8 bulk_out_endpoint = 0;
		uint16 bulk_in_max_packet_size = 0;
		uint16 bulk_out_max_packet_size = 0;
		bool mass_storage_ready = false;
		bool configured = false;
	};

	struct EhciDriverInstance;

	class EhciMassStorage final : public uni::StorageTrait {
	public:
		EhciMassStorage();
		~EhciMassStorage() override;
		bool Configure(EhciDriverInstance& controller,
			USBMassStorage& class_driver);
		void Disconnect();
		bool ScanPartitions();
		bool Read(stduint block, void* destination,
			stduint count = 1) override;
		bool Write(stduint block, const void* source,
			stduint count = 1) override;
		stduint getUnits() override;
		int operator[](uint64 offset) override;
		uni::PartitionSlice getSlice(stduint device) override;

		bool IsOnline() const {
			return online_.load(uni::MemoryOrder_Acquire) != 0;
		}
		const HD_Info& Partitions() const { return partitions_; }
		const USBMassStorage& ClassDriver() const { return *class_driver_; }
		uint16 MaximumBlocksPerTransfer() const;

	private:
		EhciDriverInstance* controller_ = nullptr;
		USBMassStorage* class_driver_ = nullptr;
		Mutex transaction_{};
		HD_Info partitions_{};
		stduint block_buffer_capacity_ = 0;
		stduint cached_block_ = stduint(-1);
		uni::Atomic<byte> partitions_valid_ = 0;
		uni::Atomic<byte> online_ = 0;
	};

	struct EhciDriverInstance {
		DeviceNode* node = nullptr;
		stduint mmio_base = 0;
		uint16 mmio_length = 0;
		uint32* periodic_list = nullptr;
		void* transfer_workspace = nullptr;
		EhciHostController host{};
		EhciRootPortStatus port_status[EhciHostController::kMaximumRootPorts]{};
		DeviceNode* port_nodes[EhciHostController::kMaximumRootPorts]{};
		uint16 port_wait_polls[EhciHostController::kMaximumRootPorts]{};
		uint32 controller_status = 0;
		uint8 irq_line = 0xFFu;
		uint8 irq_pin = 0;
		EhciEnumeratedDevice device{};
		EhciHostDevice* host_device = nullptr;
		EhciMassStorage* mass_storage = nullptr;
		DeviceNode* mass_storage_node = nullptr;
		bool running = false;
	};

	EhciDriverInstance ehci_controllers[EhciControllerCapacity]{};
	volatile bool ehci_poll_armed = false;

	bool EnumerateEhciDevice(EhciDriverInstance& controller,
		uint8 port_index);
	bool RegisterEhciEnumeratedDevice(EhciDriverInstance& controller,
		DeviceNode* port_node);
	bool ProbeEhciMassStorage(EhciDriverInstance& controller,
		EhciEnumeratedDevice& device);
	void RemoveEhciDeviceNode(EhciDriverInstance& controller,
		uint8 port_index, uint8 address);
	bool DeactivateEhciMassStorage(EhciDriverInstance& controller);

	EhciMassStorage::EhciMassStorage() {
		Block_buffer = nullptr;
		Block_Size = 0;
		readable = true;
		writable = true;
	}

	EhciMassStorage::~EhciMassStorage() {
		delete[] static_cast<byte*>(Block_buffer);
		Block_buffer = nullptr;
	}

	bool EhciMassStorage::Configure(EhciDriverInstance& controller,
		USBMassStorage& class_driver) {
		MutexLocal guard(&transaction_);
		online_.store(0, uni::MemoryOrder_Release);
		partitions_valid_.store(0, uni::MemoryOrder_Release);
		cached_block_ = stduint(-1);
		controller_ = &controller;
		class_driver_ = &class_driver;
		if (!class_driver_->IsReady()) {
			class_driver_ = nullptr;
			controller_ = nullptr;
			return false;
		}
		const stduint block_size = class_driver_->BlockSize();
		if (!Block_buffer || block_buffer_capacity_ < block_size) {
			auto* buffer = new byte[block_size];
			if (!buffer) {
				class_driver_ = nullptr;
				controller_ = nullptr;
				return false;
			}
			delete[] static_cast<byte*>(Block_buffer);
			Block_buffer = buffer;
			block_buffer_capacity_ = block_size;
		}
		Block_Size = block_size;
		partitions_ = {};
		partitions_.whole_disk.length = class_driver_->BlockCount();
		online_.store(1, uni::MemoryOrder_Release);
		return true;
	}

	void EhciMassStorage::Disconnect() {
		MutexLocal guard(&transaction_);
		online_.store(0, uni::MemoryOrder_Release);
		partitions_valid_.store(0, uni::MemoryOrder_Release);
		cached_block_ = stduint(-1);
		class_driver_ = nullptr;
		controller_ = nullptr;
	}

	uint16 EhciMassStorage::MaximumBlocksPerTransfer() const {
		if (!controller_ || !class_driver_ || !Block_Size) return 0;
		const stduint count = controller_->host.MaximumBulkTransferBytes() /
			Block_Size;
		return count > 0xFFFFu ? 0xFFFFu : uint16(count);
	}

	bool EhciMassStorage::Read(stduint block, void* destination,
		stduint count) {
		if (!destination || !count) return false;
		MutexLocal guard(&transaction_);
		if (!online_.load(uni::MemoryOrder_Acquire) || !controller_ ||
			!class_driver_ || !Block_Size ||
			block >= class_driver_->BlockCount() ||
			count > class_driver_->BlockCount() - block ||
			count > stduint(-1) / Block_Size) return false;
		const uint16 maximum = MaximumBlocksPerTransfer();
		if (!maximum) return false;
		auto* output = static_cast<byte*>(destination);
		stduint completed = 0;
		while (completed < count) {
			const stduint remaining = count - completed;
			const uint16 chunk = uint16(remaining > maximum ? maximum : remaining);
			const auto error = class_driver_->ReadBlocks(
				uint32(block + completed), chunk,
				output + completed * Block_Size);
			if (error || class_driver_->LastResult() !=
				USBMassStorage::Result::kOk) return false;
			completed += chunk;
		}
		MemCopyN(Block_buffer, output + (count - 1u) * Block_Size, Block_Size);
		cached_block_ = block + count - 1u;
		return true;
	}

	bool EhciMassStorage::Write(stduint block, const void* source,
		stduint count) {
		if (!source || !count) return false;
		MutexLocal guard(&transaction_);
		if (!online_.load(uni::MemoryOrder_Acquire) || !controller_ ||
			!class_driver_ || !Block_Size ||
			block >= class_driver_->BlockCount() ||
			count > class_driver_->BlockCount() - block ||
			count > stduint(-1) / Block_Size) return false;
		const uint16 maximum = MaximumBlocksPerTransfer();
		if (!maximum) return false;
		const auto* input = static_cast<const byte*>(source);
		cached_block_ = stduint(-1);
		stduint completed = 0;
		while (completed < count) {
			const stduint remaining = count - completed;
			const uint16 chunk = uint16(remaining > maximum ? maximum : remaining);
			const auto error = class_driver_->WriteBlocks(
				uint32(block + completed), chunk,
				input + completed * Block_Size);
			if (error || class_driver_->LastResult() !=
				USBMassStorage::Result::kOk) return false;
			completed += chunk;
		}
		return true;
	}

	stduint EhciMassStorage::getUnits() {
		return online_.load(uni::MemoryOrder_Acquire) && class_driver_ ?
			class_driver_->BlockCount() : 0;
	}

	int EhciMassStorage::operator[](uint64 offset) {
		MutexLocal guard(&transaction_);
		if (!online_.load(uni::MemoryOrder_Acquire) || !controller_ ||
			!class_driver_ ||
			!Block_Size || !Block_buffer ||
			offset >= uint64(class_driver_->BlockCount()) * Block_Size) return -1;
		const stduint block = stduint(offset / Block_Size);
		if (cached_block_ != block) {
			const auto error = class_driver_->ReadBlocks(uint32(block), 1,
				Block_buffer);
			if (error || class_driver_->LastResult() !=
				USBMassStorage::Result::kOk) return -1;
			cached_block_ = block;
		}
		return static_cast<byte*>(Block_buffer)[stduint(offset % Block_Size)];
	}

	bool EhciMassStorage::ScanPartitions() {
		if (!online_.load(uni::MemoryOrder_Acquire) ||
			!Block_buffer || !Block_Size) return false;
		if (!Read(0, Block_buffer)) return false;
		partitions_ = {};
		partitions_.whole_disk.length = class_driver_->BlockCount();
		uni::DiscPartition::Partition(*this, partitions_,
			static_cast<byte*>(Block_buffer), 0);
		partitions_valid_.store(1, uni::MemoryOrder_Release);
		return true;
	}

	uni::PartitionSlice EhciMassStorage::getSlice(stduint device) {
		if (!online_.load(uni::MemoryOrder_Acquire)) return {};
		if (!device) {
			uni::PartitionSlice slice{};
			slice.length = class_driver_ ? class_driver_->BlockCount() : 0;
			return slice;
		}
		return partitions_valid_.load(uni::MemoryOrder_Acquire) ?
			GetPartitionSlice(partitions_, device) : uni::PartitionSlice{};
	}

	const char* EhciNodeName(const EhciDriverInstance& controller) {
		return controller.node && controller.node->link.addr ?
			controller.node->link.addr : "(unnamed)";
	}

	EhciDriverInstance* AllocateEhciController(DeviceNode* node) {
		for (auto& controller : ehci_controllers) {
			if (controller.node == node) return &controller;
		}
		for (auto& controller : ehci_controllers) {
			if (controller.node) continue;
			controller = {};
			controller.node = node;
			return &controller;
		}
		return nullptr;
	}

	uint8 EhciRead8(void* context, uint16 offset) {
		auto& controller = *static_cast<EhciDriverInstance*>(context);
		return *reinterpret_cast<volatile uint8*>(controller.mmio_base + offset);
	}

	uint16 EhciRead16(void* context, uint16 offset) {
		auto& controller = *static_cast<EhciDriverInstance*>(context);
		return *reinterpret_cast<volatile uint16*>(controller.mmio_base + offset);
	}

	uint32 EhciRead32(void* context, uint16 offset) {
		auto& controller = *static_cast<EhciDriverInstance*>(context);
		return *reinterpret_cast<volatile uint32*>(controller.mmio_base + offset);
	}

	void EhciWrite32(void* context, uint16 offset, uint32 value) {
		auto& controller = *static_cast<EhciDriverInstance*>(context);
		*reinterpret_cast<volatile uint32*>(controller.mmio_base + offset) = value;
	}

	uint32 EhciReadPciConfig32(void* context, uint8 offset) {
		auto& controller = *static_cast<EhciDriverInstance*>(context);
		return uni::PCI::read_config_register(
			controller.node->fields.pci_bus,
			controller.node->fields.pci_device,
			controller.node->fields.pci_function, offset);
	}

	void EhciWritePciConfig32(void* context, uint8 offset, uint32 value) {
		auto& controller = *static_cast<EhciDriverInstance*>(context);
		uni::PCI::write_config_register(
			controller.node->fields.pci_bus,
			controller.node->fields.pci_device,
			controller.node->fields.pci_function, offset, value);
	}

	void EhciDelayMilliseconds(void*, stduint milliseconds) {
		TSC_Wait_MS(milliseconds);
	}

	void EhciSynchronizeMemory(void*) {
		_ASM volatile ("mfence" ::: "memory");
	}

	uni::device::SpaceUSB2::ControllerIO EhciControllerIO(
		EhciDriverInstance& controller) {
		return {
			&controller,
			EhciRead8,
			EhciRead16,
			EhciRead32,
			EhciWrite32,
			EhciReadPciConfig32,
			EhciWritePciConfig32,
			EhciDelayMilliseconds,
			EhciSynchronizeMemory,
		};
	}

	void ReleaseEhciPeriodicList(EhciDriverInstance& controller) {
		if (!controller.periodic_list) return;
		DmaLowFree(controller.periodic_list,
			EhciHostController::kPeriodicListBytes);
		controller.periodic_list = nullptr;
	}

	void ReleaseEhciTransferWorkspace(EhciDriverInstance& controller) {
		if (!controller.transfer_workspace) return;
		DmaLowFree(controller.transfer_workspace,
			EhciHostController::kTransferWorkspaceBytes);
		controller.transfer_workspace = nullptr;
	}

	bool AllocateEhciDma(EhciDriverInstance& controller) {
		if (controller.periodic_list || controller.transfer_workspace) return false;
		controller.periodic_list = static_cast<uint32*>(
			DmaLowAlloc(EhciHostController::kPeriodicListBytes));
		if (!controller.periodic_list) return false;
		controller.transfer_workspace = DmaLowAlloc(
			EhciHostController::kTransferWorkspaceBytes);
		if (!controller.transfer_workspace) {
			ReleaseEhciPeriodicList(controller);
			return false;
		}
		const uint64 periodic_address =
			uint64(stduint(controller.periodic_list));
		const uint64 workspace_address =
			uint64(stduint(controller.transfer_workspace));
		if ((periodic_address & 0xFFFu) ||
			periodic_address + EhciHostController::kPeriodicListBytes >
				0x100000000ull ||
			(workspace_address & 0xFFFu) ||
			workspace_address + EhciHostController::kTransferWorkspaceBytes >
				0x100000000ull) {
			plogwarn("[EHCI] %s low-DMA pages are invalid: periodic=%[64H] async=%[64H]",
				EhciNodeName(controller), periodic_address, workspace_address);
			ReleaseEhciTransferWorkspace(controller);
			ReleaseEhciPeriodicList(controller);
			return false;
		}
		return true;
	}

	bool EnableEhciPciCommand(const DeviceNode& node) {
		const uint32 command_status = uni::PCI::read_config_register(
			node.fields.pci_bus, node.fields.pci_device, node.fields.pci_function,
			EhciPciCommandOffset);
		const uint16 command = uint16(command_status) |
			EhciPciCommandMemorySpace | EhciPciCommandBusMaster;
		uni::PCI::write_config_register(
			node.fields.pci_bus, node.fields.pci_device, node.fields.pci_function,
			EhciPciCommandOffset, command);
		const uint16 current = uint16(uni::PCI::read_config_register(
			node.fields.pci_bus, node.fields.pci_device, node.fields.pci_function,
			EhciPciCommandOffset));
		return (current & (EhciPciCommandMemorySpace |
			EhciPciCommandBusMaster)) ==
			(EhciPciCommandMemorySpace | EhciPciCommandBusMaster);
	}

	void StopEhciController(EhciDriverInstance& controller) {
		controller.running = false;
		const uint8 port_index = controller.device.port_index;
		const uint8 address = controller.device.address;
		if (DeactivateEhciMassStorage(controller) && address &&
			port_index < controller.host.RootPortCount() &&
			controller.port_nodes[port_index]) {
			RemoveEhciDeviceNode(controller, port_index, address);
		}
		delete controller.host_device;
		controller.host_device = nullptr;
		controller.device = {};
		if (!controller.host.Stop()) {
			plogwarn("[EHCI] %s did not halt; retaining low-DMA pages",
				EhciNodeName(controller));
			return;
		}
		ReleaseEhciTransferWorkspace(controller);
		ReleaseEhciPeriodicList(controller);
	}

	void RemoveEhciDeviceNode(EhciDriverInstance& controller,
		uint8 port_index, uint8 address) {
		if (port_index >= controller.host.RootPortCount() ||
			!controller.port_nodes[port_index] || !address) return;
		auto device_name = String::newFormat("usb-dev@port%u.slot%u",
			(stduint)(port_index + 1), (stduint)address);
		Devsman::RemoveUSBDevice(controller.port_nodes[port_index],
			device_name.reference());
	}

	bool UnmountEhciStorageSubtree(DeviceNode* node) {
		if (!node) return true;
		for (auto* child = reinterpret_cast<DeviceNode*>(node->link.subf); child;
			child = reinterpret_cast<DeviceNode*>(child->link.next)) {
			if (!UnmountEhciStorageSubtree(child)) return false;
		}
		while (Filesys::CountMountsForSourceNode(node)) {
			auto path = Filesys::GetFirstMountPathForSourceNode(node);
			if (!path.getByteCount() || !Filesys::Unmount(path.reference())) {
				return false;
			}
		}
		return true;
	}

	void ReleaseEhciPartitionBindings(DeviceNode* storage_node) {
		if (!storage_node) return;
		for (auto* child = reinterpret_cast<DeviceNode*>(storage_node->link.subf);
			child; child = reinterpret_cast<DeviceNode*>(child->link.next)) {
			if (DeviceNodeType(child->fields.node_type) !=
				DeviceNodeType::StorageDevice ||
				!child->fields.binding.driver_name ||
				StrCompare(child->fields.binding.driver_name,
					"storage-partition") ||
				!child->fields.binding.driver_data) continue;
			delete static_cast<uni::DiscPartition*>(
				child->fields.binding.driver_data);
			child->fields.binding.driver_data = nullptr;
			child->fields.ops = nullptr;
		}
	}

	bool DeactivateEhciMassStorage(EhciDriverInstance& controller) {
		if (controller.mass_storage) controller.mass_storage->Disconnect();
		if (!controller.mass_storage_node) return true;
		if (!UnmountEhciStorageSubtree(controller.mass_storage_node)) {
			plogwarn("[EHCI] %s cannot unmount disconnected mass-storage device; retaining nodes",
				EhciNodeName(controller));
			return false;
		}
		ReleaseEhciPartitionBindings(controller.mass_storage_node);
		controller.mass_storage_node = nullptr;
		return true;
	}

	void DisconnectEhciDevice(EhciDriverInstance& controller) {
		auto& device = controller.device;
		if (!device.configured) return;
		const uint8 port_index = device.port_index;
		const uint8 address = device.address;
		if (DeactivateEhciMassStorage(controller)) {
			RemoveEhciDeviceNode(controller, port_index, address);
		}
		delete controller.host_device;
		controller.host_device = nullptr;
		device = {};
		ploginfo("[EHCI] %s port=%u device address=%u disconnected",
			EhciNodeName(controller), (stduint)(port_index + 1),
			(stduint)address);
	}

	void RetryEhciPortLater(EhciDriverInstance& controller,
		uint8 port_index) {
		if (port_index < controller.host.RootPortCount()) {
			controller.port_wait_polls[port_index] = EhciHotplugRetryPolls;
		}
		if (controller.device.configured &&
			controller.device.port_index != port_index) return;
		DeactivateEhciMassStorage(controller);
		delete controller.host_device;
		controller.host_device = nullptr;
		controller.device = {};
	}

	void RecoverEhciDeviceLater(EhciDriverInstance& controller) {
		auto& device = controller.device;
		const uint8 port_index = device.port_index;
		const uint8 address = device.address;
		if (DeactivateEhciMassStorage(controller)) {
			RemoveEhciDeviceNode(controller, port_index, address);
		}
		RetryEhciPortLater(controller, port_index);
	}

	void TryAttachEhciDevice(EhciDriverInstance& controller,
		uint8 port_index) {
		if (port_index >= controller.host.RootPortCount()) return;
		if (!controller.host.ResetRootPort(port_index)) {
			const auto status = controller.host.RootPortAt(port_index);
			if (status.valid && status.connected &&
				controller.host.RouteRootPortToCompanion(port_index)) {
				controller.host.AcknowledgeRootPortChanges(port_index);
				controller.port_status[port_index] =
					controller.host.RootPortAt(port_index);
				controller.port_wait_polls[port_index] = 0;
				ploginfo("[EHCI] %s port=%u handed to companion owner=1",
					EhciNodeName(controller), (stduint)(port_index + 1));
				return;
			}
			plogwarn("[EHCI] %s port=%u hotplug reset failed",
				EhciNodeName(controller), (stduint)(port_index + 1));
			RetryEhciPortLater(controller, port_index);
			return;
		}
		const auto status = controller.host.RootPortAt(port_index);
		controller.port_status[port_index] = status;
		if (!status.valid || !status.connected || !status.high_speed) {
			RetryEhciPortLater(controller, port_index);
			return;
		}
		if (controller.device.configured) {
			controller.port_wait_polls[port_index] = 0;
			plogwarn("[EHCI] %s port=%u additional high-speed device deferred",
				EhciNodeName(controller), (stduint)(port_index + 1));
			return;
		}
		if (!EnumerateEhciDevice(controller, port_index)) {
			plogwarn("[EHCI] %s port=%u hotplug enumeration failed; retrying",
				EhciNodeName(controller), (stduint)(port_index + 1));
			RetryEhciPortLater(controller, port_index);
			return;
		}
		if (!RegisterEhciEnumeratedDevice(controller,
			controller.port_nodes[port_index])) {
			plogwarn("[EHCI] %s port=%u hotplug device-node registration failed; retrying",
				EhciNodeName(controller), (stduint)(port_index + 1));
			RecoverEhciDeviceLater(controller);
			return;
		}
		controller.port_wait_polls[port_index] = 0;
		ploginfo("[EHCI] %s port=%u hotplug device attached address=%u",
			EhciNodeName(controller), (stduint)(port_index + 1),
			(stduint)controller.device.address);
	}

	void PollEhciControllers(pureptr_t, stduint) {
		ehci_poll_armed = false;
		bool have_running_controller = false;
		for (auto& controller : ehci_controllers) {
			if (!controller.running) continue;
			have_running_controller = true;
			const uint32 controller_status = controller.host.Status();
			if (!controller.host.IsRunning() &&
				controller_status != controller.controller_status) {
				plogwarn("[EHCI] %s stopped unexpectedly sts=%[32H] frame=%u",
					EhciNodeName(controller), controller_status,
					(stduint)controller.host.FrameIndex());
			}
			controller.controller_status = controller_status;
			for (uint8 index = 0; index < controller.host.RootPortCount(); ++index) {
				auto status = controller.host.RootPortAt(index);
				const auto previous = controller.port_status[index];
				if (status.connect_changed || status.enable_changed ||
					status.overcurrent_changed ||
					status.connected != previous.connected ||
					status.enabled != previous.enabled ||
					status.owned_by_companion != previous.owned_by_companion) {
					ploginfo("[EHCI] %s port=%u change connected=%u enabled=%u high-speed=%u owner=%u sts=%[32H]",
						EhciNodeName(controller), (stduint)(index + 1),
						(stduint)status.connected, (stduint)status.enabled,
						(stduint)status.high_speed,
						(stduint)status.owned_by_companion, status.raw);
					controller.host.AcknowledgeRootPortChanges(index);
				}
				if (status.connected && !previous.connected) {
					controller.port_wait_polls[index] = EhciHotplugDebouncePolls;
				}
				if (status.connect_changed && !status.connected &&
					status.owned_by_companion &&
					controller.host.ReclaimRootPortFromCompanion(index)) {
					status = controller.host.RootPortAt(index);
					ploginfo("[EHCI] %s port=%u reclaimed from companion owner=0",
						EhciNodeName(controller), (stduint)(index + 1));
				}
				if (controller.device.configured &&
					controller.device.port_index == index) {
					if (!status.connected) {
						DisconnectEhciDevice(controller);
					} else if (!status.high_speed) {
						plogwarn("[EHCI] %s port=%u unavailable; re-enumerating",
							EhciNodeName(controller), (stduint)(index + 1));
						RecoverEhciDeviceLater(controller);
					}
				}
				if (!status.connected || status.owned_by_companion) {
					controller.port_wait_polls[index] = 0;
				}
				controller.port_status[index] = status;
				if (status.valid && status.connected &&
					!status.owned_by_companion &&
					(!status.enabled || controller.port_wait_polls[index])) {
					if (controller.port_wait_polls[index]) {
						--controller.port_wait_polls[index];
					}
					if (!controller.port_wait_polls[index]) {
						TryAttachEhciDevice(controller, index);
					}
				}
			}
		}
		if (have_running_controller) {
			ehci_poll_armed = Systimex::AppendDeferredCallback(
				EhciPollIntervalTicks, 0, (_tocall_ft)PollEhciControllers);
			if (!ehci_poll_armed) {
				plogerro("[EHCI] cannot re-arm deferred polling");
			}
		}
	}

	bool ArmEhciPolling() {
		if (ehci_poll_armed) return true;
		ehci_poll_armed = Systimex::AppendDeferredCallback(
			EhciPollIntervalTicks, 0, (_tocall_ft)PollEhciControllers);
		if (!ehci_poll_armed) {
			plogerro("[EHCI] cannot arm deferred polling");
		}
		return ehci_poll_armed;
	}

	bool ValidateEhciConfiguration(const uint8* data, uint16 length) {
		if (!data || length < sizeof(USB::ConfigurationDescriptor)) return false;
		stduint offset = 0;
		while (offset < length) {
			if (length - offset < 2) return false;
			const uint8 descriptor_length = data[offset];
			const uint8 descriptor_type = data[offset + 1];
			if (descriptor_length < 2 || descriptor_length > length - offset) {
				return false;
			}
			if (offset == 0 &&
				(descriptor_type != USB::ConfigurationDescriptor::kType ||
				 descriptor_length < sizeof(USB::ConfigurationDescriptor))) {
				return false;
			}
			if (descriptor_type == USB::InterfaceDescriptor::kType &&
				descriptor_length < sizeof(USB::InterfaceDescriptor)) return false;
			if (descriptor_type == USB::EndpointDescriptor::kType &&
				descriptor_length < sizeof(USB::EndpointDescriptor)) return false;
			offset += descriptor_length;
		}
		return offset == length;
	}

	bool ReadEhciDeviceDescriptor(EhciDriverInstance& controller,
		uint8 port_index, EhciEnumeratedDevice& device) {
		USB::SetupData setup{};
		setup.request_type.data = 0x80u;
		setup.request = static_cast<uint8>(USB::StandardRequest::GetDescriptor);
		setup.value = uint16(USB::DeviceDescriptor::kType << 8);
		setup.length = 8;
		uint8 prefix[8]{};
		uint16 actual_length = 0;
		auto error = controller.host.ControlIn(0, 0, 64, setup,
			prefix, sizeof(prefix), actual_length, 500);
		if (error != EhciControllerError::Success) {
			plogwarn("[EHCI] %s port=%u GET_DESCRIPTOR(8) failed: %s qtd=%u status=%[32H]",
				EhciNodeName(controller), (stduint)(port_index + 1),
				EhciHostController::ErrorName(error),
				(stduint)controller.host.LastTransferDescriptorIndex(),
				controller.host.LastTransferStatus());
			return false;
		}
		if (actual_length != sizeof(prefix) ||
			prefix[0] != sizeof(USB::DeviceDescriptor) ||
			prefix[1] != USB::DeviceDescriptor::kType || prefix[7] != 64) {
			plogwarn("[EHCI] %s port=%u invalid high-speed descriptor prefix len=%u actual=%u type=%u mps0=%u",
				EhciNodeName(controller), (stduint)(port_index + 1),
				(stduint)prefix[0], (stduint)actual_length,
				(stduint)prefix[1], (stduint)prefix[7]);
			return false;
		}

		setup.length = sizeof(USB::DeviceDescriptor);
		USB::DeviceDescriptor descriptor{};
		error = controller.host.ControlIn(0, 0, 64, setup,
			reinterpret_cast<uint8*>(&descriptor), sizeof(descriptor),
			actual_length, 500);
		if (error != EhciControllerError::Success ||
			actual_length != sizeof(descriptor) ||
			descriptor.length != sizeof(descriptor) ||
			descriptor.descriptor_type != USB::DeviceDescriptor::kType ||
			descriptor.max_packet_size != 64) {
			plogwarn("[EHCI] %s port=%u GET_DESCRIPTOR(18) invalid: %s actual=%u qtd=%u status=%[32H]",
				EhciNodeName(controller), (stduint)(port_index + 1),
				EhciHostController::ErrorName(error), (stduint)actual_length,
				(stduint)controller.host.LastTransferDescriptorIndex(),
				controller.host.LastTransferStatus());
			return false;
		}
		device.descriptor = descriptor;
		device.port_index = port_index;
		device.max_packet_size = descriptor.max_packet_size;
		ploginfo("[EHCI] %s port=%u high-speed device usb=%[16H] vid=%[16H] pid=%[16H] class=%u/%u/%u mps0=%u configs=%u",
			EhciNodeName(controller), (stduint)(port_index + 1),
			descriptor.usb_release, descriptor.vendor_id, descriptor.product_id,
			(stduint)descriptor.device_class,
			(stduint)descriptor.device_sub_class,
			(stduint)descriptor.device_protocol,
			(stduint)descriptor.max_packet_size,
			(stduint)descriptor.num_configurations);
		return true;
	}

	bool SetEhciDeviceAddress(EhciDriverInstance& controller,
		EhciEnumeratedDevice& device) {
		USB::SetupData setup{};
		setup.request_type.data = 0x00u;
		setup.request = static_cast<uint8>(USB::StandardRequest::SetAddress);
		setup.value = USB::kDefaultDeviceAddress;
		const auto error = controller.host.ControlNoData(0, 0, 64,
			setup, 500);
		if (error != EhciControllerError::Success) {
			plogwarn("[EHCI] %s port=%u SET_ADDRESS(%u) failed: %s qtd=%u status=%[32H]",
				EhciNodeName(controller), (stduint)(device.port_index + 1),
				(stduint)USB::kDefaultDeviceAddress,
				EhciHostController::ErrorName(error),
				(stduint)controller.host.LastTransferDescriptorIndex(),
				controller.host.LastTransferStatus());
			return false;
		}
		TSC_Wait_MS(2);
		device.address = USB::kDefaultDeviceAddress;
		return true;
	}

	bool ReadEhciConfiguration(EhciDriverInstance& controller,
		EhciEnumeratedDevice& device) {
		USB::SetupData setup{};
		setup.request_type.data = 0x80u;
		setup.request = static_cast<uint8>(USB::StandardRequest::GetDescriptor);
		setup.value = uint16(USB::ConfigurationDescriptor::kType << 8);
		setup.length = sizeof(USB::ConfigurationDescriptor);
		USB::ConfigurationDescriptor header{};
		uint16 actual_length = 0;
		auto error = controller.host.ControlIn(device.address, 0, 64,
			setup, reinterpret_cast<uint8*>(&header), sizeof(header),
			actual_length, 500);
		if (error != EhciControllerError::Success ||
			actual_length != sizeof(header) ||
			header.length != sizeof(header) ||
			header.descriptor_type != USB::ConfigurationDescriptor::kType ||
			header.total_length < sizeof(header) ||
			header.total_length > EhciConfigurationCapacity ||
			!header.configuration_value) {
			plogwarn("[EHCI] %s port=%u invalid configuration header: %s actual=%u total=%u value=%u",
				EhciNodeName(controller), (stduint)(device.port_index + 1),
				EhciHostController::ErrorName(error), (stduint)actual_length,
				(stduint)header.total_length,
				(stduint)header.configuration_value);
			return false;
		}
		setup.length = header.total_length;
		error = controller.host.ControlIn(device.address, 0, 64,
			setup, device.configuration, header.total_length,
			actual_length, 500);
		if (error != EhciControllerError::Success ||
			actual_length != header.total_length ||
			!ValidateEhciConfiguration(device.configuration, actual_length)) {
			plogwarn("[EHCI] %s port=%u GET_CONFIGURATION(%u) failed: %s actual=%u qtd=%u status=%[32H]",
				EhciNodeName(controller), (stduint)(device.port_index + 1),
				(stduint)header.total_length,
				EhciHostController::ErrorName(error), (stduint)actual_length,
				(stduint)controller.host.LastTransferDescriptorIndex(),
				controller.host.LastTransferStatus());
			return false;
		}
		const auto* configuration =
			reinterpret_cast<const USB::ConfigurationDescriptor*>(
				device.configuration);
		if (configuration->total_length != header.total_length ||
			configuration->configuration_value != header.configuration_value) {
			plogwarn("[EHCI] %s port=%u configuration header changed during read",
				EhciNodeName(controller), (stduint)(device.port_index + 1));
			return false;
		}
		device.configuration_length = actual_length;
		return true;
	}

	void DecodeEhciString(const uint8* descriptor, uint16 length,
		char* output, stduint capacity) {
		if (!output || !capacity) return;
		output[0] = 0;
		if (!descriptor || length < 2 || descriptor[1] !=
			USB::descriptor_type::kString) return;
		stduint output_length = 0;
		const stduint descriptor_length = descriptor[0] < length ?
			descriptor[0] : length;
		for (stduint offset = 2; offset + 1 < descriptor_length &&
			output_length + 1 < capacity; offset += 2) {
			const uint8 low = descriptor[offset];
			const uint8 high = descriptor[offset + 1];
			output[output_length++] = high == 0 && low >= 0x20u && low < 0x7Fu ?
				char(low) : '?';
		}
		output[output_length] = 0;
	}

	bool ReadEhciString(EhciDriverInstance& controller,
		EhciEnumeratedDevice& device, uint8 index, char* output,
		stduint capacity) {
		if (!index) return true;
		USB::SetupData setup{};
		setup.request_type.data = 0x80u;
		setup.request = static_cast<uint8>(USB::StandardRequest::GetDescriptor);
		setup.value = uint16((USB::descriptor_type::kString << 8) | index);
		setup.index = device.language_id;
		setup.length = 2;
		uint8 header[2]{};
		uint16 actual_length = 0;
		auto error = controller.host.ControlIn(device.address, 0, 64,
			setup, header, sizeof(header), actual_length, 500);
		if (error != EhciControllerError::Success || actual_length != 2 ||
			header[0] < 2 || header[1] != USB::descriptor_type::kString) {
			return false;
		}
		uint8 descriptor[128]{};
		const uint16 descriptor_length = header[0] <= sizeof(descriptor) ?
			header[0] : sizeof(descriptor);
		setup.length = descriptor_length;
		error = controller.host.ControlIn(device.address, 0, 64,
			setup, descriptor, descriptor_length, actual_length, 500);
		if (error != EhciControllerError::Success || actual_length < 2 ||
			descriptor[1] != USB::descriptor_type::kString) return false;
		DecodeEhciString(descriptor, actual_length, output, capacity);
		return true;
	}

	void ReadEhciStrings(EhciDriverInstance& controller,
		EhciEnumeratedDevice& device) {
		if (!device.descriptor.manufacturer && !device.descriptor.product &&
			!device.descriptor.serial_number) return;
		USB::SetupData setup{};
		setup.request_type.data = 0x80u;
		setup.request = static_cast<uint8>(USB::StandardRequest::GetDescriptor);
		setup.value = uint16(USB::descriptor_type::kString << 8);
		setup.length = 4;
		uint8 language[4]{};
		uint16 actual_length = 0;
		const auto error = controller.host.ControlIn(device.address, 0, 64,
			setup, language, sizeof(language), actual_length, 500);
		if (error != EhciControllerError::Success || actual_length < 4 ||
			language[1] != USB::descriptor_type::kString) {
			plogwarn("[EHCI] %s port=%u string language descriptor unavailable",
				EhciNodeName(controller), (stduint)(device.port_index + 1));
			return;
		}
		device.language_id = uint16(language[2]) | (uint16(language[3]) << 8);
		bool strings_ok = ReadEhciString(controller, device,
			device.descriptor.manufacturer, device.manufacturer,
			sizeof(device.manufacturer));
		strings_ok = ReadEhciString(controller, device,
			device.descriptor.product, device.product,
			sizeof(device.product)) && strings_ok;
		strings_ok = ReadEhciString(controller, device,
			device.descriptor.serial_number, device.serial,
			sizeof(device.serial)) && strings_ok;
		if (!strings_ok) {
			plogwarn("[EHCI] %s port=%u one or more string descriptors unavailable",
				EhciNodeName(controller), (stduint)(device.port_index + 1));
		}
	}

	bool SetEhciConfiguration(EhciDriverInstance& controller,
		EhciEnumeratedDevice& device) {
		const auto* configuration =
			reinterpret_cast<const USB::ConfigurationDescriptor*>(
				device.configuration);
		USB::SetupData setup{};
		setup.request_type.data = 0x00u;
		setup.request = static_cast<uint8>(USB::StandardRequest::SetConfiguration);
		setup.value = configuration->configuration_value;
		const auto error = controller.host.ControlNoData(device.address, 0, 64,
			setup, 500);
		if (error != EhciControllerError::Success) {
			plogwarn("[EHCI] %s port=%u SET_CONFIGURATION(%u) failed: %s qtd=%u status=%[32H]",
				EhciNodeName(controller), (stduint)(device.port_index + 1),
				(stduint)configuration->configuration_value,
				EhciHostController::ErrorName(error),
				(stduint)controller.host.LastTransferDescriptorIndex(),
				controller.host.LastTransferStatus());
			return false;
		}
		device.configured = true;
		ploginfo("[EHCI] %s port=%u configured address=%u config=%u total=%u interfaces=%u",
			EhciNodeName(controller), (stduint)(device.port_index + 1),
			(stduint)device.address,
			(stduint)configuration->configuration_value,
			(stduint)configuration->total_length,
			(stduint)configuration->num_interfaces);
		return true;
	}

	bool ProbeEhciMassStorage(EhciDriverInstance& controller,
		EhciEnumeratedDevice& device) {
		const uint8* position = device.configuration;
		const uint8* const end = position + device.configuration_length;
		position += position[0];
		bool mass_storage_interface = false;
		bool mass_storage_found = false;
		while (position < end) {
			const uint8 descriptor_length = position[0];
			const uint8 descriptor_type = position[1];
			if (descriptor_type == USB::InterfaceDescriptor::kType) {
				const auto* descriptor =
					reinterpret_cast<const USB::InterfaceDescriptor*>(position);
				mass_storage_interface = !descriptor->alternate_setting &&
					descriptor->interface_class == 8u &&
					descriptor->interface_sub_class == 6u &&
					descriptor->interface_protocol == 0x50u;
				if (mass_storage_interface) {
					mass_storage_found = true;
					device.mass_storage_interface = descriptor->interface_number;
				}
			} else if (mass_storage_interface &&
				descriptor_type == USB::EndpointDescriptor::kType) {
				const auto* descriptor =
					reinterpret_cast<const USB::EndpointDescriptor*>(position);
				const uint16 max_packet_size =
					descriptor->max_packet_size & 0x07FFu;
				if (descriptor->attributes.bits.transfer_type ==
					uint8(USB::EndpointType::kBulk) &&
					descriptor->endpoint_address.bits.number &&
					max_packet_size && max_packet_size <= 512u) {
					if (descriptor->endpoint_address.bits.dir_in) {
						device.bulk_in_endpoint =
							descriptor->endpoint_address.bits.number;
						device.bulk_in_max_packet_size = max_packet_size;
					} else {
						device.bulk_out_endpoint =
							descriptor->endpoint_address.bits.number;
						device.bulk_out_max_packet_size = max_packet_size;
					}
				}
			}
			position += descriptor_length;
		}
		if (!mass_storage_found) return true;
		if (!device.bulk_in_endpoint || !device.bulk_out_endpoint) {
			plogwarn("[EHCI] %s port=%u mass-storage bulk endpoint pair incomplete",
				EhciNodeName(controller), (stduint)(device.port_index + 1));
			return false;
		}
		if (!controller.mass_storage) {
			controller.mass_storage = new EhciMassStorage;
			if (!controller.mass_storage) {
				plogwarn("[EHCI] %s port=%u cannot allocate mass-storage state",
					EhciNodeName(controller), (stduint)(device.port_index + 1));
				return false;
			}
		}
		if (!controller.host_device) return false;
		auto* class_driver = static_cast<USBMassStorage*>(
			controller.host_device->ClassDriverOf(device.bulk_in_endpoint));
		if (!class_driver || class_driver->OnEndpointsConfigured() ||
			!controller.mass_storage->Configure(controller,
			*class_driver)) {
			plogwarn("[EHCI] %s port=%u mass-storage class driver is not ready result=%s status=%[32H]",
				EhciNodeName(controller), (stduint)(device.port_index + 1),
				class_driver ? class_driver->ResultName() : "missing",
				controller.host.LastTransferStatus());
			return false;
		}

		const auto& storage = controller.mass_storage->ClassDriver();
		device.mass_storage_ready = true;
		ploginfo("[EHCI] %s port=%u mass-storage ready vendor=%s product=%s revision=%s blocks=%u block-size=%u bulk-in=%u bulk-out=%u mps=%u/%u",
			EhciNodeName(controller), (stduint)(device.port_index + 1),
			storage.Vendor(), storage.Product(), storage.Revision(),
			(stduint)storage.BlockCount(), (stduint)storage.BlockSize(),
			(stduint)device.bulk_in_endpoint,
			(stduint)device.bulk_out_endpoint,
			(stduint)device.bulk_in_max_packet_size,
			(stduint)device.bulk_out_max_packet_size);
		return true;
	}

	bool EnumerateEhciDevice(EhciDriverInstance& controller,
		uint8 port_index) {
		delete controller.host_device;
		controller.host_device = nullptr;
		controller.device = {};
		auto* host_device = new EhciHostDevice(controller.host);
		if (!host_device) return false;
		controller.host_device = host_device;
		const auto initialize_error = host_device->StartInitialize();
		if (initialize_error || !host_device->IsEnumerated()) {
			delete controller.host_device;
			controller.host_device = nullptr;
			return false;
		}

		auto& device = controller.device;
		device.port_index = port_index;
		device.address = host_device->DeviceAddress();
		device.max_packet_size = 64;
		device.descriptor.length = sizeof(USB::DeviceDescriptor);
		device.descriptor.descriptor_type = USB::DeviceDescriptor::kType;
		device.descriptor.vendor_id = host_device->VendorID();
		device.descriptor.product_id = host_device->ProductID();
		device.descriptor.device_class = host_device->DeviceClass();
		device.descriptor.device_sub_class = host_device->DeviceSubClass();
		device.descriptor.device_protocol = host_device->DeviceProtocol();
		device.descriptor.max_packet_size = 64;
		device.descriptor.num_configurations = 1;
		const auto* configuration = reinterpret_cast<
			const USB::ConfigurationDescriptor*>(host_device->Buffer());
		if (configuration->descriptor_type != USB::ConfigurationDescriptor::kType ||
			configuration->total_length > sizeof(device.configuration) ||
			!ValidateEhciConfiguration(host_device->Buffer(),
				configuration->total_length)) {
			delete controller.host_device;
			controller.host_device = nullptr;
			device = {};
			return false;
		}
		device.configuration_length = configuration->total_length;
		MemCopyN(device.configuration, host_device->Buffer(),
			device.configuration_length);
		auto copy_string = [](char* destination, stduint capacity,
			const char* source) {
			if (!capacity) return;
			stduint index = 0;
			if (source) {
				while (source[index] && index + 1 < capacity) {
					destination[index] = source[index];
					++index;
				}
			}
			destination[index] = 0;
		};
		copy_string(device.manufacturer, sizeof(device.manufacturer),
			host_device->ManufacturerString());
		copy_string(device.product, sizeof(device.product),
			host_device->ProductString());
		copy_string(device.serial, sizeof(device.serial),
			host_device->SerialString());
		device.configured = true;
		if (host_device->IsInitialized()) {
			if (auto error = host_device->ConfigureTransportEndpoints()) return false;
		}
		return ProbeEhciMassStorage(controller, controller.device);
	}

	const char* EhciDeviceDriverName(const USB::DeviceDescriptor& descriptor) {
		return descriptor.device_class == 0x09u ? "usb-hub" : "usb-device";
	}

	const char* EhciInterfaceDriverName(
		const USB::InterfaceDescriptor& descriptor) {
		if (descriptor.interface_class == 8u &&
			descriptor.interface_sub_class == 6u &&
			descriptor.interface_protocol == 0x50u) return "usb-mass-storage";
		return "usb-interface";
	}

	bool RegisterEhciMassStorageNode(EhciDriverInstance& controller,
		DeviceNode* interface_node) {
		if (!controller.device.mass_storage_ready) return true;
		if (!controller.mass_storage || !controller.mass_storage->IsOnline() ||
			!interface_node) return false;
		auto* storage_node = Devsman::RegisterStorageDevice(interface_node,
			"usb-storage@0", DeviceBusType::USB, "ehci-mass-storage",
			controller.mass_storage);
		if (!storage_node) return false;
		controller.mass_storage_node = storage_node;
		auto alias = String::newFormat("usb-storage%u",
			(stduint)(&controller - ehci_controllers));
		if (!Devsman::RegisterDevAlias(storage_node, alias.reference())) {
			plogwarn("[EHCI] %s cannot register storage alias %s",
				EhciNodeName(controller), alias.reference());
		}
		if (!controller.mass_storage->ScanPartitions()) return false;
		const auto& partitions = controller.mass_storage->Partitions();
		for (stduint partition = 1; partition <= partitions.part_count;
			++partition) {
			const auto slice = GetPartitionSlice(partitions, partition);
			if (!slice.length || !slice.sys_id || slice.sys_id == Part_EX_PART) {
				continue;
			}
			auto name = String::newFormat("partition@%u", partition);
			auto* partition_node = Devsman::RegisterStoragePartition(
				storage_node, name.reference(), *controller.mass_storage,
				partition);
			if (!partition_node) return false;
			auto mount_path = String::newFormat("/mnt/ehci%u.%u",
				(stduint)(&controller - ehci_controllers), partition);
			if (auto* filesystem = Filesys::Mount(*controller.mass_storage,
				partition, mount_path.reference(), partition_node)) {
				ploginfo("[EHCI] mount %s on %s", filesystem->name,
					mount_path.reference());
			} else {
				plogwarn("[EHCI] cannot mount partition=%u type=%[8H] on %s",
					partition, (stduint)slice.sys_id,
					mount_path.reference());
			}
		}
		const auto& class_driver = controller.mass_storage->ClassDriver();
		ploginfo("[EHCI] %s port=%u storage registered blocks=%u block-size=%u partitions=%u max-transfer-blocks=%u",
			EhciNodeName(controller),
			(stduint)(controller.device.port_index + 1),
			(stduint)class_driver.BlockCount(),
			(stduint)class_driver.BlockSize(),
			(stduint)partitions.part_count,
			(stduint)controller.mass_storage->MaximumBlocksPerTransfer());
		return true;
	}

	bool RegisterEhciEnumeratedDevice(EhciDriverInstance& controller,
		DeviceNode* port_node) {
		auto& device = controller.device;
		if (!device.configured) return true;
		if (!port_node) return false;
		auto device_name = String::newFormat("usb-dev@port%u.slot%u",
			(stduint)(device.port_index + 1), (stduint)device.address);
		auto* device_node = Devsman::RegisterUSBDevice(port_node,
			device_name.reference(), device.descriptor.vendor_id,
			device.descriptor.product_id,
			device.manufacturer[0] ? device.manufacturer : nullptr,
			device.product[0] ? device.product : nullptr,
			device.serial[0] ? device.serial : nullptr,
			device.descriptor.device_class,
			device.descriptor.device_sub_class,
			device.descriptor.device_protocol,
			uint8(device.port_index + 1), device.address,
			EhciDeviceDriverName(device.descriptor), controller.host_device);
		if (!device_node) return false;

		const uint8* position = device.configuration;
		const uint8* const end = position + device.configuration_length;
		position += position[0];
		DeviceNode* interface_node = nullptr;
		DeviceNode* mass_storage_interface_node = nullptr;
		uint32 endpoint_index = 0;
		while (position < end) {
			const uint8 descriptor_length = position[0];
			const uint8 descriptor_type = position[1];
			if (descriptor_type == USB::InterfaceDescriptor::kType) {
				const auto* descriptor =
					reinterpret_cast<const USB::InterfaceDescriptor*>(position);
				interface_node = nullptr;
				endpoint_index = 0;
				if (!descriptor->alternate_setting) {
					auto interface_name = String::newFormat("usb-if@%u",
						(stduint)descriptor->interface_number);
					interface_node = Devsman::RegisterUSBInterface(device_node,
						interface_name.reference(), descriptor->interface_class,
						descriptor->interface_sub_class,
						descriptor->interface_protocol,
						EhciInterfaceDriverName(*descriptor),
						controller.host_device);
					if (!interface_node) return false;
					if (descriptor->interface_number ==
						device.mass_storage_interface &&
						descriptor->interface_class == 8u &&
						descriptor->interface_sub_class == 6u &&
						descriptor->interface_protocol == 0x50u) {
						mass_storage_interface_node = interface_node;
					}
				}
			} else if (descriptor_type == USB::EndpointDescriptor::kType &&
				interface_node) {
				const auto* descriptor =
					reinterpret_cast<const USB::EndpointDescriptor*>(position);
				if (!Devsman::AddUSBEndpointResource(interface_node,
					endpoint_index++, descriptor->endpoint_address.data,
					descriptor->attributes.bits.transfer_type,
					descriptor->max_packet_size, descriptor->interval)) {
					return false;
				}
			}
			position += descriptor_length;
		}
		return RegisterEhciMassStorageNode(controller,
			mass_storage_interface_node);
	}

	bool RegisterEhciDeviceNodes(EhciDriverInstance& controller) {
		auto usb_bus_name = String::newFormat("usb-bus@%04x:%02x:%02x.%x", 0,
			(stduint)controller.node->fields.pci_bus,
			(stduint)controller.node->fields.pci_device,
			(stduint)controller.node->fields.pci_function);
		auto* usb_bus_node = Devsman::RegisterUSBBus(usb_bus_name.reference(),
			"ehci", &controller.host);
		if (!usb_bus_node) return false;
		auto* root_hub = Devsman::RegisterUSBRootHub(usb_bus_node,
			"usb-root-hub@0", 0x09u, 0x00u, 0x01u,
			"usb-root-hub", &controller.host);
		if (!root_hub) return false;
		for (uint8 port = 1; port <= controller.host.RootPortCount(); ++port) {
			auto port_name = String::newFormat("usb-port@%u", (stduint)port);
			auto* port_node = Devsman::RegisterUSBPort(root_hub,
				port_name.reference(), port);
			if (!port_node) return false;
			controller.port_nodes[port - 1u] = port_node;
			if (controller.device.configured &&
				controller.device.port_index + 1u == port &&
				!RegisterEhciEnumeratedDevice(controller, port_node)) return false;
		}
		return true;
	}

	bool StartEhciDriver(DeviceNode* node) {
		if (!node) return false;
		const auto* mmio = Devsman::FindResource(node,
			DeviceResourceType::PciBarMmio, 0);
		if (!mmio || !mmio->start || !mmio->length ||
			mmio->length > 0xFFFFu || mmio->start > uint64(~stduint(0)) ||
			mmio->start + mmio->length < mmio->start ||
			mmio->start + mmio->length > uint64(~stduint(0))) {
			plogwarn("[EHCI] %s has invalid BAR0 MMIO resource",
				node->link.addr ? node->link.addr : "(unnamed)");
			return false;
		}
		auto* controller = AllocateEhciController(node);
		if (!controller) {
			plogwarn("[EHCI] controller capacity exhausted");
			return false;
		}
		controller->mmio_base = stduint(mmio->start);
		controller->mmio_length = uint16(mmio->length);
		if (const auto* irq = Devsman::FindResource(node,
			DeviceResourceType::IrqLine, 0)) {
			controller->irq_line = uint8(irq->start);
			controller->irq_pin = uint8(irq->extra);
		}
		if (!EnableEhciPciCommand(*node)) {
			plogwarn("[EHCI] %s failed to enable PCI memory and bus mastering",
				EhciNodeName(*controller));
			return false;
		}
		if (!AllocateEhciDma(*controller)) {
			plogwarn("[EHCI] %s cannot reserve two 4K low-DMA schedule pages",
				EhciNodeName(*controller));
			return false;
		}
		controller->host.Bind(EhciControllerIO(*controller),
			controller->mmio_length);
		auto error = controller->host.Initialize(controller->periodic_list,
			uint32(stduint(controller->periodic_list)),
			controller->transfer_workspace,
			uint32(stduint(controller->transfer_workspace)),
			EhciHostController::kTransferWorkspaceBytes);
		if (error != EhciControllerError::Success) {
			plogwarn("[EHCI] %s initialize failed: %s cmd=%[32H] sts=%[32H]",
				EhciNodeName(*controller), EhciHostController::ErrorName(error),
				controller->host.Command(), controller->host.Status());
			ReleaseEhciTransferWorkspace(*controller);
			ReleaseEhciPeriodicList(*controller);
			return false;
		}
		error = controller->host.Run();
		if (error != EhciControllerError::Success) {
			plogwarn("[EHCI] %s schedule start failed: %s cmd=%[32H] sts=%[32H] frame=%u",
				EhciNodeName(*controller), EhciHostController::ErrorName(error),
				controller->host.Command(), controller->host.Status(),
				(stduint)controller->host.FrameIndex());
			StopEhciController(*controller);
			return false;
		}
		ploginfo("[EHCI] %s frame index advanced, current=%u version=%[16H]",
			EhciNodeName(*controller),
			(stduint)controller->host.FrameIndex(),
			controller->host.InterfaceVersion());

		bool device_enumerated = false;
		for (uint8 index = 0; index < controller->host.RootPortCount(); ++index) {
			const auto before = controller->host.RootPortAt(index);
			const bool reset_ok = before.connected &&
				controller->host.ResetRootPort(index);
			auto status = controller->host.RootPortAt(index);
			if (before.connected && !reset_ok && status.valid &&
				status.connected &&
				controller->host.RouteRootPortToCompanion(index)) {
				controller->host.AcknowledgeRootPortChanges(index);
				status = controller->host.RootPortAt(index);
			}
			controller->port_status[index] = status;
			
			#if 0
			ploginfo("[EHCI] %s port=%u connected=%u enabled=%u high-speed=%u owner=%u line=%u reset=%s sts=%[32H]",
				EhciNodeName(*controller), (stduint)(index + 1),
				(stduint)status.connected, (stduint)status.enabled,
				(stduint)status.high_speed,
				(stduint)status.owned_by_companion,
				(stduint)status.line_status,
				!before.connected ? "idle" :
					(status.owned_by_companion ? "companion" :
						(reset_ok ? "high-speed" : "failed")),
				status.raw);
			#endif

			if (!status.high_speed) continue;
			if (!device_enumerated) {
				if (!EnumerateEhciDevice(*controller, index)) {
					StopEhciController(*controller);
					return false;
				}
				device_enumerated = true;
			} else {
				plogwarn("[EHCI] %s port=%u additional high-speed device deferred",
					EhciNodeName(*controller), (stduint)(index + 1));
			}
		}

		controller->running = true;
		controller->controller_status = controller->host.Status();
		node->fields.binding.driver_data = &controller->host;
		if (!RegisterEhciDeviceNodes(*controller)) {
			StopEhciController(*controller);
			node->fields.binding.driver_data = nullptr;
			return false;
		}
		ArmEhciPolling();
		ploginfo("[EHCI] %s started MMIO=%[64H] periodic=%[64H] async=%[64H] ports=%u companions=%u ports-per-companion=%u IRQ line=%u pin=%u frame=%u",
			EhciNodeName(*controller), uint64(controller->mmio_base),
			uint64(stduint(controller->periodic_list)),
			uint64(stduint(controller->transfer_workspace)),
			(stduint)controller->host.RootPortCount(),
			(stduint)controller->host.CompanionControllerCount(),
			(stduint)controller->host.CompanionPortsPerController(),
			(stduint)controller->irq_line, (stduint)controller->irq_pin,
			(stduint)controller->host.FrameIndex());
		return true;
	}
}

_ESYM_C void R_EHCI_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_EHCI{
	.init = R_EHCI_INIT,
	.name = "EHCI",
};

void R_EHCI_INIT() {
	if (!Devsman::RegisterDriverStarter("ehci", StartEhciDriver)) {
		plogwarn("[EHCI] failed to register driver starter");
		return;
	}
	Devsman::StartKnownDrivers();
}
#endif
