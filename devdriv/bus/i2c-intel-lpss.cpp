// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: Intel LPSS DesignWare I2C Bring-up
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"

#if (_MCCA & 0xFF00) == 0x8600
#include <cpp/Device/Bus/PCI.hpp>
#include <cpp/Device/Bus/Serial/IIC-HID.hpp>
#include <cpp/Device/IIC>

namespace {
	constexpr stduint DesignWareI2cTransferBudget = 100000u;
	constexpr uint16 AcpiI2cTypeFlagTenBitAddress = 1u << 0;
	constexpr uint32 IntelLpssI2cResourceRequirements = DeviceControllerRequirement_Access | DeviceControllerRequirement_Interrupt | DeviceControllerRequirement_Clock | DeviceControllerRequirement_Reset | DeviceControllerRequirement_Power | DeviceControllerRequirement_Namespace | DeviceControllerRequirement_Identity | DeviceControllerRequirement_Dma;

	enum class IntelLpssI2cFailure : uint8 {
		None,
		MissingMmio,
		AddressRange,
		CapabilitiesUnavailable,
		WrongControllerType,
		Allocation,
		ComponentSignature,
		ClockUnavailable,
		Initialization,
		ResourceContract,
		CompatibilityUnavailable,
	};

	struct IntelLpssI2cInstance {
		IntelLpssI2cInstance(stduint base, DeviceNode* node) : controller(base), pci_node(node) {}

		uni::IIC_DESIGNWARE controller;
		uni::IICDesignWareCapabilities capabilities{};
		DeviceNode* pci_node;
		uint32 lpss_capabilities = 0;
		uint32 lpss_reset = 0;
		uint32 input_clock_hz = 0;
		uint32 bus_speed_hz = 0;
		stduint bound_bus_count = 0;
		bool initialized = false;
		IntelLpssI2cInstance* next = nullptr;
	};

	uni::PCI lpss_pci;
	IntelLpssI2cInstance* lpss_i2c_instances = nullptr;
	IntelLpssI2cInstance* first_lpss_i2c_instance = nullptr;
	IntelLpssI2cInstance* first_initialized_lpss_i2c_instance = nullptr;
	DeviceNode* first_lpss_i2c_failure_node = nullptr;
	IntelLpssI2cFailure first_lpss_i2c_failure = IntelLpssI2cFailure::None;
	uint32 first_lpss_i2c_failure_detail = 0;
	uint32 first_lpss_i2c_failure_capabilities = 0;
	stduint lpss_i2c_attempted = 0;
	stduint lpss_i2c_started = 0;
	stduint lpss_i2c_failed = 0;
	stduint lpss_i2c_initialized = 0;
	stduint lpss_i2c_bound_buses = 0;

	#if CONFIG_ACPI_BOOT_TEST
	struct DesignWareI2cTestModel {
		uint32 registers[0x300u / sizeof(uint32)]{};
		byte memory[256]{};
		byte receive_fifo[64]{};
		uint16 receive_head = 0;
		uint16 receive_tail = 0;
		uint16 receive_count = 0;
		byte pending_write[66]{};
		uint16 register_pointer = 0;
		uint16 pending_write_length = 0;
		bool hold_enable_status = false;
		bool hold_transfer_progress = false;
		bool hold_bus_busy = false;
		bool recover_bus_succeeds = true;
		bool inject_stale_receive = false;
		bool saw_restart = false;
		bool saw_stop = false;
		bool accepted_ten_bit = false;
		uint16 accepted_address = 0x2Cu;
		uint32 injected_abort_source = 0;
		stduint restart_count = 0;
		stduint stop_count = 0;
		stduint register_access_count = 0;
		stduint data_register_access_count = 0;
		stduint recovery_count = 0;
	};

	struct DesignWareI2cTestFixture {
		DesignWareI2cTestFixture();

		DesignWareI2cTestModel model;
		uni::IIC_DESIGNWARE controller;
		byte command[3]{};
		byte received[65]{};
		uni::IICMessage messages[3]{};
		uni::IICTransferResult transfer_result{};
	};

	struct HidI2cProtocolTestModel {
		byte descriptor[uni::HIDI2CDescriptorLength]{};
		byte report_descriptor[52]{};
		byte input_packet[64]{};
		byte feature_packet[64]{};
		byte output_packet[64]{};
		byte set_report_packet[64]{};
		stduint output_length = 0;
		stduint set_report_length = 0;
		bool power_on = false;
		bool reset_requested = false;
		bool return_reset_completion = false;
		bool inject_address_nack = false;
		bool inject_short_transfer = false;
	};

	struct HidI2cProtocolTestFixture {
		HidI2cProtocolTestModel model;
		byte report[64]{};
		byte payload[4]{};
		uni::HIDI2CResult result{};
	};

	DesignWareI2cTestFixture* designware_i2c_test_fixture = nullptr;
	#endif

	uint32 ReadLpssRegister(stduint base, stduint offset) {
		return *reinterpret_cast<volatile uint32*>(base + offset);
	}

	void WriteLpssRegister(stduint base, stduint offset, uint32 value) {
		*reinterpret_cast<volatile uint32*>(base + offset) = value;
	}

	#if CONFIG_ACPI_BOOT_TEST
	static const byte HidI2cTestReportDescriptor[52] = {
		0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x09,
		0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x95, 0x03, 0x75, 0x01,
		0x81, 0x02, 0x95, 0x01, 0x75, 0x05, 0x81, 0x01, 0x05, 0x01, 0x09, 0x30,
		0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x06,
		0xC0, 0xC0, 0x05, 0x01,
	};

	void FinishDesignWareI2cTestWrite(DesignWareI2cTestModel& model, bool register_only) {
		if (!model.pending_write_length) return;
		const stduint address_length = register_only && model.pending_write_length == 2 ? 2u : 1u;
		model.register_pointer = model.pending_write[0];
		if (address_length == 2) model.register_pointer |= uint16(model.pending_write[1]) << 8;
		for (stduint index = address_length; index < model.pending_write_length; ++index) model.memory[byte(model.register_pointer++)] = model.pending_write[index];
		model.pending_write_length = 0;
	}

	uint32 ReadDesignWareI2cTestRegister(void* context, stduint offset) {
		auto* model = static_cast<DesignWareI2cTestModel*>(context);
		if (!model || offset >= sizeof(model->registers) || offset % sizeof(uint32)) return 0xFFFFFFFFu;
		++model->register_access_count;
		if (offset == _IMM(uni::IICDesignWareReg::DataCommand)) ++model->data_register_access_count;
		if (offset == _IMM(uni::IICDesignWareReg::Status)) {
			if (model->hold_bus_busy) return (1u << 0) | (1u << 1) | (1u << 2);
			if (model->hold_transfer_progress) return 0;
			return (1u << 1) | (1u << 2) | (model->receive_count ? 1u << 3 : 0);
		}
		if (offset == _IMM(uni::IICDesignWareReg::DataCommand)) {
			if (!model->receive_count) return 0;
			const byte value = model->receive_fifo[model->receive_head];
			model->receive_head = uint16((model->receive_head + 1u) % numsof(model->receive_fifo));
			--model->receive_count;
			return value;
		}
		if (offset == _IMM(uni::IICDesignWareReg::ClearInterrupt)) {
			const uint32 value = model->registers[_IMM(uni::IICDesignWareReg::RawInterruptStatus) / sizeof(uint32)];
			model->registers[_IMM(uni::IICDesignWareReg::RawInterruptStatus) / sizeof(uint32)] = 0;
			model->registers[_IMM(uni::IICDesignWareReg::TransmitAbortSource) / sizeof(uint32)] = 0;
			return value;
		}
		if (offset == _IMM(uni::IICDesignWareReg::ClearTransmitAbort)) {
			const uint32 value = model->registers[_IMM(uni::IICDesignWareReg::TransmitAbortSource) / sizeof(uint32)];
			model->registers[_IMM(uni::IICDesignWareReg::RawInterruptStatus) / sizeof(uint32)] &= ~(1u << 6);
			model->registers[_IMM(uni::IICDesignWareReg::TransmitAbortSource) / sizeof(uint32)] = 0;
			return value;
		}
		if (offset == _IMM(uni::IICDesignWareReg::ClearStopDetection)) {
			const uint32 value = model->registers[_IMM(uni::IICDesignWareReg::RawInterruptStatus) / sizeof(uint32)] & (1u << 9);
			model->registers[_IMM(uni::IICDesignWareReg::RawInterruptStatus) / sizeof(uint32)] &= ~(1u << 9);
			return value;
		}
		return model->registers[offset / sizeof(uint32)];
	}

