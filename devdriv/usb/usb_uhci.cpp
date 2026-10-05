// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: UHCI Bring-up
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"

#if _MCCA == 0x8664 && defined(_UEFI)
#include <c/driver/keyboard.h>
#include <cpp/Device/Bus/PCI.hpp>
#include <cpp/Device/USB/USBHost-HID.hpp>
#include <cpp/Device/USB/uHCI/uHCI.hpp>

void TSC_Wait_MS(uint64_t ms);
void hand_kboard(keyboard_event_t event);

namespace {
	using UhciHostController = uni::device::SpaceUSB1::HostController;
	using UhciControllerError = uni::device::SpaceUSB1::ControllerError;
	using UhciRootPortStatus = uni::device::SpaceUSB1::RootPortStatus;
	namespace USB = uni::device::SpaceUSB;

	constexpr uint8 UhciPciCommandOffset = 0x04u;
	constexpr uint16 UhciPciCommandIoSpace = 1u << 0;
	constexpr uint16 UhciPciCommandBusMaster = 1u << 2;
	constexpr uint8 UhciControllerCapacity = 8;
	constexpr stduint UhciConfigurationCapacity = 512;
	constexpr uint16 UhciKeyboardReportBytes =
		USB::HIDBootKeyboardReportDecoder::kReportBytes;
	constexpr uint16 UhciHotplugDebouncePolls = 10;
	constexpr uint16 UhciHotplugRetryPolls = 100;
	constexpr stduint UhciPollIntervalTicks = CONFIG_SysTickFreq >= 100 ?
		CONFIG_SysTickFreq / 100 : 1;

	struct UhciEnumeratedDevice {
		USB::DeviceDescriptor descriptor{};
		uint8 configuration[UhciConfigurationCapacity]{};
		uint16 configuration_length = 0;
		uint8 port_index = 0;
		uint8 address = 0;
		uint8 max_packet_size = 0;
		uint8 keyboard_interface = 0;
		uint8 keyboard_endpoint = 0;
		uint8 keyboard_interval = 0;
		uint16 keyboard_max_packet_size = 0;
		uint8 keyboard_report[UhciKeyboardReportBytes]{};
		USB::HIDBootKeyboardReportDecoder keyboard_decoder{};
		bool low_speed = false;
		bool configured = false;
		bool keyboard_active = false;
	};

	struct UhciDriverInstance {
		DeviceNode* node = nullptr;
		uint32* frame_list = nullptr;
		void* transfer_workspace = nullptr;
		UhciHostController host{};
		uint16 io_base = 0;
		uint16 io_length = 0;
		UhciRootPortStatus port_status[UhciHostController::kMaximumRootPorts]{};
		DeviceNode* port_nodes[UhciHostController::kMaximumRootPorts]{};
		uint16 port_wait_polls[UhciHostController::kMaximumRootPorts]{};
		uint16 controller_status = 0;
		uint8 irq_line = 0xFFu;
		uint8 irq_pin = 0;
		UhciEnumeratedDevice device{};
		bool running = false;
	};

	UhciDriverInstance uhci_controllers[UhciControllerCapacity]{};
	volatile bool uhci_poll_armed = false;

	bool EnumerateUhciDevice(UhciDriverInstance& controller,
		uint8 port_index, bool low_speed);
	bool RegisterUhciEnumeratedDevice(UhciDriverInstance& controller,
		DeviceNode* port_node);

	const char* UhciNodeName(const UhciDriverInstance& controller) {
		return controller.node && controller.node->link.addr ?
			controller.node->link.addr : "(unnamed)";
	}

	UhciDriverInstance* AllocateUhciController(DeviceNode* node) {
		for (auto& controller : uhci_controllers) {
			if (controller.node == node) return &controller;
		}
		for (auto& controller : uhci_controllers) {
			if (controller.node) continue;
			controller = {};
			controller.node = node;
			return &controller;
		}
		return nullptr;
	}

	uint16 UhciRead16(void* context, uint16 offset) {
		auto& controller = *static_cast<UhciDriverInstance*>(context);
		return innpw(uint16(controller.io_base + offset));
	}

	void UhciWrite8(void* context, uint16 offset, uint8 value) {
		auto& controller = *static_cast<UhciDriverInstance*>(context);
		outpb(uint16(controller.io_base + offset), value);
	}

	void UhciWrite16(void* context, uint16 offset, uint16 value) {
		auto& controller = *static_cast<UhciDriverInstance*>(context);
		outpw(uint16(controller.io_base + offset), value);
	}

	void UhciWrite32(void* context, uint16 offset, uint32 value) {
		auto& controller = *static_cast<UhciDriverInstance*>(context);
		outpd(uint16(controller.io_base + offset), value);
	}

	void UhciDelayMilliseconds(void*, stduint milliseconds) {
		TSC_Wait_MS(milliseconds);
	}