	void WriteDesignWareI2cTestRegister(void* context, stduint offset, uint32 value) {
		auto* model = static_cast<DesignWareI2cTestModel*>(context);
		if (!model || offset >= sizeof(model->registers) || offset % sizeof(uint32)) return;
		++model->register_access_count;
		if (offset == _IMM(uni::IICDesignWareReg::DataCommand)) ++model->data_register_access_count;
		model->registers[offset / sizeof(uint32)] = value;
		if (offset == _IMM(uni::IICDesignWareReg::Enable)) {
			if (!model->hold_enable_status) model->registers[_IMM(uni::IICDesignWareReg::EnableStatus) / sizeof(uint32)] = value & 1u;
			if (value & 1u) {
				model->pending_write_length = 0;
				model->receive_head = 0;
				model->receive_tail = 0;
				model->receive_count = 0;
			}
			return;
		}
		if (offset != _IMM(uni::IICDesignWareReg::DataCommand) || model->hold_transfer_progress) return;
		const uint32 target = model->registers[_IMM(uni::IICDesignWareReg::TargetAddress) / sizeof(uint32)];
		if ((target & 0x3FFu) != model->accepted_address || bool(target & (1u << 12)) != model->accepted_ten_bit) {
			model->registers[_IMM(uni::IICDesignWareReg::TransmitAbortSource) / sizeof(uint32)] = 1u;
			model->registers[_IMM(uni::IICDesignWareReg::RawInterruptStatus) / sizeof(uint32)] |= 1u << 6;
			return;
		}
		if (model->injected_abort_source) {
			model->registers[_IMM(uni::IICDesignWareReg::TransmitAbortSource) / sizeof(uint32)] = model->injected_abort_source;
			model->registers[_IMM(uni::IICDesignWareReg::RawInterruptStatus) / sizeof(uint32)] |= 1u << 6;
			model->injected_abort_source = 0;
			if (model->inject_stale_receive && model->receive_count < numsof(model->receive_fifo)) {
				model->receive_fifo[model->receive_tail] = 0xEEu;
				model->receive_tail = uint16((model->receive_tail + 1u) % numsof(model->receive_fifo));
				++model->receive_count;
				model->inject_stale_receive = false;
			}
			return;
		}
		if (value & (1u << 10)) {
			model->saw_restart = true;
			++model->restart_count;
		}
		if (value & (1u << 8)) {
			if (value & (1u << 10)) FinishDesignWareI2cTestWrite(*model, true);
			if (model->receive_count < numsof(model->receive_fifo)) {
				model->receive_fifo[model->receive_tail] = model->memory[byte(model->register_pointer++)];
				model->receive_tail = uint16((model->receive_tail + 1u) % numsof(model->receive_fifo));
				++model->receive_count;
			}
		}
		else if (model->pending_write_length < numsof(model->pending_write)) model->pending_write[model->pending_write_length++] = byte(value);
		if (value & (1u << 9)) {
			if (!(value & (1u << 8))) FinishDesignWareI2cTestWrite(*model, false);
			model->saw_stop = true;
			++model->stop_count;
			model->registers[_IMM(uni::IICDesignWareReg::RawInterruptStatus) / sizeof(uint32)] |= 1u << 9;
		}
	}

	void LoadDesignWareI2cHidDescriptor(DesignWareI2cTestModel& model) {
		static const byte descriptor[uni::HIDI2CDescriptorLength] = {
			0x1E, 0x00, 0x00, 0x01, 0x34, 0x00, 0x40, 0x00,
			0x50, 0x00, 0x40, 0x00, 0x60, 0x00, 0x40, 0x00,
			0x70, 0x00, 0x72, 0x00, 0x34, 0x12, 0x78, 0x56,
			0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
		};
		for0(index, numsof(descriptor)) model.memory[0x20u + index] = descriptor[index];
		for0(index, numsof(HidI2cTestReportDescriptor)) model.memory[0x40u + index] = HidI2cTestReportDescriptor[index];
	}

	void LoadHidI2cProtocolTestData(HidI2cProtocolTestModel& model) {
		static const byte descriptor[uni::HIDI2CDescriptorLength] = {
			0x1E, 0x00, 0x00, 0x01, 0x34, 0x00, 0x40, 0x00,
			0x50, 0x00, 0x40, 0x00, 0x60, 0x00, 0x40, 0x00,
			0x70, 0x00, 0x72, 0x00, 0x34, 0x12, 0x78, 0x56,
			0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
		};
		for0(index, numsof(descriptor)) model.descriptor[index] = descriptor[index];
		for0(index, numsof(model.report_descriptor)) model.report_descriptor[index] = HidI2cTestReportDescriptor[index];
		model.input_packet[0] = 5;
		model.input_packet[1] = 0;
		model.input_packet[2] = 1;
		model.input_packet[3] = 0xAAu;
		model.input_packet[4] = 0x55u;
		model.feature_packet[0] = 5;
		model.feature_packet[1] = 0;
		model.feature_packet[2] = 0x23u;
		model.feature_packet[3] = 0x33u;
		model.feature_packet[4] = 0x44u;
	}

	void CopyHidI2cProtocolTestData(byte* destination, stduint capacity, const byte* source, stduint length) {
		for0(index, capacity) destination[index] = index < length ? source[index] : 0;
	}

	uni::IICTransferStatus TransferHidI2cProtocolTest(void* context, uni::IICMessage* messages, stduint count, stduint, uni::IICTransferResult& result) {
		auto* model = static_cast<HidI2cProtocolTestModel*>(context);
		result = {};
		result.failed_message = count;
		if (!model || !messages || !count) return uni::IICTransferStatus::InvalidArgument;
		if (model->inject_address_nack) {
			model->inject_address_nack = false;
			result.failed_message = 0;
			result.hardware_status = 1;
			return uni::IICTransferStatus::AddressNack;
		}
		for0(index, count) {
			if (messages[index].address != 0x2Cu) {
				result.failed_message = index;
				return uni::IICTransferStatus::AddressNack;
			}
			result.submitted_bytes += messages[index].length;
		}
		if (count == 2) {
			const auto& write = messages[0];
			auto& read = messages[1];
			if (!write.write_buffer || write.length < 2 || !read.read_buffer || !(read.flags & uni::IICMessageFlag_Read)) return uni::IICTransferStatus::InvalidArgument;
			const uint16 register_address = uni::DecodeHIDI2CWord(write.write_buffer);
			if (register_address == 0x20u) CopyHidI2cProtocolTestData(read.read_buffer, read.length, model->descriptor, numsof(model->descriptor));
			else if (register_address == 0x40u) CopyHidI2cProtocolTestData(read.read_buffer, read.length, model->report_descriptor, numsof(model->report_descriptor));
			else if (register_address == 0x70u && write.length == 7u && write.write_buffer[2] == 0x3Fu && write.write_buffer[3] == 2u && write.write_buffer[4] == 0x23u && uni::DecodeHIDI2CWord(write.write_buffer + 5) == 0x72u) CopyHidI2cProtocolTestData(read.read_buffer, read.length, model->feature_packet, numsof(model->feature_packet));
			else return uni::IICTransferStatus::InvalidArgument;
			messages[0].transferred = messages[0].length;
			messages[1].transferred = model->inject_short_transfer && messages[1].length ? messages[1].length - 1 : messages[1].length;
			model->inject_short_transfer = false;
			result.completed_messages = 2;
			result.completed_bytes = messages[0].transferred + messages[1].transferred;
			return uni::IICTransferStatus::Success;
		}
		auto& message = messages[0];
		if (message.flags & uni::IICMessageFlag_Read) {
			if (!message.read_buffer) return uni::IICTransferStatus::InvalidArgument;
			if (model->return_reset_completion) {
				for0(index, message.length) message.read_buffer[index] = 0;
				model->return_reset_completion = false;
			}
			else CopyHidI2cProtocolTestData(message.read_buffer, message.length, model->input_packet, numsof(model->input_packet));
		}
		else {
			if (!message.write_buffer || message.length < 4) return uni::IICTransferStatus::InvalidArgument;
			const uint16 register_address = uni::DecodeHIDI2CWord(message.write_buffer);
			if (register_address == 0x60u) {
				model->output_length = message.length - 2;
				CopyHidI2cProtocolTestData(model->output_packet, numsof(model->output_packet), message.write_buffer + 2, model->output_length);
			}
			else if (register_address == 0x70u) {
				const uint8 opcode = message.write_buffer[3] & 0x0Fu;
				if (opcode == 1u && message.length == 4u && message.write_buffer[2] == 0) model->reset_requested = true;
				else if (opcode == 8u && message.length == 4u) model->power_on = message.write_buffer[2] == 0;
				else if (opcode == 3u) {
					stduint data_offset = (message.write_buffer[2] & 0x0Fu) == 0x0Fu ? 5u : 4u;
					if (data_offset == 5u && (message.write_buffer[2] != 0x3Fu || message.write_buffer[4] != 0x23u)) return uni::IICTransferStatus::InvalidArgument;
					if (message.length < data_offset + 4 || uni::DecodeHIDI2CWord(message.write_buffer + data_offset) != 0x72u) return uni::IICTransferStatus::InvalidArgument;
					data_offset += 2;
					model->set_report_length = message.length - data_offset;
					CopyHidI2cProtocolTestData(model->set_report_packet, numsof(model->set_report_packet), message.write_buffer + data_offset, model->set_report_length);
				}
				else return uni::IICTransferStatus::InvalidArgument;
			}
			else return uni::IICTransferStatus::InvalidArgument;
		}
		message.transferred = model->inject_short_transfer && message.length ? message.length - 1 : message.length;
		model->inject_short_transfer = false;
		result.completed_messages = 1;
		result.completed_bytes = message.transferred;
		return uni::IICTransferStatus::Success;
	}

	bool RecoverDesignWareI2cTestBus(void* context, stduint timeout) {
		auto* model = static_cast<DesignWareI2cTestModel*>(context);
		if (!model || !timeout) return false;
		++model->recovery_count;
		if (!model->recover_bus_succeeds) return false;
		model->hold_bus_busy = false;
		return true;
	}

	DesignWareI2cTestFixture::DesignWareI2cTestFixture() : controller(&model, ReadDesignWareI2cTestRegister, WriteDesignWareI2cTestRegister) {
		model.registers[_IMM(uni::IICDesignWareReg::ComponentType) / sizeof(uint32)] = uni::IIC_DESIGNWARE::ComponentTypeValue;
		model.registers[_IMM(uni::IICDesignWareReg::ComponentVersion) / sizeof(uint32)] = 0x3132312Au;
		model.registers[_IMM(uni::IICDesignWareReg::ComponentParameter) / sizeof(uint32)] = (63u << 16) | (63u << 8);
		model.registers[_IMM(uni::IICDesignWareReg::Enable) / sizeof(uint32)] = 1;
		model.registers[_IMM(uni::IICDesignWareReg::EnableStatus) / sizeof(uint32)] = 1;
	}
	#endif

	const DeviceAcpiSerialResource* FindI2cEndpointResource(const DeviceNode* endpoint) {
		if (!endpoint || !endpoint->fields.acpi_info) return nullptr;
		const auto* info = endpoint->fields.acpi_info;
		for0(index, info->serial_resource_count) {
			const auto& resource = info->serial_resources[index];
			if (resource.bus_type != DeviceAcpiSerialBusType::I2C) continue;
			const uint16 maximum_address = resource.type_flags & AcpiI2cTypeFlagTenBitAddress ? 0x3FFu : 0x7Fu;
			if (resource.address <= maximum_address) return &resource;
		}
		return nullptr;
	}

	uni::IIC_DESIGNWARE* ResolveDesignWareI2cController(DeviceNode* endpoint) {
		if (!FindI2cEndpointResource(endpoint)) return nullptr;
		auto* bus = reinterpret_cast<DeviceNode*>(endpoint->link.getParent());
		if (!bus || DeviceNodeType(bus->fields.node_type) != DeviceNodeType::I2cBus || DriverBindingState(bus->fields.binding.state) != DriverBindingState::Started) return nullptr;
		return static_cast<uni::IIC_DESIGNWARE*>(bus->fields.binding.driver_data);
	}

	uni::IICTransferStatus TransferDesignWareI2cEndpoint(DeviceNode* endpoint, uni::IICMessage* messages, stduint message_count, stduint timeout, uni::IICTransferResult& result) {
		result = {};
		result.failed_message = message_count;
		const auto* resource = FindI2cEndpointResource(endpoint);
		auto* controller = ResolveDesignWareI2cController(endpoint);
		if (!resource || !controller || !messages || !message_count || !timeout) return uni::IICTransferStatus::InvalidArgument;
		const bool ten_bit = resource->type_flags & AcpiI2cTypeFlagTenBitAddress;
		for (stduint message_index = 0; message_index < message_count; ++message_index) {
			const auto& message = messages[message_index];
			if (message.address != resource->address || bool(message.flags & uni::IICMessageFlag_TenBitAddress) != ten_bit) {
				result.failed_message = message_index;
				return uni::IICTransferStatus::InvalidArgument;
			}
		}
		return controller->Transfer(messages, message_count, timeout, result);
	}

	stdsint ReadDesignWareI2cEndpoint(DeviceNode* endpoint, void* buffer, stduint count, stduint index, stduint flags) {
		if (index || flags || (!buffer && count)) return -1;
		if (!count) return 0;
		const auto* resource = FindI2cEndpointResource(endpoint);
		if (!resource) return -1;
		const uint16 message_flags = uni::IICMessageFlag_Read | uni::IICMessageFlag_Stop | (resource->type_flags & AcpiI2cTypeFlagTenBitAddress ? uni::IICMessageFlag_TenBitAddress : 0);
		uni::IICMessage message{ resource->address, message_flags, nullptr, static_cast<byte*>(buffer), count, 0 };
		uni::IICTransferResult result{};
		return TransferDesignWareI2cEndpoint(endpoint, &message, 1, DesignWareI2cTransferBudget, result) == uni::IICTransferStatus::Success ? stdsint(message.transferred) : -1;
	}

	stdsint SendDesignWareI2cEndpoint(DeviceNode* endpoint, const void* buffer, stduint count, stduint index, stduint flags) {
		if (index || flags || (!buffer && count)) return -1;
		if (!count) return 0;
		const auto* resource = FindI2cEndpointResource(endpoint);
		if (!resource) return -1;
		const uint16 message_flags = uni::IICMessageFlag_Stop | (resource->type_flags & AcpiI2cTypeFlagTenBitAddress ? uni::IICMessageFlag_TenBitAddress : 0);
		uni::IICMessage message{ resource->address, message_flags, static_cast<const byte*>(buffer), nullptr, count, 0 };
		uni::IICTransferResult result{};
		return TransferDesignWareI2cEndpoint(endpoint, &message, 1, DesignWareI2cTransferBudget, result) == uni::IICTransferStatus::Success ? stdsint(message.transferred) : -1;
	}

	stdsint CtrlDesignWareI2cEndpoint(DeviceNode* endpoint, stduint cmd, void* args, stduint flags) {
		if (flags || cmd != _IMM(I2cCtrlCommand::Transfer) || !args) return -1;
		auto& request = *static_cast<DeviceI2cTransferRequest*>(args);
		if (!request.result) return -1;
		request.status = TransferDesignWareI2cEndpoint(endpoint, request.messages, request.message_count, request.timeout, *request.result);
		return 0;
	}

	const DeviceNodeOps designware_i2c_endpoint_ops{
		ReadDesignWareI2cEndpoint,
		SendDesignWareI2cEndpoint,
		CtrlDesignWareI2cEndpoint,
	};

	bool BindDesignWareI2cBus(DeviceNode* bus, uni::IIC_DESIGNWARE* controller) {
		if (!bus || !controller || DeviceNodeType(bus->fields.node_type) != DeviceNodeType::I2cBus) return false;
		stduint endpoint_count = 0;
		for (auto* endpoint = reinterpret_cast<DeviceNode*>(bus->link.subf); endpoint; endpoint = reinterpret_cast<DeviceNode*>(endpoint->link.next)) {
			if (DeviceNodeType(endpoint->fields.node_type) == DeviceNodeType::I2cDevice && FindI2cEndpointResource(endpoint)) ++endpoint_count;
		}
		if (!endpoint_count) return false;
		bus->fields.binding.driver_name = "i2c-designware-bus";
		bus->fields.binding.driver_data = controller;
		bus->fields.binding.probe_result = 0;
		bus->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
		for (auto* endpoint = reinterpret_cast<DeviceNode*>(bus->link.subf); endpoint; endpoint = reinterpret_cast<DeviceNode*>(endpoint->link.next)) {
			if (DeviceNodeType(endpoint->fields.node_type) != DeviceNodeType::I2cDevice || !FindI2cEndpointResource(endpoint)) continue;
			Devsman::SetOps(endpoint, &designware_i2c_endpoint_ops);
		}
		return true;
	}

	struct IntelLpssI2cBusPlan {
		stduint bus_count = 0;
		uint32 speed_hz = 0;
		bool valid = true;
	};

	DeviceNode* FindI2cRoot() {
		auto* root = Devsman::Root();
		if (!root) return nullptr;
		for (auto* node = reinterpret_cast<DeviceNode*>(root->link.subf); node; node = reinterpret_cast<DeviceNode*>(node->link.next)) {
			if (DeviceBusType(node->fields.bus_type) == DeviceBusType::I2C && DeviceNodeType(node->fields.node_type) == DeviceNodeType::BusRoot) return node;
		}
		return nullptr;
	}

	uint32 SelectDesignWareI2cBusSpeed(const DeviceNode* bus) {
		uint32 connection_limit = 0;
		for (auto* endpoint = reinterpret_cast<DeviceNode*>(bus ? bus->link.subf : nullptr); endpoint; endpoint = reinterpret_cast<DeviceNode*>(endpoint->link.next)) {
			if (DeviceNodeType(endpoint->fields.node_type) != DeviceNodeType::I2cDevice || !endpoint->fields.acpi_info) continue;
			const auto* info = endpoint->fields.acpi_info;
			for0(index, info->serial_resource_count) {
				const auto& resource = info->serial_resources[index];
				if (resource.bus_type != DeviceAcpiSerialBusType::I2C || !resource.speed) continue;
				if (!connection_limit || resource.speed < connection_limit) connection_limit = resource.speed;
			}
		}
		if (connection_limit < 100000u) return 0;
		return connection_limit >= 400000u ? 400000u : 100000u;
	}

	IntelLpssI2cBusPlan PlanIntelLpssI2cBuses(DeviceNode* controller) {
		IntelLpssI2cBusPlan plan{};
		auto* root = FindI2cRoot();
		for (auto* bus = reinterpret_cast<DeviceNode*>(root ? root->link.subf : nullptr); bus; bus = reinterpret_cast<DeviceNode*>(bus->link.next)) {
			if (DeviceNodeType(bus->fields.node_type) != DeviceNodeType::I2cBus || bus->fields.controller != controller) continue;
			++plan.bus_count;
			const uint32 speed = SelectDesignWareI2cBusSpeed(bus);
			if (!speed) {
				plan.valid = false;
				continue;
			}
			if (!plan.speed_hz || speed < plan.speed_hz) plan.speed_hz = speed;
		}
		if (plan.bus_count && !plan.speed_hz) plan.valid = false;
		return plan;
	}

	stduint BindIntelLpssI2cBuses(DeviceNode* controller_node, uni::IIC_DESIGNWARE* controller) {
		stduint count = 0;
		auto* root = FindI2cRoot();
		for (auto* bus = reinterpret_cast<DeviceNode*>(root ? root->link.subf : nullptr); bus; bus = reinterpret_cast<DeviceNode*>(bus->link.next)) {
			if (DeviceNodeType(bus->fields.node_type) == DeviceNodeType::I2cBus && bus->fields.controller == controller_node && BindDesignWareI2cBus(bus, controller)) ++count;
		}
		return count;
	}