	void UhciSynchronizeMemory(void*) {
		_ASM volatile ("mfence" ::: "memory");
	}

	uni::device::SpaceUSB1::ControllerIO UhciControllerIO(
		UhciDriverInstance& controller) {
		return {
			&controller,
			UhciRead16,
			UhciWrite8,
			UhciWrite16,
			UhciWrite32,
			UhciDelayMilliseconds,
			UhciSynchronizeMemory,
		};
	}

	void ReleaseUhciFrameList(UhciDriverInstance& controller) {
		if (!controller.frame_list) return;
		DmaLowFree(controller.frame_list, UhciHostController::kFrameListBytes);
		controller.frame_list = nullptr;
	}

	void ReleaseUhciTransferWorkspace(UhciDriverInstance& controller) {
		if (!controller.transfer_workspace) return;
		DmaLowFree(controller.transfer_workspace,
			UhciHostController::kTransferWorkspaceBytes);
		controller.transfer_workspace = nullptr;
	}

	bool AllocateUhciFrameList(UhciDriverInstance& controller) {
		if (controller.frame_list) return false;
		controller.frame_list = static_cast<uint32*>(
			DmaLowAlloc(UhciHostController::kFrameListBytes));
		if (!controller.frame_list) return false;
		const uint64 address = uint64(stduint(controller.frame_list));
		if ((address & 0xFFFu) ||
			address + UhciHostController::kFrameListBytes > 0x100000000ull) {
			plogwarn("[UHCI] %s low-DMA frame list is invalid: %[64H]",
				UhciNodeName(controller), address);
			ReleaseUhciFrameList(controller);
			return false;
		}
		return true;
	}

	bool AllocateUhciTransferWorkspace(UhciDriverInstance& controller) {
		if (controller.transfer_workspace) return true;
		controller.transfer_workspace = DmaLowAlloc(
			UhciHostController::kTransferWorkspaceBytes);
		if (!controller.transfer_workspace) return false;
		const uint64 address = uint64(stduint(controller.transfer_workspace));
		if ((address & 0xFFFu) ||
			address + UhciHostController::kTransferWorkspaceBytes > 0x100000000ull) {
			plogwarn("[UHCI] %s low-DMA transfer workspace is invalid: %[64H]",
				UhciNodeName(controller), address);
			ReleaseUhciTransferWorkspace(controller);
			return false;
		}
		const auto error = controller.host.ConfigureTransferWorkspace(
			controller.transfer_workspace, uint32(address),
			UhciHostController::kTransferWorkspaceBytes);
		if (error != UhciControllerError::Success) {
			plogwarn("[UHCI] %s transfer workspace rejected: %s",
				UhciNodeName(controller), UhciHostController::ErrorName(error));
			ReleaseUhciTransferWorkspace(controller);
			return false;
		}
		return true;
	}

	bool EnableUhciPciCommand(const DeviceNode& node) {
		const uint32 command_status = uni::PCI::read_config_register(
			node.fields.pci_bus, node.fields.pci_device, node.fields.pci_function,
			UhciPciCommandOffset);
		const uint16 command = uint16(command_status) |
			UhciPciCommandIoSpace | UhciPciCommandBusMaster;
		uni::PCI::write_config_register(
			node.fields.pci_bus, node.fields.pci_device, node.fields.pci_function,
			UhciPciCommandOffset, command);
		const uint16 current = uint16(uni::PCI::read_config_register(
			node.fields.pci_bus, node.fields.pci_device, node.fields.pci_function,
			UhciPciCommandOffset));
		return (current & (UhciPciCommandIoSpace | UhciPciCommandBusMaster)) ==
			(UhciPciCommandIoSpace | UhciPciCommandBusMaster);
	}

	void StopUhciController(UhciDriverInstance& controller) {
		controller.running = false;
		if (!controller.host.Stop()) {
			plogwarn("[UHCI] %s did not halt; retaining low-DMA pages",
				UhciNodeName(controller));
			return;
		}
		ReleaseUhciTransferWorkspace(controller);
		ReleaseUhciFrameList(controller);
	}

	void DispatchUhciKeyboardEvent(uint8 modifiers, uint8 keycode,
		bool pressed) {
		keyboard_event_t event{};
		event.mod_val = modifiers;
		event.keycode = keycode;
		event.method = pressed ? keyboard_event_t::method_t::keydown :
			keyboard_event_t::method_t::keyup;
		hand_kboard(event);
	}

	void RemoveUhciDeviceNode(UhciDriverInstance& controller,
		uint8 port_index, uint8 address) {
		if (port_index >= controller.host.RootPortCount() ||
			!controller.port_nodes[port_index] || !address) return;
		auto device_name = String::newFormat("usb-dev@port%u.slot%u",
			(stduint)(port_index + 1), (stduint)address);
		Devsman::RemoveUSBDevice(controller.port_nodes[port_index],
			device_name.reference());
	}

	void DisconnectUhciDevice(UhciDriverInstance& controller) {
		auto& device = controller.device;
		if (!device.configured) return;
		const uint8 port_index = device.port_index;
		const uint8 address = device.address;
		if (device.keyboard_endpoint) {
			device.keyboard_decoder.Reset(DispatchUhciKeyboardEvent);
		}
		if (device.keyboard_active) {
			controller.host.StopInterruptIn();
			device.keyboard_active = false;
		}
		RemoveUhciDeviceNode(controller, port_index, address);
		device = {};
		ploginfo("[UHCI] %s port=%u device address=%u disconnected",
			UhciNodeName(controller), (stduint)(port_index + 1),
			(stduint)address);
	}

	void RetryUhciPortLater(UhciDriverInstance& controller,
		uint8 port_index) {
		controller.port_wait_polls[port_index] = UhciHotplugRetryPolls;
		controller.device = {};
	}

	void RecoverUhciDeviceLater(UhciDriverInstance& controller) {
		auto& device = controller.device;
		const uint8 port_index = device.port_index;
		const uint8 address = device.address;
		if (device.keyboard_endpoint) {
			device.keyboard_decoder.Reset(DispatchUhciKeyboardEvent);
		}
		if (device.keyboard_active) controller.host.StopInterruptIn();
		RemoveUhciDeviceNode(controller, port_index, address);
		RetryUhciPortLater(controller, port_index);
	}

	void TryAttachUhciDevice(UhciDriverInstance& controller,
		uint8 port_index) {
		if (controller.device.configured ||
			port_index >= controller.host.RootPortCount()) return;
		if (!controller.host.ResetRootPort(port_index)) {
			plogwarn("[UHCI] %s port=%u hotplug reset failed",
				UhciNodeName(controller), (stduint)(port_index + 1));
			RetryUhciPortLater(controller, port_index);
			return;
		}
		const auto status = controller.host.RootPortAt(port_index);
		controller.port_status[port_index] = status;
		if (!status.valid || !status.connected || !status.enabled) {
			RetryUhciPortLater(controller, port_index);
			return;
		}
		if (!EnumerateUhciDevice(controller, port_index, status.low_speed)) {
			plogwarn("[UHCI] %s port=%u hotplug enumeration failed; retrying",
				UhciNodeName(controller), (stduint)(port_index + 1));
			RetryUhciPortLater(controller, port_index);
			return;
		}
		if (!RegisterUhciEnumeratedDevice(controller,
			controller.port_nodes[port_index])) {
			plogwarn("[UHCI] %s port=%u hotplug device-node registration failed; retrying",
				UhciNodeName(controller), (stduint)(port_index + 1));
			if (controller.device.keyboard_active) {
				controller.host.StopInterruptIn();
			}
			RemoveUhciDeviceNode(controller, controller.device.port_index,
				controller.device.address);
			RetryUhciPortLater(controller, port_index);
			return;
		}
		controller.port_wait_polls[port_index] = 0;
		ploginfo("[UHCI] %s port=%u hotplug device attached address=%u",
			UhciNodeName(controller), (stduint)(port_index + 1),
			(stduint)controller.device.address);
	}

	void PollUhciKeyboard(UhciDriverInstance& controller) {
		auto& device = controller.device;
		if (!device.keyboard_active) return;
		uint16 actual_length = 0;
		bool completed = false;
		const auto error = controller.host.PollInterruptIn(
			device.keyboard_report, sizeof(device.keyboard_report),
			actual_length, completed);
		if (error != UhciControllerError::Success) {
			plogwarn("[UHCI] %s keyboard interrupt-in stopped: %s status=%[32H]; re-enumerating",
				UhciNodeName(controller), UhciHostController::ErrorName(error),
				controller.host.LastTransferStatus());
			RecoverUhciDeviceLater(controller);
			return;
		}
		if (completed && actual_length == UhciKeyboardReportBytes) {
			device.keyboard_decoder.Process(device.keyboard_report,
				actual_length, DispatchUhciKeyboardEvent);
		}
	}