	DeviceControllerResourceContract* ConfigureIntelLpssI2cResources(DeviceNode* node, const DeviceControllerCompatibilityDescription& compatibility) {
		auto* contract = Devsman::EnsureControllerResourceContract(node);
		if (!contract) return nullptr;
		contract->requirements = IntelLpssI2cResourceRequirements;
		contract->status = DeviceControllerContractStatus::NotEvaluated;
		const auto* mmio = Devsman::FindResource(node, DeviceResourceType::PciBarMmio, compatibility.access_index);
		contract->access_type = mmio ? DeviceControllerAccessType::MMIO : DeviceControllerAccessType::None;
		contract->access_source = mmio ? DeviceControllerResourceSource::PciConfiguration : DeviceControllerResourceSource::None;
		contract->access_index = compatibility.access_index;
		contract->access_base = mmio ? mmio->start : 0;
		contract->access_length = mmio ? mmio->length : 0;
		contract->interrupt_mode = DeviceControllerInterruptMode::Polling;
		contract->interrupt_source = DeviceControllerResourceSource::DriverPolicy;
		contract->interrupt = 0;
		contract->input_clock_hz = compatibility.clock_model == DeviceControllerClockModel::Fixed ? compatibility.input_clock_hz : 0;
		contract->clock_source = contract->input_clock_hz ? DeviceControllerResourceSource::PlatformDescription : DeviceControllerResourceSource::None;
		contract->reset_type = DeviceControllerResetType::Register;
		contract->reset_source = DeviceControllerResourceSource::PlatformDescription;
		contract->reset_register_offset = compatibility.reset_register_offset;
		contract->reset_mask = compatibility.reset_mask;
		contract->reset_deassert_value = compatibility.reset_deassert_value;
		contract->power_state = DeviceControllerPowerState::FirmwareConfigured;
		contract->power_source = DeviceControllerResourceSource::FirmwareConfigured;
		contract->dma_mode = DeviceControllerDmaMode::NotRequired;
		contract->dma_source = DeviceControllerResourceSource::DriverPolicy;
		return contract;
	}

	uint32 ReadLpssRegisterContext(void* context, stduint offset) {
		return ReadLpssRegister(reinterpret_cast<stduint>(context), offset);
	}

	void WriteLpssRegisterContext(void* context, stduint offset, uint32 value) {
		WriteLpssRegister(reinterpret_cast<stduint>(context), offset, value);
	}

	bool ApplyControllerReset(const DeviceControllerResourceContract& contract, void* context, uni::IICDesignWareReadRegister read, uni::IICDesignWareWriteRegister write) {
		if (contract.reset_type == DeviceControllerResetType::FirmwareConfigured) return true;
		if (contract.reset_type != DeviceControllerResetType::Register || !context || !read || !write || !contract.reset_mask) return false;
		uint32 value = read(context, contract.reset_register_offset);
		const uint32 expected = contract.reset_deassert_value & contract.reset_mask;
		if ((value & contract.reset_mask) != expected) {
			value = (value & ~contract.reset_mask) | expected;
			write(context, contract.reset_register_offset, value);
			value = read(context, contract.reset_register_offset);
		}
		return (value & contract.reset_mask) == expected;
	}

	void ProgramIntelLpssI2cRemap(stduint base, const DeviceControllerCompatibilityDescription& compatibility) {
		WriteLpssRegister(base, compatibility.remap_address_low_offset, uint32(uint64(base)));
		WriteLpssRegister(base, compatibility.remap_address_high_offset, uint32(uint64(base) >> 32));
	}

	IntelLpssI2cFailure IntelLpssI2cContractFailure(DeviceControllerContractStatus status) {
		if (status == DeviceControllerContractStatus::MissingAccess) return IntelLpssI2cFailure::MissingMmio;
		if (status == DeviceControllerContractStatus::MissingClock) return IntelLpssI2cFailure::ClockUnavailable;
		return IntelLpssI2cFailure::ResourceContract;
	}

	const char* IntelLpssI2cFailureName(IntelLpssI2cFailure failure) {
		switch (failure) {
		case IntelLpssI2cFailure::MissingMmio: return "missing-mmio";
		case IntelLpssI2cFailure::AddressRange: return "address-range";
		case IntelLpssI2cFailure::CapabilitiesUnavailable: return "caps-unavailable";
		case IntelLpssI2cFailure::WrongControllerType: return "wrong-controller-type";
		case IntelLpssI2cFailure::Allocation: return "allocation";
		case IntelLpssI2cFailure::ComponentSignature: return "component-signature";
		case IntelLpssI2cFailure::CompatibilityUnavailable: return "compatibility-unavailable";
		case IntelLpssI2cFailure::ClockUnavailable: return "missing-clock";
		case IntelLpssI2cFailure::Initialization: return "initialization";
		case IntelLpssI2cFailure::ResourceContract: return "resource-contract";
		default: return "none";
		}
	}

	void RecordIntelLpssI2cFailure(DeviceNode* node, IntelLpssI2cFailure failure, uint32 capabilities = 0, uint32 detail = 0) {
		++lpss_i2c_failed;
		if (node) node->fields.binding.probe_result = -static_cast<int32>(failure);
		if (first_lpss_i2c_failure != IntelLpssI2cFailure::None) return;
		first_lpss_i2c_failure_node = node;
		first_lpss_i2c_failure = failure;
		first_lpss_i2c_failure_capabilities = capabilities;
		first_lpss_i2c_failure_detail = detail;
	}

	bool StartIntelLpssI2c(DeviceNode* node) {
		++lpss_i2c_attempted;
		const auto* compatibility = Devsman::FindControllerCompatibility(node);
		if (!compatibility || compatibility->ip_type != DeviceControllerIpType::DesignWareI2c) {
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::CompatibilityUnavailable);
			return false;
		}
		const auto bus_plan = PlanIntelLpssI2cBuses(node);
		auto* resources = ConfigureIntelLpssI2cResources(node, *compatibility);
		if (!resources) {
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::Allocation);
			return false;
		}
		if (!bus_plan.bus_count) {
			++lpss_i2c_started;
			return true;
		}
		const auto contract_status = Devsman::ValidateControllerResourceContract(*resources);
		if (contract_status != DeviceControllerContractStatus::Ready) {
			RecordIntelLpssI2cFailure(node, IntelLpssI2cContractFailure(contract_status), 0, _IMM(contract_status));
			return false;
		}
		if (resources->access_type != DeviceControllerAccessType::MMIO || resources->access_length < compatibility->minimum_access_length) {
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::MissingMmio);
			return false;
		}
		if (!resources->access_base || resources->access_base > uint64(~stduint(0)) || resources->access_length > uint64(~stduint(0)) - resources->access_base) {
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::AddressRange);
			return false;
		}
		if (!bus_plan.valid) {
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::Initialization, 0, _IMM(uni::IICDesignWareStatus::InvalidArgument));
			return false;
		}

		uni::PCI::Device pci_device{};
		pci_device.segment_group = node->fields.pci_segment;
		pci_device.bus = node->fields.pci_bus;
		pci_device.device = node->fields.pci_device;
		pci_device.function = node->fields.pci_function;
		pci_device.header_type = uni::PCI::read_header_type(pci_device);
		pci_device.class_code = uni::PCI::read_class_code(pci_device);
		lpss_pci.enable_MMIO(pci_device);

		const stduint base = stduint(resources->access_base);
		const uint32 lpss_capabilities = ReadLpssRegister(base, compatibility->capabilities_register_offset);
		if (lpss_capabilities == 0xFFFFFFFFu) {
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::CapabilitiesUnavailable, lpss_capabilities);
			return false;
		}
		if ((lpss_capabilities & compatibility->capabilities_type_mask) != compatibility->capabilities_type_value) {
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::WrongControllerType, lpss_capabilities);
			return false;
		}
		if (!ApplyControllerReset(*resources, reinterpret_cast<void*>(base), ReadLpssRegisterContext, WriteLpssRegisterContext)) {
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::ResourceContract, lpss_capabilities, resources->reset_register_offset);
			return false;
		}

		void* storage = uni_hostenv_allocator->allocate(sizeof(IntelLpssI2cInstance), 4);
		if (!storage) {
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::Allocation, lpss_capabilities);
			return false;
		}
		auto* instance = new (storage) IntelLpssI2cInstance(base + compatibility->core_register_offset, node);
		instance->lpss_capabilities = lpss_capabilities;
		instance->lpss_reset = ReadLpssRegister(base, compatibility->reset_register_offset);
		if (!instance->controller.Probe(instance->capabilities)) {
			const uint32 component = instance->capabilities.component_type;
			instance->~IntelLpssI2cInstance();
			uni_hostenv_allocator->deallocate(instance);
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::ComponentSignature, lpss_capabilities, component);
			return false;
		}
		ProgramIntelLpssI2cRemap(base, *compatibility);
		instance->input_clock_hz = resources->input_clock_hz;
		instance->bus_speed_hz = bus_plan.speed_hz;
		const uni::IICDesignWareInitialization initialization{ instance->input_clock_hz, instance->bus_speed_hz, DesignWareI2cTransferBudget, nullptr, nullptr, compatibility->i2c_sda_hold_time_ns };
		const auto status = instance->controller.Initialize(initialization);
		if (status != uni::IICDesignWareStatus::Success) {
			instance->~IntelLpssI2cInstance();
			uni_hostenv_allocator->deallocate(instance);
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::Initialization, lpss_capabilities, _IMM(status));
			return false;
		}
		instance->initialized = true;
		instance->bound_bus_count = BindIntelLpssI2cBuses(node, &instance->controller);
		++lpss_i2c_initialized;
		lpss_i2c_bound_buses += instance->bound_bus_count;
		if (!first_initialized_lpss_i2c_instance) first_initialized_lpss_i2c_instance = instance;

		instance->next = lpss_i2c_instances;
		lpss_i2c_instances = instance;
		if (!first_lpss_i2c_instance) first_lpss_i2c_instance = instance;
		node->fields.binding.driver_data = &instance->controller;
		++lpss_i2c_started;
		return true;
	}

	void LogIntelLpssI2cSummary() {
		if (!lpss_i2c_attempted) return;
		#if CONFIG_ACPI_BOOT_TEST
		if (first_lpss_i2c_instance) {
			const auto* instance = first_initialized_lpss_i2c_instance ? first_initialized_lpss_i2c_instance : first_lpss_i2c_instance;
			const auto* node = instance->pci_node;
			ploginfo("[I2C-LPSS-SUM] attempted=%u started=%u failed=%u initialized=%u buses=%u example=%04x:%02x:%02x.%x clock=%u speed=%u version=%[32H] tx-fifo=%u rx-fifo=%u",
				lpss_i2c_attempted, lpss_i2c_started, lpss_i2c_failed, lpss_i2c_initialized, lpss_i2c_bound_buses,
				(stduint)node->fields.pci_segment, (stduint)node->fields.pci_bus,
				(stduint)node->fields.pci_device, (stduint)node->fields.pci_function,
				instance->input_clock_hz, instance->bus_speed_hz,
				instance->capabilities.component_version,
				(stduint)instance->capabilities.transmit_fifo_depth,
				(stduint)instance->capabilities.receive_fifo_depth);
		}
		#endif
		if (first_lpss_i2c_failure_node) {
			const auto* node = first_lpss_i2c_failure_node;
			#if CONFIG_ACPI_BOOT_TEST
			plogwarn("[I2C-LPSS-FAIL] count=%u first=%04x:%02x:%02x.%x reason=%s caps=%[32H] detail=%[32H]",
			#else
			plogerro("[I2C-LPSS] initialization failed count=%u first=%04x:%02x:%02x.%x reason=%s caps=%[32H] detail=%[32H]",
			#endif
				lpss_i2c_failed, (stduint)node->fields.pci_segment,
				(stduint)node->fields.pci_bus, (stduint)node->fields.pci_device,
				(stduint)node->fields.pci_function,
				IntelLpssI2cFailureName(first_lpss_i2c_failure),
				first_lpss_i2c_failure_capabilities,
				first_lpss_i2c_failure_detail);
		}
	}

	#if CONFIG_ACPI_BOOT_TEST
	void RunHidI2cProtocolTest() {
		void* fixture_storage = uni_hostenv_allocator->allocate(sizeof(HidI2cProtocolTestFixture), 4);
		void* device_storage = uni_hostenv_allocator->allocate(sizeof(uni::HIDI2CDevice), 4);
		if (!fixture_storage || !device_storage) {
			if (fixture_storage) uni_hostenv_allocator->deallocate(fixture_storage, sizeof(HidI2cProtocolTestFixture));
			if (device_storage) uni_hostenv_allocator->deallocate(device_storage, sizeof(uni::HIDI2CDevice));
			plogwarn("[I2C-HID-CORE-TEST] allocation=failed");
			return;
		}
		auto* fixture = new (fixture_storage) HidI2cProtocolTestFixture();
		LoadHidI2cProtocolTestData(fixture->model);
		const uni::HIDI2CTransport transport{ &fixture->model, TransferHidI2cProtocolTest, 0x2Cu, uni::IICMessageFlag_None };
		auto* device = new (device_storage) uni::HIDI2CDevice(transport, uni_hostenv_allocator);

		const bool descriptor_passed = device->Initialize(0x20u, 128u, fixture->result) == uni::HIDI2CStatus::Success && device->Descriptor().vendor_id == 0x1234u && device->Descriptor().product_id == 0x5678u;
		const bool report_descriptor_passed = device->ReadReportDescriptor(128u, fixture->result) == uni::HIDI2CStatus::Success && fixture->result.length == numsof(fixture->model.report_descriptor) && device->ReportDescriptor()[0] == 0x05u && device->ReportDescriptor()[51] == 0x01u;
		uni::HIDReportParseResult parse_result{};
		const bool report_layout_passed = device->ParseReportDescriptor(parse_result) == uni::HIDReportDescriptorStatus::Success && device->ReportLayout().DefinitionCount() == 1 && device->ReportLayout().FieldCount() == 3 && device->ReportLayout().CollectionCount() == 2;
		const bool input_passed = device->ReadInput(fixture->report, numsof(fixture->report), 128u, fixture->result) == uni::HIDI2CStatus::Success && fixture->result.length == 3u && fixture->report[0] == 1u && fixture->report[1] == 0xAAu && fixture->report[2] == 0x55u;

		fixture->payload[0] = 0x11u;
		fixture->payload[1] = 0x22u;
		const bool output_passed = device->WriteOutput(3u, fixture->payload, 2u, 128u, fixture->result) == uni::HIDI2CStatus::Success && fixture->model.output_length == 5u && uni::DecodeHIDI2CWord(fixture->model.output_packet) == 5u && fixture->model.output_packet[2] == 3u && fixture->model.output_packet[3] == 0x11u && fixture->model.output_packet[4] == 0x22u;
		const bool feature_get_passed = device->getReport(uni::HIDI2CReportType::Feature, 0x23u, fixture->report, numsof(fixture->report) - 2u, 128u, fixture->result) == uni::HIDI2CStatus::Success && fixture->result.length == 3u && fixture->report[0] == 0x23u && fixture->report[1] == 0x33u && fixture->report[2] == 0x44u;
		fixture->payload[0] = 0x55u;
		fixture->payload[1] = 0x66u;
		const bool feature_set_passed = device->setReport(uni::HIDI2CReportType::Feature, 0x23u, fixture->payload, 2u, 128u, fixture->result) == uni::HIDI2CStatus::Success && fixture->model.set_report_length == 5u && uni::DecodeHIDI2CWord(fixture->model.set_report_packet) == 5u && fixture->model.set_report_packet[2] == 0x23u && fixture->model.set_report_packet[3] == 0x55u && fixture->model.set_report_packet[4] == 0x66u;

		const bool power_passed = device->setPower(uni::HIDI2CPowerState::On, 128u, fixture->result) == uni::HIDI2CStatus::Success && fixture->model.power_on;
		const bool reset_started = device->BeginReset(128u, fixture->result) == uni::HIDI2CStatus::Success && device->IsResetPending() && fixture->model.reset_requested;
		fixture->model.return_reset_completion = true;
		const bool reset_completed = device->ReadInput(fixture->report, numsof(fixture->report), 128u, fixture->result) == uni::HIDI2CStatus::ResetComplete && !device->IsResetPending();
		const bool reset_passed = power_passed && reset_started && reset_completed;
		const bool reset_timeout_started = device->BeginReset(128u, fixture->result) == uni::HIDI2CStatus::Success;
		const bool reset_expired = reset_timeout_started && device->ExpireReset(fixture->result) == uni::HIDI2CStatus::Timeout && !device->IsResetPending();
		const bool reset_restarted = reset_expired && device->BeginReset(128u, fixture->result) == uni::HIDI2CStatus::Success;
		fixture->report[0] = 0;
		fixture->report[1] = 0;
		const bool reset_timeout_passed = reset_restarted && device->AcceptInput(fixture->report, 2u, fixture->report, numsof(fixture->report), fixture->result) == uni::HIDI2CStatus::ResetComplete && !device->IsResetPending();

		fixture->model.inject_short_transfer = true;
		const bool short_passed = device->ReadReportDescriptor(128u, fixture->result) == uni::HIDI2CStatus::ShortTransfer;
		fixture->model.inject_address_nack = true;
		const bool nack_passed = device->ReadReportDescriptor(128u, fixture->result) == uni::HIDI2CStatus::TransferFailed && fixture->result.transfer_status == uni::IICTransferStatus::AddressNack;
		fixture->model.input_packet[0] = 65u;
		const bool length_passed = device->ReadInput(fixture->report, numsof(fixture->report), 128u, fixture->result) == uni::HIDI2CStatus::InvalidReportLength;
		const bool oversized_report_passed = device->WriteOutput(3u, fixture->payload, uni::HIDI2CMaximumTransferLength + 1u, 128u, fixture->result) == uni::HIDI2CStatus::InvalidReportLength;
		fixture->model.descriptor[10] = 0x01u;
		fixture->model.descriptor[11] = 0x10u;
		const bool oversized_descriptor_passed = device->Initialize(0x20u, 128u, fixture->result) == uni::HIDI2CStatus::InvalidDescriptor && fixture->result.descriptor_status == uni::HIDI2CDescriptorStatus::InvalidInputLength;
		const bool oversize_passed = oversized_report_passed && oversized_descriptor_passed;

		ploginfo("[I2C-HID-CORE-TEST] descriptor=%s report-desc=%s report-layout=%s input=%s output=%s feature-get=%s feature-set=%s reset=%s reset-timeout=%s short=%s nack=%s length=%s oversize=%s", descriptor_passed ? "success" : "failed", report_descriptor_passed ? "success" : "failed", report_layout_passed ? "success" : "failed", input_passed ? "success" : "failed", output_passed ? "success" : "failed", feature_get_passed ? "success" : "failed", feature_set_passed ? "success" : "failed", reset_passed ? "success" : "failed", reset_timeout_passed ? "success" : "failed", short_passed ? "success" : "failed", nack_passed ? "success" : "failed", length_passed ? "success" : "failed", oversize_passed ? "success" : "failed");
		device->~HIDI2CDevice();
		fixture->~HidI2cProtocolTestFixture();
		uni_hostenv_allocator->deallocate(device_storage, sizeof(uni::HIDI2CDevice));
		uni_hostenv_allocator->deallocate(fixture_storage, sizeof(HidI2cProtocolTestFixture));
	}

	void RunDesignWareI2cInitializationTest() {
		if (designware_i2c_test_fixture) return;
		auto* endpoint = Devsman::FindNamedNode(DeviceNodeType::I2cDevice, "i2c-dev@0x2C");
		auto* bus = reinterpret_cast<DeviceNode*>(endpoint ? endpoint->link.getParent() : nullptr);
		auto* controller = bus ? bus->fields.controller : nullptr;
		if (!endpoint || !endpoint->fields.acpi_info || !(endpoint->fields.acpi_info->flags & DeviceAcpiInfoFlag_HasHardwareId) || StrCompare(endpoint->fields.acpi_info->hardware_id, "ALPS0001") != 0) return;
		if (!controller || !controller->fields.acpi_info || !(controller->fields.acpi_info->flags & DeviceAcpiInfoFlag_HasHardwareId) || StrCompare(controller->fields.acpi_info->hardware_id, "MCOA0001") != 0) return;
		void* storage = uni_hostenv_allocator->allocate(sizeof(DesignWareI2cTestFixture), 4);
		if (!storage) {
			plogwarn("[I2C-DW-TEST] allocation=failed");
			return;
		}
		auto* fixture = new (storage) DesignWareI2cTestFixture();
		const auto* broxton_a = Devsman::FindPciControllerCompatibility(0x8086u, 0x0AACu);
		const auto* broxton_b = Devsman::FindPciControllerCompatibility(0x8086u, 0x1ABAu);
		const auto* apollo_lake = Devsman::FindPciControllerCompatibility(0x8086u, 0x5AACu);
		const auto* apollo_lake_last = Devsman::FindPciControllerCompatibility(0x8086u, 0x5ABAu);
		const bool compatibility_passed = broxton_a && broxton_b && broxton_a == broxton_b && apollo_lake && apollo_lake == apollo_lake_last && broxton_a != apollo_lake && broxton_a->input_clock_hz == 133000000u && apollo_lake->input_clock_hz == 133000000u && broxton_a->i2c_sda_hold_time_ns == 42u && apollo_lake->i2c_sda_hold_time_ns == 207u && apollo_lake->reset_register_offset == 0x204u && apollo_lake->capabilities_register_offset == 0x2FCu && !Devsman::FindPciControllerCompatibility(0x8086u, 0x5ABBu);
		const auto fast_initialize_status = fixture->controller.Initialize(100000000u, 400000u, 32u);
		const uint32 control = fixture->controller.ReadRegister(uni::IICDesignWareReg::Control);
		const uint32 standard_high = fixture->controller.ReadRegister(uni::IICDesignWareReg::StandardSclHighCount);
		const uint32 standard_low = fixture->controller.ReadRegister(uni::IICDesignWareReg::StandardSclLowCount);
		const uint32 fast_high = fixture->controller.ReadRegister(uni::IICDesignWareReg::FastSclHighCount);
		const uint32 fast_low = fixture->controller.ReadRegister(uni::IICDesignWareReg::FastSclLowCount);
		const bool fast_initialize_passed = fast_initialize_status == uni::IICDesignWareStatus::Success && control == 0x65u && standard_high == 427u && standard_low == 499u && fast_high == 87u && fast_low == 159u;
		const auto standard_initialize_status = fixture->controller.Initialize(100000000u, 100000u, 32u);
		const bool standard_initialize_passed = standard_initialize_status == uni::IICDesignWareStatus::Success && fixture->controller.ReadRegister(uni::IICDesignWareReg::Control) == 0x63u;
		const uni::IICDesignWareInitialization apollo_lake_initialization{ apollo_lake ? apollo_lake->input_clock_hz : 0, 400000u, 32u, nullptr, nullptr, apollo_lake ? apollo_lake->i2c_sda_hold_time_ns : 0 };
		const auto apollo_lake_initialize_status = fixture->controller.Initialize(apollo_lake_initialization);
		const bool apollo_lake_initialize_passed = apollo_lake_initialize_status == uni::IICDesignWareStatus::Success && fixture->controller.ReadRegister(uni::IICDesignWareReg::StandardSclHighCount) == 569u && fixture->controller.ReadRegister(uni::IICDesignWareReg::StandardSclLowCount) == 664u && fixture->controller.ReadRegister(uni::IICDesignWareReg::FastSclHighCount) == 117u && fixture->controller.ReadRegister(uni::IICDesignWareReg::FastSclLowCount) == 212u && fixture->controller.ReadRegister(uni::IICDesignWareReg::SdaHold) == 0x0001001Cu;
		fixture->model.hold_enable_status = true;
		fixture->model.registers[_IMM(uni::IICDesignWareReg::EnableStatus) / sizeof(uint32)] = 1;
		const auto disable_timeout_status = fixture->controller.Initialize(100000000u, 400000u, 8u);
		const bool disable_timeout_passed = disable_timeout_status == uni::IICDesignWareStatus::DisableTimeout;
		fixture->model.registers[_IMM(uni::IICDesignWareReg::EnableStatus) / sizeof(uint32)] = 0;
		const auto enable_timeout_status = fixture->controller.Initialize(100000000u, 400000u, 8u);
		const bool enable_timeout_passed = enable_timeout_status == uni::IICDesignWareStatus::EnableTimeout;

		fixture->model.hold_enable_status = false;
		const bool reinitialize_after_timeout_passed = fixture->controller.Initialize(100000000u, 400000u, 32u) == uni::IICDesignWareStatus::Success;
		fixture->command[0] = 0x10u;
		fixture->command[1] = 0xA5u;
		fixture->command[2] = 0x5Au;
		const auto write_status = fixture->controller.Transfer(0x2Cu, fixture->command, 3u, nullptr, 0, 128u);
		const bool write_passed = write_status == uni::IICDesignWareStatus::Success && fixture->model.memory[0x10] == 0xA5u && fixture->model.memory[0x11] == 0x5Au && fixture->model.saw_stop;
		fixture->model.saw_restart = false;
		fixture->model.saw_stop = false;
		fixture->command[0] = 0x10u;
		const auto combined_status = fixture->controller.Transfer(0x2Cu, fixture->command, 1u, fixture->received, 2u, 128u);
		const bool combined_passed = combined_status == uni::IICDesignWareStatus::Success && fixture->received[0] == 0xA5u && fixture->received[1] == 0x5Au && fixture->model.saw_restart && fixture->model.saw_stop;

		for0(index, numsof(fixture->received)) fixture->model.memory[0x20u + index] = byte(index);
		fixture->command[0] = 0x20u;
		const auto fifo_status = fixture->controller.Transfer(0x2Cu, fixture->command, 1u, fixture->received, numsof(fixture->received), 256u);
		bool fifo_passed = fifo_status == uni::IICDesignWareStatus::Success;
		for0(index, numsof(fixture->received)) {
			if (fixture->received[index] != byte(index)) fifo_passed = false;
		}

		uint32 abort_source = 0;
		fixture->command[0] = 0;
		const auto nack_status = fixture->controller.Transfer(0x2Du, fixture->command, 1u, nullptr, 0, 32u, &abort_source);
		const bool nack_passed = nack_status == uni::IICDesignWareStatus::TransmitAbort && abort_source == 1u;
		fixture->model.hold_transfer_progress = true;
		const auto transfer_timeout_status = fixture->controller.Transfer(0x2Cu, fixture->command, 1u, nullptr, 0, 8u);
		const bool transfer_timeout_passed = transfer_timeout_status == uni::IICDesignWareStatus::TransferTimeout;

		fixture->model.hold_transfer_progress = false;
		auto ResetMessages = [&]() {
			for0(message_index, numsof(fixture->messages)) fixture->messages[message_index] = {};
			fixture->transfer_result = {};
		};
		ResetMessages();
		fixture->command[0] = 0x40u;
		fixture->command[1] = 0x3Cu;
		fixture->messages[0] = { 0x2Cu, uni::IICMessageFlag_Stop, fixture->command, nullptr, 2u, 0 };
		const auto message_write_status = fixture->controller.Transfer(fixture->messages, 1, 128u, fixture->transfer_result);
		const bool message_write_passed = message_write_status == uni::IICTransferStatus::Success && fixture->messages[0].transferred == 2u && fixture->transfer_result.completed_messages == 1u && fixture->transfer_result.completed_bytes == 2u && fixture->model.memory[0x40] == 0x3Cu;

		ResetMessages();
		fixture->model.register_pointer = 0x40u;
		fixture->received[0] = 0;
		fixture->messages[0] = { 0x2Cu, uint16(uni::IICMessageFlag_Read | uni::IICMessageFlag_Stop), nullptr, fixture->received, 1u, 0 };
		const auto message_read_status = fixture->controller.Transfer(fixture->messages, 1, 128u, fixture->transfer_result);
		const bool message_read_passed = message_read_status == uni::IICTransferStatus::Success && fixture->messages[0].transferred == 1u && fixture->received[0] == 0x3Cu;

		ResetMessages();
		fixture->model.saw_restart = false;
		fixture->model.saw_stop = false;
		fixture->command[0] = 0x40u;
		fixture->messages[0] = { 0x2Cu, uni::IICMessageFlag_None, fixture->command, nullptr, 1u, 0 };
		fixture->messages[1] = { 0x2Cu, uint16(uni::IICMessageFlag_Read | uni::IICMessageFlag_Stop), nullptr, fixture->received, 1u, 0 };
		const auto message_restart_status = fixture->controller.Transfer(fixture->messages, 2, 128u, fixture->transfer_result);
		const bool message_restart_passed = message_restart_status == uni::IICTransferStatus::Success && fixture->transfer_result.completed_messages == 2u && fixture->received[0] == 0x3Cu && fixture->model.saw_restart && fixture->model.saw_stop;

		ResetMessages();
		for0(index, numsof(fixture->received)) fixture->model.memory[0x60u + index] = byte(index ^ 0x5Au);
		fixture->command[0] = 0x60u;
		fixture->messages[0] = { 0x2Cu, uni::IICMessageFlag_None, fixture->command, nullptr, 1u, 0 };
		fixture->messages[1] = { 0x2Cu, uint16(uni::IICMessageFlag_Read | uni::IICMessageFlag_Stop), nullptr, fixture->received, numsof(fixture->received), 0 };
		const auto message_fifo_status = fixture->controller.Transfer(fixture->messages, 2, 256u, fixture->transfer_result);
		bool message_fifo_passed = message_fifo_status == uni::IICTransferStatus::Success && fixture->messages[1].transferred == numsof(fixture->received);
		for0(index, numsof(fixture->received)) {
			if (fixture->received[index] != byte(index ^ 0x5Au)) message_fifo_passed = false;
		}
		const uni::IICDesignWareInitialization recovery_initialization{ 100000000u, 400000u, 32u, &fixture->model, RecoverDesignWareI2cTestBus, 0u };
		const bool recovery_initialization_passed = fixture->controller.Initialize(recovery_initialization) == uni::IICDesignWareStatus::Success;

		ResetMessages();
		fixture->command[0] = 0;
		fixture->messages[0] = { 0x2Du, uni::IICMessageFlag_Stop, fixture->command, nullptr, 1u, 0 };
		const auto message_address_nack_status = fixture->controller.Transfer(fixture->messages, 1, 32u, fixture->transfer_result);
		const bool message_address_nack_passed = message_address_nack_status == uni::IICTransferStatus::AddressNack && fixture->transfer_result.failed_message == 0 && fixture->transfer_result.hardware_status == 1u;

		ResetMessages();
		fixture->model.injected_abort_source = 1u << 3;
		fixture->model.inject_stale_receive = true;
		fixture->messages[0] = { 0x2Cu, uni::IICMessageFlag_Stop, fixture->command, nullptr, 1u, 0 };
		const auto message_data_nack_status = fixture->controller.Transfer(fixture->messages, 1, 32u, fixture->transfer_result);
		const bool message_data_nack_passed = message_data_nack_status == uni::IICTransferStatus::DataNack && fixture->transfer_result.hardware_status == (1u << 3);
		const bool fifo_clean_passed = fixture->model.receive_count == 0;

		ResetMessages();
		fixture->model.injected_abort_source = 1u << 12;
		fixture->messages[0] = { 0x2Cu, uni::IICMessageFlag_Stop, fixture->command, nullptr, 1u, 0 };
		const auto message_arbitration_status = fixture->controller.Transfer(fixture->messages, 1, 32u, fixture->transfer_result);
		const bool message_arbitration_passed = message_arbitration_status == uni::IICTransferStatus::ArbitrationLost && fixture->transfer_result.hardware_status == (1u << 12);
		ResetMessages();
		fixture->command[0] = 0x42u;
		fixture->messages[0] = { 0x2Cu, uni::IICMessageFlag_Stop, fixture->command, nullptr, 1u, 0 };
		const auto abort_recovery_status = fixture->controller.Transfer(fixture->messages, 1, 64u, fixture->transfer_result);
		const bool abort_recovery_passed = abort_recovery_status == uni::IICTransferStatus::Success && !(fixture->model.registers[_IMM(uni::IICDesignWareReg::RawInterruptStatus) / sizeof(uint32)] & (1u << 6));

		ResetMessages();
		const stduint bus_recovery_count = fixture->model.recovery_count;
		fixture->model.hold_bus_busy = true;
		fixture->messages[0] = { 0x2Cu, uni::IICMessageFlag_Stop, fixture->command, nullptr, 1u, 0 };
		const auto message_busy_status = fixture->controller.Transfer(fixture->messages, 1, 8u, fixture->transfer_result);
		const bool message_busy_passed = message_busy_status == uni::IICTransferStatus::BusBusy && fixture->transfer_result.failed_message == 0;
		ResetMessages();
		fixture->messages[0] = { 0x2Cu, uni::IICMessageFlag_Stop, fixture->command, nullptr, 1u, 0 };
		const auto bus_recovery_followup_status = fixture->controller.Transfer(fixture->messages, 1, 64u, fixture->transfer_result);
		const bool bus_recovery_passed = message_busy_passed && fixture->model.recovery_count == bus_recovery_count + 1u && !fixture->model.hold_bus_busy && bus_recovery_followup_status == uni::IICTransferStatus::Success;

		ResetMessages();
		fixture->model.recover_bus_succeeds = false;
		fixture->model.hold_bus_busy = true;
		const stduint failed_recovery_count = fixture->model.recovery_count;
		fixture->messages[0] = { 0x2Cu, uni::IICMessageFlag_Stop, fixture->command, nullptr, 1u, 0 };
		const auto recovery_failure_status = fixture->controller.Transfer(fixture->messages, 1, 8u, fixture->transfer_result);
		const bool recovery_failure_detected = recovery_failure_status == uni::IICTransferStatus::BusBusy && fixture->model.recovery_count == failed_recovery_count + 1u;
		fixture->model.recover_bus_succeeds = true;
		fixture->model.hold_bus_busy = false;
		const bool recovery_failure_reinitialized = fixture->controller.Initialize(recovery_initialization) == uni::IICDesignWareStatus::Success;
		const bool recovery_failure_passed = recovery_failure_detected && recovery_failure_reinitialized;

		ResetMessages();
		fixture->model.hold_transfer_progress = true;
		fixture->messages[0] = { 0x2Cu, uni::IICMessageFlag_Stop, fixture->command, nullptr, 1u, 0 };
		const auto message_timeout_status = fixture->controller.Transfer(fixture->messages, 1, 8u, fixture->transfer_result);
		const bool message_timeout_passed = message_timeout_status == uni::IICTransferStatus::Timeout && fixture->transfer_result.failed_message == 0;
		fixture->model.hold_transfer_progress = false;
		ResetMessages();
		fixture->messages[0] = { 0x2Cu, uni::IICMessageFlag_Stop, fixture->command, nullptr, 1u, 0 };
		const auto timeout_recovery_status = fixture->controller.Transfer(fixture->messages, 1, 64u, fixture->transfer_result);
		const bool timeout_recovery_passed = timeout_recovery_status == uni::IICTransferStatus::Success;

		ResetMessages();
		const stduint invalid_register_accesses = fixture->model.register_access_count;
		fixture->messages[0] = { 0x2Cu, uint16(1u << 15), fixture->command, nullptr, 1u, 0 };
		const auto message_invalid_status = fixture->controller.Transfer(fixture->messages, 1, 32u, fixture->transfer_result);
		const bool message_invalid_passed = message_invalid_status == uni::IICTransferStatus::InvalidArgument && fixture->model.register_access_count == invalid_register_accesses;

		ResetMessages();
		fixture->model.accepted_address = 0x2AAu;
		fixture->model.accepted_ten_bit = true;
		fixture->messages[0] = { 0x2AAu, uint16(uni::IICMessageFlag_TenBitAddress | uni::IICMessageFlag_Stop), fixture->command, nullptr, 1u, 0 };
		const auto message_ten_bit_status = fixture->controller.Transfer(fixture->messages, 1, 64u, fixture->transfer_result);
		const uint32 ten_bit_control = fixture->model.registers[_IMM(uni::IICDesignWareReg::Control) / sizeof(uint32)];
		const uint32 ten_bit_target = fixture->model.registers[_IMM(uni::IICDesignWareReg::TargetAddress) / sizeof(uint32)];
		const bool message_ten_bit_passed = message_ten_bit_status == uni::IICTransferStatus::Success && (ten_bit_control & (1u << 4)) && (ten_bit_target & 0x3FFu) == 0x2AAu && (ten_bit_target & (1u << 12));
		fixture->model.accepted_address = 0x2Cu;
		fixture->model.accepted_ten_bit = false;

		ResetMessages();
		fixture->model.restart_count = 0;
		fixture->model.stop_count = 0;
		fixture->command[0] = 0x70u;
		fixture->command[1] = 0x71u;
		fixture->messages[0] = { 0x2Cu, uni::IICMessageFlag_Stop, fixture->command, nullptr, 1u, 0 };
		fixture->messages[1] = { 0x2Cu, uni::IICMessageFlag_Stop, fixture->command + 1, nullptr, 1u, 0 };
		const auto message_split_status = fixture->controller.Transfer(fixture->messages, 2, 128u, fixture->transfer_result);
		const bool message_split_passed = message_split_status == uni::IICTransferStatus::Success && fixture->transfer_result.completed_messages == 2u && fixture->model.restart_count == 0 && fixture->model.stop_count == 2u;

		ResetMessages();
		fixture->command[0] = 0x72u;
		fixture->messages[0] = { 0x2Cu, uni::IICMessageFlag_Stop, fixture->command, nullptr, 1u, 0 };
		const bool continuous_first = fixture->controller.Transfer(fixture->messages, 1, 64u, fixture->transfer_result) == uni::IICTransferStatus::Success;
		ResetMessages();
		fixture->model.accepted_address = 0x2AAu;
		fixture->model.accepted_ten_bit = true;
		fixture->messages[0] = { 0x2AAu, uint16(uni::IICMessageFlag_TenBitAddress | uni::IICMessageFlag_Stop), fixture->command, nullptr, 1u, 0 };
		const bool continuous_second = fixture->controller.Transfer(fixture->messages, 1, 64u, fixture->transfer_result) == uni::IICTransferStatus::Success;
		ResetMessages();
		fixture->model.accepted_address = 0x2Cu;
		fixture->model.accepted_ten_bit = false;
		fixture->messages[0] = { 0x2Cu, uni::IICMessageFlag_Stop, fixture->command, nullptr, 1u, 0 };
		const bool continuous_third = fixture->controller.Transfer(fixture->messages, 1, 64u, fixture->transfer_result) == uni::IICTransferStatus::Success;
		const bool continuous_passed = continuous_first && continuous_second && continuous_third;
		const bool reconfigure_passed = reinitialize_after_timeout_passed && recovery_initialization_passed && recovery_failure_reinitialized && fixture->controller.ReadRegister(uni::IICDesignWareReg::Control) == 0x65u && fixture->controller.ReadRegister(uni::IICDesignWareReg::StandardSclHighCount) == 427u && fixture->controller.ReadRegister(uni::IICDesignWareReg::FastSclHighCount) == 87u && fixture->controller.ReadRegister(uni::IICDesignWareReg::EnableStatus) == 1u;

		const bool route_initialized = fixture->controller.Initialize(100000000u, 400000u, 32u) == uni::IICDesignWareStatus::Success;
		LoadDesignWareI2cHidDescriptor(fixture->model);
		const bool route_bound = route_initialized && SelectDesignWareI2cBusSpeed(bus) == 400000u && BindDesignWareI2cBus(bus, &fixture->controller);
		bool route_passed = false;
		bool message_route_passed = false;
		if (route_bound) {
			fixture->command[0] = 0x30u;
			fixture->command[1] = 0xC3u;
			const bool route_write = Devsman::Send(endpoint, fixture->command, 2u) == 2;
			fixture->command[0] = 0x30u;
			const bool route_select = Devsman::Send(endpoint, fixture->command, 1u) == 1;
			fixture->received[0] = 0;
			const bool route_read = Devsman::Read(endpoint, fixture->received, 1u) == 1;
			route_passed = route_write && route_select && route_read && fixture->received[0] == 0xC3u;
			ResetMessages();
			fixture->command[0] = 0x30u;
			fixture->received[0] = 0;
			fixture->messages[0] = { 0x2Cu, uni::IICMessageFlag_None, fixture->command, nullptr, 1u, 0 };
			fixture->messages[1] = { 0x2Cu, uint16(uni::IICMessageFlag_Read | uni::IICMessageFlag_Stop), nullptr, fixture->received, 1u, 0 };
			DeviceI2cTransferRequest request{ fixture->messages, 2, 128u, &fixture->transfer_result, uni::IICTransferStatus::InvalidArgument };
			message_route_passed = Devsman::Ctrl(endpoint, _IMM(I2cCtrlCommand::Transfer), &request) == 0 && request.status == uni::IICTransferStatus::Success && fixture->received[0] == 0xC3u;
			designware_i2c_test_fixture = fixture;
		}

		auto* contracts = new DeviceControllerResourceContract[3]{};
		bool contract_a_passed = false;
		bool contract_b_passed = false;
		bool missing_clock_passed = false;
		if (contracts) {
			auto& contract_a = contracts[0];
			contract_a.requirements = IntelLpssI2cResourceRequirements;
			contract_a.access_type = DeviceControllerAccessType::MMIO;
			contract_a.access_source = DeviceControllerResourceSource::PciConfiguration;
			contract_a.access_base = 1;
			contract_a.access_length = sizeof(fixture->model.registers);
			contract_a.interrupt_mode = DeviceControllerInterruptMode::Polling;
			contract_a.interrupt_source = DeviceControllerResourceSource::DriverPolicy;
			contract_a.clock_source = DeviceControllerResourceSource::PlatformDescription;
			contract_a.input_clock_hz = 100000000u;
			contract_a.reset_type = DeviceControllerResetType::Register;
			contract_a.reset_source = DeviceControllerResourceSource::PlatformDescription;
			contract_a.reset_register_offset = 0x204u;
			contract_a.reset_mask = 0x07u;
			contract_a.reset_deassert_value = 0x05u;
			contract_a.power_state = DeviceControllerPowerState::FirmwareConfigured;
			contract_a.power_source = DeviceControllerResourceSource::FirmwareConfigured;
			contract_a.identity_type = DeviceControllerIdentityType::Pci;
			contract_a.identity_source = DeviceControllerResourceSource::PciConfiguration;
			contract_a.acpi_path = "\\_SB.PCI0.MI2C";
			contract_a.dma_mode = DeviceControllerDmaMode::NotRequired;
			contract_a.dma_source = DeviceControllerResourceSource::DriverPolicy;
			fixture->model.registers[contract_a.reset_register_offset / sizeof(uint32)] = 0xA0u;
			const uni::IICDesignWareInitialization initialization_a{ contract_a.input_clock_hz, 400000u, 32u, nullptr, nullptr, 0u };
			contract_a_passed = Devsman::ValidateControllerResourceContract(contract_a) == DeviceControllerContractStatus::Ready && ApplyControllerReset(contract_a, &fixture->model, ReadDesignWareI2cTestRegister, WriteDesignWareI2cTestRegister) && fixture->controller.Initialize(initialization_a) == uni::IICDesignWareStatus::Success && (fixture->model.registers[contract_a.reset_register_offset / sizeof(uint32)] & contract_a.reset_mask) == (contract_a.reset_deassert_value & contract_a.reset_mask);

			auto& contract_b = contracts[1];
			contract_b = contract_a;
			contract_b.status = DeviceControllerContractStatus::NotEvaluated;
			contract_b.input_clock_hz = apollo_lake ? apollo_lake->input_clock_hz : 0;
			contract_b.reset_register_offset = 0x208u;
			contract_b.reset_mask = 0x30u;
			contract_b.reset_deassert_value = 0x20u;
			fixture->model.registers[contract_b.reset_register_offset / sizeof(uint32)] = 0xC0u;
			const uni::IICDesignWareInitialization initialization_b{ contract_b.input_clock_hz, 400000u, 32u, nullptr, nullptr, 0u };
			contract_b_passed = Devsman::ValidateControllerResourceContract(contract_b) == DeviceControllerContractStatus::Ready && ApplyControllerReset(contract_b, &fixture->model, ReadDesignWareI2cTestRegister, WriteDesignWareI2cTestRegister) && fixture->controller.Initialize(initialization_b) == uni::IICDesignWareStatus::Success && (fixture->model.registers[contract_b.reset_register_offset / sizeof(uint32)] & contract_b.reset_mask) == (contract_b.reset_deassert_value & contract_b.reset_mask);

			auto& missing_clock = contracts[2];
			missing_clock = contract_a;
			missing_clock.status = DeviceControllerContractStatus::NotEvaluated;
			missing_clock.clock_source = DeviceControllerResourceSource::None;
			missing_clock.input_clock_hz = 0;
			const stduint register_accesses = fixture->model.register_access_count;
			const stduint data_accesses = fixture->model.data_register_access_count;
			missing_clock_passed = Devsman::ValidateControllerResourceContract(missing_clock) == DeviceControllerContractStatus::MissingClock && fixture->model.register_access_count == register_accesses && fixture->model.data_register_access_count == data_accesses;
			delete[] contracts;
		}

		ploginfo("[I2C-DW-TEST] fast=%s standard=%s apl=%s compat=%s disable-timeout=%s enable-timeout=%s write=%s restart-read=%s fifo=%s nack=%s transfer-timeout=%s route=%s contract-a=%s contract-b=%s missing-clock=%s", fast_initialize_passed ? "success" : "failed", standard_initialize_passed ? "success" : "failed", apollo_lake_initialize_passed ? "success" : "failed", compatibility_passed ? "success" : "failed", disable_timeout_passed ? "success" : "failed", enable_timeout_passed ? "success" : "failed", write_passed ? "success" : "failed", combined_passed ? "success" : "failed", fifo_passed ? "success" : "failed", nack_passed ? "success" : "failed", transfer_timeout_passed ? "success" : "failed", route_passed ? "success" : "failed", contract_a_passed ? "success" : "failed", contract_b_passed ? "success" : "failed", missing_clock_passed ? "success" : "failed");
		ploginfo("[I2C-MSG-TEST] write=%s read=%s restart=%s fifo=%s addr-nack=%s data-nack=%s arbitration=%s busy=%s timeout=%s invalid=%s ten-bit=%s split-stop=%s route=%s", message_write_passed ? "success" : "failed", message_read_passed ? "success" : "failed", message_restart_passed ? "success" : "failed", message_fifo_passed ? "success" : "failed", message_address_nack_passed ? "success" : "failed", message_data_nack_passed ? "success" : "failed", message_arbitration_passed ? "success" : "failed", message_busy_passed ? "success" : "failed", message_timeout_passed ? "success" : "failed", message_invalid_passed ? "success" : "failed", message_ten_bit_passed ? "success" : "failed", message_split_passed ? "success" : "failed", message_route_passed ? "success" : "failed");
		ploginfo("[I2C-DW-CORE-TEST] recovery-init=%s abort-recovery=%s timeout-recovery=%s bus-recovery=%s recovery-failed=%s continuous=%s fifo-clean=%s reconfigure=%s", recovery_initialization_passed ? "success" : "failed", abort_recovery_passed ? "success" : "failed", timeout_recovery_passed ? "success" : "failed", bus_recovery_passed ? "success" : "failed", recovery_failure_passed ? "success" : "failed", continuous_passed ? "success" : "failed", fifo_clean_passed ? "success" : "failed", reconfigure_passed ? "success" : "failed");
		RunHidI2cProtocolTest();
		if (!route_bound) {
			fixture->~DesignWareI2cTestFixture();
			uni_hostenv_allocator->deallocate(fixture);
		}
	}
	#endif
}

_ESYM_C void R_INTEL_LPSS_I2C_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_INTEL_LPSS_I2C{
	.init = R_INTEL_LPSS_I2C_INIT,
	.name = "IntelLpssI2c",
};

void R_INTEL_LPSS_I2C_INIT() {
	if (!Devsman::RegisterDriverStarter("intel-lpss-i2c", StartIntelLpssI2c)) {
		plogerro("[I2C-LPSS] failed to register driver starter");
		return;
	}
	Devsman::StartKnownDrivers();
	LogIntelLpssI2cSummary();
	#if CONFIG_ACPI_BOOT_TEST
	RunDesignWareI2cInitializationTest();
	Devsman::StartKnownDrivers();
	#endif
}
#endif