	void PollUhciControllers(pureptr_t, stduint) {
		uhci_poll_armed = false;
		bool have_running_controller = false;
		for (auto& controller : uhci_controllers) {
			if (!controller.running) continue;
			have_running_controller = true;
			const uint16 controller_status = controller.host.Status();
			if (!controller.host.IsRunning() &&
				controller_status != controller.controller_status) {
				plogwarn("[UHCI] %s stopped unexpectedly sts=%[16H] frame=%u",
					UhciNodeName(controller), controller_status,
					(stduint)controller.host.FrameNumber());
			}
			controller.controller_status = controller_status;
			for (uint8 index = 0; index < controller.host.RootPortCount(); ++index) {
				const auto status = controller.host.RootPortAt(index);
				const auto previous = controller.port_status[index];
				if (status.connect_changed || status.enable_changed ||
					status.connected != previous.connected ||
					status.enabled != previous.enabled ||
					status.low_speed != previous.low_speed) {
					ploginfo("[UHCI] %s port=%u change connected=%u enabled=%u low-speed=%u sts=%[16H]",
						UhciNodeName(controller), (stduint)(index + 1),
						(stduint)status.connected, (stduint)status.enabled,
						(stduint)status.low_speed, status.raw);
					controller.host.AcknowledgeRootPortChanges(index);
				}
				if (status.connected && !previous.connected) {
					controller.port_wait_polls[index] = UhciHotplugDebouncePolls;
				}
				if (controller.device.configured &&
					controller.device.port_index == index) {
					if (!status.connected) {
						DisconnectUhciDevice(controller);
					} else if (!status.enabled) {
						plogwarn("[UHCI] %s port=%u disabled; re-enumerating",
							UhciNodeName(controller), (stduint)(index + 1));
						RecoverUhciDeviceLater(controller);
					}
				}
				if (!status.connected) {
					controller.port_wait_polls[index] = 0;
				}
				controller.port_status[index] = status;
				if (!controller.device.configured && status.valid &&
					status.connected) {
					if (controller.port_wait_polls[index]) {
						--controller.port_wait_polls[index];
					}
					if (!controller.port_wait_polls[index]) {
						TryAttachUhciDevice(controller, index);
					}
				}
			}
			PollUhciKeyboard(controller);
		}
		if (have_running_controller) {
			uhci_poll_armed = true;
			SysTimer::Append(UhciPollIntervalTicks, 0, (_tocall_ft)PollUhciControllers);
		}
	}

	void ArmUhciPolling() {
		if (uhci_poll_armed) return;
		uhci_poll_armed = true;
		SysTimer::Append(UhciPollIntervalTicks, 0, (_tocall_ft)PollUhciControllers);
	}

	bool ReadUhciDeviceDescriptor(UhciDriverInstance& controller,
		uint8 port_index, bool low_speed, UhciEnumeratedDevice& device) {
		if (!AllocateUhciTransferWorkspace(controller)) {
			plogwarn("[UHCI] %s cannot reserve one 4K low-DMA transfer page",
				UhciNodeName(controller));
			return false;
		}
		USB::SetupData setup{};
		setup.request_type.data = 0x80u;
		setup.request = USB::request::kGetDescriptor;
		setup.value = uint16(USB::DeviceDescriptor::kType << 8);
		setup.index = 0;
		setup.length = 8;
		uint8 descriptor_prefix[8]{};
		uint16 actual_length = 0;
		auto error = controller.host.ControlIn(0, 0, low_speed, 8,
			setup, descriptor_prefix, sizeof(descriptor_prefix),
			actual_length, 500);
		if (error != UhciControllerError::Success) {
			plogwarn("[UHCI] %s port=%u GET_DESCRIPTOR(8) failed: %s td=%u status=%[32H]",
				UhciNodeName(controller), (stduint)(port_index + 1),
				UhciHostController::ErrorName(error),
				(stduint)controller.host.LastTransferDescriptorIndex(),
				controller.host.LastTransferStatus());
			return false;
		}
		const uint8 max_packet_size = descriptor_prefix[7];
		if (actual_length != sizeof(descriptor_prefix) ||
			descriptor_prefix[0] != sizeof(USB::DeviceDescriptor) ||
			descriptor_prefix[1] != USB::DeviceDescriptor::kType ||
			(max_packet_size != 8 && max_packet_size != 16 &&
			 max_packet_size != 32 && max_packet_size != 64) ||
			(low_speed && max_packet_size != 8)) {
			plogwarn("[UHCI] %s port=%u invalid device descriptor prefix len=%u actual=%u type=%u mps0=%u",
				UhciNodeName(controller), (stduint)(port_index + 1),
				(stduint)descriptor_prefix[0], (stduint)actual_length,
				(stduint)descriptor_prefix[1], (stduint)max_packet_size);
			return false;
		}
		setup.length = sizeof(USB::DeviceDescriptor);
		USB::DeviceDescriptor descriptor{};
		error = controller.host.ControlIn(0, 0, low_speed, max_packet_size,
			setup, reinterpret_cast<uint8*>(&descriptor), sizeof(descriptor),
			actual_length, 500);
		if (error != UhciControllerError::Success) {
			plogwarn("[UHCI] %s port=%u GET_DESCRIPTOR(18) failed: %s td=%u status=%[32H]",
				UhciNodeName(controller), (stduint)(port_index + 1),
				UhciHostController::ErrorName(error),
				(stduint)controller.host.LastTransferDescriptorIndex(),
				controller.host.LastTransferStatus());
			return false;
		}
		if (actual_length != sizeof(descriptor) ||
			descriptor.length != sizeof(descriptor) ||
			descriptor.descriptor_type != USB::DeviceDescriptor::kType ||
			descriptor.max_packet_size != max_packet_size) {
			plogwarn("[UHCI] %s port=%u invalid device descriptor len=%u actual=%u type=%u mps0=%u",
				UhciNodeName(controller), (stduint)(port_index + 1),
				(stduint)descriptor.length, (stduint)actual_length,
				(stduint)descriptor.descriptor_type,
				(stduint)descriptor.max_packet_size);
			return false;
		}
		ploginfo("[UHCI] %s port=%u device descriptor usb=%[16H] vid=%[16H] pid=%[16H] class=%u/%u/%u mps0=%u configs=%u",
			UhciNodeName(controller), (stduint)(port_index + 1),
			descriptor.usb_release, descriptor.vendor_id, descriptor.product_id,
			(stduint)descriptor.device_class,
			(stduint)descriptor.device_sub_class,
			(stduint)descriptor.device_protocol,
			(stduint)descriptor.max_packet_size,
			(stduint)descriptor.num_configurations);
		device.descriptor = descriptor;
		device.port_index = port_index;
		device.max_packet_size = max_packet_size;
		device.low_speed = low_speed;
		return true;
	}

	bool SetUhciDeviceAddress(UhciDriverInstance& controller,
		UhciEnumeratedDevice& device) {
		USB::SetupData setup{};
		setup.request_type.data = 0x00u;
		setup.request = USB::request::kSetAddress;
		setup.value = USB::kDefaultDeviceAddress;
		setup.index = 0;
		setup.length = 0;
		const auto error = controller.host.ControlNoData(0, 0,
			device.low_speed, device.max_packet_size, setup, 500);
		if (error != UhciControllerError::Success) {
			plogwarn("[UHCI] %s port=%u SET_ADDRESS(%u) failed: %s td=%u status=%[32H]",
				UhciNodeName(controller), (stduint)(device.port_index + 1),
				(stduint)USB::kDefaultDeviceAddress,
				UhciHostController::ErrorName(error),
				(stduint)controller.host.LastTransferDescriptorIndex(),
				controller.host.LastTransferStatus());
			return false;
		}
		device.address = USB::kDefaultDeviceAddress;
		return true;
	}

	bool ValidateUhciConfiguration(const uint8* data, uint16 length) {
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
				descriptor_length < sizeof(USB::InterfaceDescriptor)) {
				return false;
			}
			if (descriptor_type == USB::EndpointDescriptor::kType &&
				descriptor_length < sizeof(USB::EndpointDescriptor)) {
				return false;
			}
			offset += descriptor_length;
		}
		return offset == length;
	}

	bool ReadUhciConfiguration(UhciDriverInstance& controller,
		UhciEnumeratedDevice& device) {
		USB::SetupData setup{};
		setup.request_type.data = 0x80u;
		setup.request = USB::request::kGetDescriptor;
		setup.value = uint16(USB::ConfigurationDescriptor::kType << 8);
		setup.index = 0;
		setup.length = sizeof(USB::ConfigurationDescriptor);
		USB::ConfigurationDescriptor header{};
		uint16 actual_length = 0;
		auto error = controller.host.ControlIn(device.address, 0,
			device.low_speed, device.max_packet_size, setup,
			reinterpret_cast<uint8*>(&header), sizeof(header), actual_length, 500);
		if (error != UhciControllerError::Success) {
			plogwarn("[UHCI] %s port=%u GET_CONFIGURATION_HEADER failed: %s td=%u status=%[32H]",
				UhciNodeName(controller), (stduint)(device.port_index + 1),
				UhciHostController::ErrorName(error),
				(stduint)controller.host.LastTransferDescriptorIndex(),
				controller.host.LastTransferStatus());
			return false;
		}
		if (actual_length != sizeof(header) ||
			header.length != sizeof(header) ||
			header.descriptor_type != USB::ConfigurationDescriptor::kType ||
			header.total_length < sizeof(header) ||
			header.total_length > UhciConfigurationCapacity ||
			!header.configuration_value) {
			plogwarn("[UHCI] %s port=%u invalid configuration header len=%u actual=%u type=%u total=%u value=%u",
				UhciNodeName(controller), (stduint)(device.port_index + 1),
				(stduint)header.length, (stduint)actual_length,
				(stduint)header.descriptor_type, (stduint)header.total_length,
				(stduint)header.configuration_value);
			return false;
		}

		setup.length = header.total_length;
		error = controller.host.ControlIn(device.address, 0,
			device.low_speed, device.max_packet_size, setup,
			device.configuration, header.total_length, actual_length, 500);
		if (error != UhciControllerError::Success) {
			plogwarn("[UHCI] %s port=%u GET_CONFIGURATION(%u) failed: %s td=%u status=%[32H]",
				UhciNodeName(controller), (stduint)(device.port_index + 1),
				(stduint)header.total_length,
				UhciHostController::ErrorName(error),
				(stduint)controller.host.LastTransferDescriptorIndex(),
				controller.host.LastTransferStatus());
			return false;
		}
		if (actual_length != header.total_length ||
			!ValidateUhciConfiguration(device.configuration, actual_length)) {
			plogwarn("[UHCI] %s port=%u invalid full configuration actual=%u expected=%u",
				UhciNodeName(controller), (stduint)(device.port_index + 1),
				(stduint)actual_length, (stduint)header.total_length);
			return false;
		}
		const auto* configuration =
			reinterpret_cast<const USB::ConfigurationDescriptor*>(
				device.configuration);
		if (configuration->total_length != header.total_length ||
			configuration->configuration_value != header.configuration_value) {
			plogwarn("[UHCI] %s port=%u configuration header changed during read",
				UhciNodeName(controller), (stduint)(device.port_index + 1));
			return false;
		}
		device.configuration_length = actual_length;
		return true;
	}

	bool SetUhciConfiguration(UhciDriverInstance& controller,
		UhciEnumeratedDevice& device) {
		const auto* configuration =
			reinterpret_cast<const USB::ConfigurationDescriptor*>(
				device.configuration);
		USB::SetupData setup{};
		setup.request_type.data = 0x00u;
		setup.request = USB::request::kSetConfiguration;
		setup.value = configuration->configuration_value;
		setup.index = 0;
		setup.length = 0;
		const auto error = controller.host.ControlNoData(device.address, 0,
			device.low_speed, device.max_packet_size, setup, 500);
		if (error != UhciControllerError::Success) {
			plogwarn("[UHCI] %s port=%u SET_CONFIGURATION(%u) failed: %s td=%u status=%[32H]",
				UhciNodeName(controller), (stduint)(device.port_index + 1),
				(stduint)configuration->configuration_value,
				UhciHostController::ErrorName(error),
				(stduint)controller.host.LastTransferDescriptorIndex(),
				controller.host.LastTransferStatus());
			return false;
		}
		device.configured = true;
		ploginfo("[UHCI] %s port=%u configured address=%u config=%u total=%u interfaces=%u",
			UhciNodeName(controller), (stduint)(device.port_index + 1),
			(stduint)device.address,
			(stduint)configuration->configuration_value,
			(stduint)configuration->total_length,
			(stduint)configuration->num_interfaces);
		return true;
	}

	bool ConfigureUhciKeyboard(UhciDriverInstance& controller,
		UhciEnumeratedDevice& device) {
		const uint8* position = device.configuration;
		const uint8* const end = position + device.configuration_length;
		position += position[0];
		bool keyboard_interface = false;
		bool keyboard_interface_found = false;
		while (position < end) {
			const uint8 descriptor_length = position[0];
			const uint8 descriptor_type = position[1];
			if (descriptor_type == USB::InterfaceDescriptor::kType) {
				const auto* descriptor =
					reinterpret_cast<const USB::InterfaceDescriptor*>(position);
				keyboard_interface = !descriptor->alternate_setting &&
					descriptor->interface_class == 3u &&
					descriptor->interface_sub_class == 1u &&
					descriptor->interface_protocol == 1u;
				if (keyboard_interface) {
					keyboard_interface_found = true;
					device.keyboard_interface = descriptor->interface_number;
				}
			} else if (keyboard_interface &&
				descriptor_type == USB::EndpointDescriptor::kType) {
				const auto* descriptor =
					reinterpret_cast<const USB::EndpointDescriptor*>(position);
				const uint16 max_packet_size =
					descriptor->max_packet_size & 0x07FFu;
				if (descriptor->attributes.bits.transfer_type ==
					uint8(USB::EndpointType::kInterrupt) &&
					descriptor->endpoint_address.bits.dir_in &&
					descriptor->endpoint_address.bits.number &&
					max_packet_size >= UhciKeyboardReportBytes &&
					max_packet_size <= 64 && descriptor->interval) {
					device.keyboard_endpoint =
						descriptor->endpoint_address.bits.number;
					device.keyboard_interval = descriptor->interval;
					device.keyboard_max_packet_size = max_packet_size;
					break;
				}
			}
			position += descriptor_length;
		}
		if (!device.keyboard_endpoint) {
			if (!keyboard_interface_found) return true;
			plogwarn("[UHCI] %s port=%u boot keyboard has no usable interrupt-in endpoint",
				UhciNodeName(controller), (stduint)(device.port_index + 1));
			return false;
		}

		USB::SetupData setup{};
		setup.request_type.bits.direction = USB::request_type::kOut;
		setup.request_type.bits.type = USB::request_type::kClass;
		setup.request_type.bits.recipient = USB::request_type::kInterface;
		setup.request = USB::request::kSetProtocol;
		setup.value = 0;
		setup.index = device.keyboard_interface;
		setup.length = 0;
		auto error = controller.host.ControlNoData(device.address, 0,
			device.low_speed, device.max_packet_size, setup, 500);
		if (error != UhciControllerError::Success) {
			plogwarn("[UHCI] %s port=%u keyboard SET_PROTOCOL(boot) failed: %s td=%u status=%[32H]",
				UhciNodeName(controller), (stduint)(device.port_index + 1),
				UhciHostController::ErrorName(error),
				(stduint)controller.host.LastTransferDescriptorIndex(),
				controller.host.LastTransferStatus());
			return false;
		}
		error = controller.host.StartInterruptIn(device.address,
			device.keyboard_endpoint, device.low_speed,
			device.keyboard_max_packet_size, device.keyboard_interval,
			UhciKeyboardReportBytes);
		if (error != UhciControllerError::Success) {
			plogwarn("[UHCI] %s port=%u cannot arm keyboard interrupt-in: %s",
				UhciNodeName(controller), (stduint)(device.port_index + 1),
				UhciHostController::ErrorName(error));
			return false;
		}
		device.keyboard_active = true;
		ploginfo("[UHCI] %s port=%u keyboard interrupt-in armed ep=%u mps=%u interval=%u",
			UhciNodeName(controller), (stduint)(device.port_index + 1),
			(stduint)device.keyboard_endpoint,
			(stduint)device.keyboard_max_packet_size,
			(stduint)device.keyboard_interval);
		return true;
	}

	bool EnumerateUhciDevice(UhciDriverInstance& controller,
		uint8 port_index, bool low_speed) {
		controller.device = {};
		if (!ReadUhciDeviceDescriptor(controller, port_index, low_speed,
			controller.device)) return false;
		if (!controller.device.descriptor.num_configurations) return false;
		if (!SetUhciDeviceAddress(controller, controller.device)) return false;
		if (!ReadUhciConfiguration(controller, controller.device)) return false;
		if (!SetUhciConfiguration(controller, controller.device)) return false;
		return ConfigureUhciKeyboard(controller, controller.device);
	}

	const char* UhciDeviceDriverName(const USB::DeviceDescriptor& descriptor) {
		return descriptor.device_class == 0x09u ? "usb-hub" : "usb-device";
	}

	const char* UhciInterfaceDriverName(
		const USB::InterfaceDescriptor& descriptor) {
		if (descriptor.interface_class == 3u &&
			descriptor.interface_sub_class == 1u) {
			if (descriptor.interface_protocol == 1u) return "usb-hid-keyboard";
			if (descriptor.interface_protocol == 2u) return "usb-hid-mouse";
		}
		return "usb-interface";
	}

	bool RegisterUhciEnumeratedDevice(UhciDriverInstance& controller,
		DeviceNode* port_node) {
		auto& device = controller.device;
		if (!device.configured) return true;
		if (!port_node) return false;
		auto device_name = String::newFormat("usb-dev@port%u.slot%u",
			(stduint)(device.port_index + 1), (stduint)device.address);
		auto* device_node = Devsman::RegisterUSBDevice(port_node,
			device_name.reference(), device.descriptor.vendor_id,
			device.descriptor.product_id, nullptr, nullptr, nullptr,
			device.descriptor.device_class,
			device.descriptor.device_sub_class,
			device.descriptor.device_protocol,
			uint8(device.port_index + 1), device.address,
			UhciDeviceDriverName(device.descriptor), &device);
		if (!device_node) return false;

		const uint8* position = device.configuration;
		const uint8* const end = position + device.configuration_length;
		position += position[0];
		DeviceNode* interface_node = nullptr;
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
						UhciInterfaceDriverName(*descriptor), &device);
					if (!interface_node) return false;
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
		return true;
	}

	bool RegisterUhciDeviceNodes(UhciDriverInstance& controller) {
		auto usb_bus_name = String::newFormat("usb-bus@%04x:%02x:%02x.%x", 0,
			(stduint)controller.node->fields.pci_bus,
			(stduint)controller.node->fields.pci_device,
			(stduint)controller.node->fields.pci_function);
		auto* usb_bus_node = Devsman::RegisterUSBBus(usb_bus_name.reference(),
			"uhci", &controller.host);
		if (!usb_bus_node) return false;
		auto* root_hub = Devsman::RegisterUSBRootHub(usb_bus_node,
			"usb-root-hub@0", 0x09u, 0x00u, 0x00u,
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
				!RegisterUhciEnumeratedDevice(controller, port_node)) return false;
		}
		return true;
	}

	bool StartUhciDriver(DeviceNode* node) {
		if (!node) return false;
		const auto* io = Devsman::FindResource(node, DeviceResourceType::PciBarIo, 4);
		if (!io || !io->start || io->start > 0xFFFFu ||
			!io->length || io->length > 0xFFFFu ||
			io->start + io->length > 0x10000u) {
			plogwarn("[UHCI] %s has invalid BAR4 I/O resource",
				node->link.addr ? node->link.addr : "(unnamed)");
			return false;
		}
		auto* controller = AllocateUhciController(node);
		if (!controller) {
			plogwarn("[UHCI] controller capacity exhausted");
			return false;
		}
		controller->io_base = uint16(io->start);
		controller->io_length = uint16(io->length);
		if (const auto* irq = Devsman::FindResource(node, DeviceResourceType::IrqLine, 0)) {
			controller->irq_line = uint8(irq->start);
			controller->irq_pin = uint8(irq->extra);
		}
		if (!EnableUhciPciCommand(*node)) {
			plogwarn("[UHCI] %s failed to enable PCI I/O and bus mastering",
				UhciNodeName(*controller));
			return false;
		}
		if (!AllocateUhciFrameList(*controller)) {
			plogwarn("[UHCI] %s cannot reserve one 4K low-DMA frame-list page",
				UhciNodeName(*controller));
			return false;
		}
		controller->host.Bind(UhciControllerIO(*controller), controller->io_length);
		auto error = controller->host.Initialize(controller->frame_list,
			uint32(stduint(controller->frame_list)));
		if (error != UhciControllerError::Success) {
			plogwarn("[UHCI] %s initialize failed: %s cmd=%[16H] sts=%[16H]",
				UhciNodeName(*controller), UhciHostController::ErrorName(error),
				controller->host.Command(), controller->host.Status());
			StopUhciController(*controller);
			return false;
		}
		error = controller->host.Run();
		if (error != UhciControllerError::Success) {
			plogwarn("[UHCI] %s schedule start failed: %s cmd=%[16H] sts=%[16H] frame=%u",
				UhciNodeName(*controller), UhciHostController::ErrorName(error),
				controller->host.Command(), controller->host.Status(),
				(stduint)controller->host.FrameNumber());
			StopUhciController(*controller);
			return false;
		}
		ploginfo("[UHCI] %s frame number advanced, current=%u",
			UhciNodeName(*controller), (stduint)controller->host.FrameNumber());
		bool device_enumerated = false;
		for (uint8 index = 0; index < controller->host.RootPortCount(); ++index) {
			const auto before = controller->host.RootPortAt(index);
			const bool reset_ok = !before.connected ||
				controller->host.ResetRootPort(index);
			const auto status = controller->host.RootPortAt(index);
			controller->port_status[index] = status;
			ploginfo("[UHCI] %s port=%u connected=%u enabled=%u low-speed=%u reset=%s sts=%[16H]",
				UhciNodeName(*controller), (stduint)(index + 1),
				(stduint)status.connected, (stduint)status.enabled,
				(stduint)status.low_speed, reset_ok ? "ok" : "failed", status.raw);
			if (before.connected && !reset_ok) {
				StopUhciController(*controller);
				return false;
			}
			if (status.connected && !device_enumerated) {
				if (!EnumerateUhciDevice(*controller, index, status.low_speed)) {
					StopUhciController(*controller);
					return false;
				}
				device_enumerated = true;
			} else if (status.connected) {
				plogwarn("[UHCI] %s port=%u additional device deferred",
					UhciNodeName(*controller), (stduint)(index + 1));
			}
		}
		controller->running = true;
		controller->controller_status = controller->host.Status();
		node->fields.binding.driver_data = &controller->host;
		if (!RegisterUhciDeviceNodes(*controller)) {
			StopUhciController(*controller);
			node->fields.binding.driver_data = nullptr;
			return false;
		}
		ArmUhciPolling();
		ploginfo("[UHCI] %s started IO=%[16H] frame-list=%[64H] ports=%u IRQ line=%u pin=%u frame=%u",
			UhciNodeName(*controller), controller->io_base,
			uint64(stduint(controller->frame_list)),
			(stduint)controller->host.RootPortCount(),
			(stduint)controller->irq_line, (stduint)controller->irq_pin,
			(stduint)controller->host.FrameNumber());
		return true;
	}
}

_ESYM_C void R_UHCI_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_UHCI{
	.init = R_UHCI_INIT,
	.name = "UHCI",
};

void R_UHCI_INIT() {
	if (!Devsman::RegisterDriverStarter("uhci", StartUhciDriver)) {
		plogwarn("[UHCI] failed to register driver starter");
		return;
	}
	Devsman::StartKnownDrivers();
}
#endif
