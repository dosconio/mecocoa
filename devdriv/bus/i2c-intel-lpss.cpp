// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: Intel LPSS DesignWare I2C Bring-up
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"

#if (_MCCA & 0xFF00) == 0x8600
#include <cpp/Device/Bus/PCI.hpp>
#include <cpp/Device/IIC>

namespace {
	constexpr stduint LpssPrivateOffset = 0x200u;
	constexpr stduint LpssPrivateReset = LpssPrivateOffset + 0x04u;
	constexpr stduint LpssPrivateRemapAddressLow = LpssPrivateOffset + 0x40u;
	constexpr stduint LpssPrivateRemapAddressHigh = LpssPrivateOffset + 0x44u;
	constexpr stduint LpssPrivateCapabilities = LpssPrivateOffset + 0xFCu;
	constexpr uint32 LpssResetDeasserted = 0x07u;
	constexpr uint32 LpssCapabilitiesTypeMask = 0xF0u;
	constexpr uint32 LpssCapabilitiesTypeI2c = 0x00u;
	constexpr uint32 IntelLpssApolloLakeI2cClock = 133000000u;
	constexpr stduint DesignWareI2cTransferBudget = 100000u;

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

	struct DesignWareI2cTestModel {
		uint32 registers[0x100u / sizeof(uint32)]{};
		byte memory[256]{};
		byte receive_fifo[64]{};
		uint16 receive_head = 0;
		uint16 receive_tail = 0;
		uint16 receive_count = 0;
		byte register_pointer = 0;
		bool expect_register = true;
		bool hold_enable_status = false;
		bool hold_transfer_progress = false;
		bool saw_restart = false;
		bool saw_stop = false;
	};

	struct DesignWareI2cTestFixture {
		DesignWareI2cTestFixture();

		DesignWareI2cTestModel model;
		uni::IIC_DESIGNWARE controller;
		byte command[3]{};
		byte received[65]{};
	};

	DesignWareI2cTestFixture* designware_i2c_test_fixture = nullptr;

	uint32 ReadLpssRegister(stduint base, stduint offset) {
		return *reinterpret_cast<volatile uint32*>(base + offset);
	}

	void WriteLpssRegister(stduint base, stduint offset, uint32 value) {
		*reinterpret_cast<volatile uint32*>(base + offset) = value;
	}

	uint32 ReadDesignWareI2cTestRegister(void* context, stduint offset) {
		auto* model = static_cast<DesignWareI2cTestModel*>(context);
		if (!model || offset >= sizeof(model->registers) || offset % sizeof(uint32)) return 0xFFFFFFFFu;
		if (offset == _IMM(uni::IICDesignWareReg::Status)) {
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
		model->registers[offset / sizeof(uint32)] = value;
		if (offset == _IMM(uni::IICDesignWareReg::Enable)) {
			if (!model->hold_enable_status) model->registers[_IMM(uni::IICDesignWareReg::EnableStatus) / sizeof(uint32)] = value & 1u;
			if (value & 1u) {
				model->expect_register = true;
				model->receive_head = 0;
				model->receive_tail = 0;
				model->receive_count = 0;
			}
			return;
		}
		if (offset != _IMM(uni::IICDesignWareReg::DataCommand) || model->hold_transfer_progress) return;
		if ((model->registers[_IMM(uni::IICDesignWareReg::TargetAddress) / sizeof(uint32)] & 0x7Fu) != 0x2Cu) {
			model->registers[_IMM(uni::IICDesignWareReg::TransmitAbortSource) / sizeof(uint32)] = 1u;
			model->registers[_IMM(uni::IICDesignWareReg::RawInterruptStatus) / sizeof(uint32)] |= 1u << 6;
			return;
		}
		if (value & (1u << 10)) model->saw_restart = true;
		if (value & (1u << 8)) {
			if (model->receive_count < numsof(model->receive_fifo)) {
				model->receive_fifo[model->receive_tail] = model->memory[model->register_pointer++];
				model->receive_tail = uint16((model->receive_tail + 1u) % numsof(model->receive_fifo));
				++model->receive_count;
			}
		}
		else if (model->expect_register) {
			model->register_pointer = byte(value);
			model->expect_register = false;
		}
		else model->memory[model->register_pointer++] = byte(value);
		if (value & (1u << 9)) {
			model->saw_stop = true;
			model->expect_register = true;
			model->registers[_IMM(uni::IICDesignWareReg::RawInterruptStatus) / sizeof(uint32)] |= 1u << 9;
		}
	}

	DesignWareI2cTestFixture::DesignWareI2cTestFixture() : controller(&model, ReadDesignWareI2cTestRegister, WriteDesignWareI2cTestRegister) {
		model.registers[_IMM(uni::IICDesignWareReg::ComponentType) / sizeof(uint32)] = uni::IIC_DESIGNWARE::ComponentTypeValue;
		model.registers[_IMM(uni::IICDesignWareReg::ComponentVersion) / sizeof(uint32)] = 0x3132312Au;
		model.registers[_IMM(uni::IICDesignWareReg::ComponentParameter) / sizeof(uint32)] = (63u << 16) | (63u << 8);
		model.registers[_IMM(uni::IICDesignWareReg::Enable) / sizeof(uint32)] = 1;
		model.registers[_IMM(uni::IICDesignWareReg::EnableStatus) / sizeof(uint32)] = 1;
	}

	const DeviceAcpiSerialResource* FindI2cEndpointResource(const DeviceNode* endpoint) {
		if (!endpoint || !endpoint->fields.acpi_info) return nullptr;
		const auto* info = endpoint->fields.acpi_info;
		for0(index, info->serial_resource_count) {
			const auto& resource = info->serial_resources[index];
			if (resource.bus_type == DeviceAcpiSerialBusType::I2C && resource.address <= 0x7Fu) return &resource;
		}
		return nullptr;
	}

	uni::IIC_DESIGNWARE* ResolveDesignWareI2cController(DeviceNode* endpoint) {
		if (!FindI2cEndpointResource(endpoint)) return nullptr;
		auto* bus = reinterpret_cast<DeviceNode*>(endpoint->link.getParent());
		if (!bus || DeviceNodeType(bus->fields.node_type) != DeviceNodeType::I2cBus || DriverBindingState(bus->fields.binding.state) != DriverBindingState::Started) return nullptr;
		return static_cast<uni::IIC_DESIGNWARE*>(bus->fields.binding.driver_data);
	}

	stdsint ReadDesignWareI2cEndpoint(DeviceNode* endpoint, void* buffer, stduint count, stduint index, stduint flags) {
		if (index || flags || (!buffer && count)) return -1;
		if (!count) return 0;
		const auto* resource = FindI2cEndpointResource(endpoint);
		auto* controller = ResolveDesignWareI2cController(endpoint);
		if (!resource || !controller) return -1;
		return controller->Transfer(resource->address, nullptr, 0, static_cast<byte*>(buffer), count, DesignWareI2cTransferBudget) == uni::IICDesignWareStatus::Success ? stdsint(count) : -1;
	}

	stdsint SendDesignWareI2cEndpoint(DeviceNode* endpoint, const void* buffer, stduint count, stduint index, stduint flags) {
		if (index || flags || (!buffer && count)) return -1;
		if (!count) return 0;
		const auto* resource = FindI2cEndpointResource(endpoint);
		auto* controller = ResolveDesignWareI2cController(endpoint);
		if (!resource || !controller) return -1;
		return controller->Transfer(resource->address, static_cast<const byte*>(buffer), count, nullptr, 0, DesignWareI2cTransferBudget) == uni::IICDesignWareStatus::Success ? stdsint(count) : -1;
	}

	const DeviceNodeOps designware_i2c_endpoint_ops{
		ReadDesignWareI2cEndpoint,
		SendDesignWareI2cEndpoint,
		nullptr,
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
			if (!Devsman::SetOps(endpoint, &designware_i2c_endpoint_ops)) continue;
			endpoint->fields.binding.driver_name = "i2c-designware-endpoint";
			endpoint->fields.binding.probe_result = 0;
			endpoint->fields.binding.state = static_cast<uint32>(DriverBindingState::Started);
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

	uint32 IntelLpssI2cInputClock(uint16 device_id) {
		switch (device_id) {
		case 0x0AACu:
		case 0x0AAEu:
		case 0x0AB0u:
		case 0x0AB2u:
		case 0x0AB4u:
		case 0x0AB6u:
		case 0x0AB8u:
		case 0x0ABAu:
		case 0x1AACu:
		case 0x1AAEu:
		case 0x1AB0u:
		case 0x1AB2u:
		case 0x1AB4u:
		case 0x1AB6u:
		case 0x1AB8u:
		case 0x1ABAu:
		case 0x5AACu:
		case 0x5AAEu:
		case 0x5AB0u:
		case 0x5AB2u:
		case 0x5AB4u:
		case 0x5AB6u:
		case 0x5AB8u:
		case 0x5ABAu:
			return IntelLpssApolloLakeI2cClock;
		default:
			return 0;
		}
	}

	void PrepareIntelLpssI2c(stduint base) {
		if ((ReadLpssRegister(base, LpssPrivateReset) & LpssResetDeasserted) != LpssResetDeasserted) WriteLpssRegister(base, LpssPrivateReset, LpssResetDeasserted);
		WriteLpssRegister(base, LpssPrivateRemapAddressLow, uint32(uint64(base)));
		WriteLpssRegister(base, LpssPrivateRemapAddressHigh, uint32(uint64(base) >> 32));
	}

	const char* IntelLpssI2cFailureName(IntelLpssI2cFailure failure) {
		switch (failure) {
		case IntelLpssI2cFailure::MissingMmio: return "missing-mmio";
		case IntelLpssI2cFailure::AddressRange: return "address-range";
		case IntelLpssI2cFailure::CapabilitiesUnavailable: return "caps-unavailable";
		case IntelLpssI2cFailure::WrongControllerType: return "wrong-controller-type";
		case IntelLpssI2cFailure::Allocation: return "allocation";
		case IntelLpssI2cFailure::ComponentSignature: return "component-signature";
		case IntelLpssI2cFailure::ClockUnavailable: return "clock-unavailable";
		case IntelLpssI2cFailure::Initialization: return "initialization";
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
		const auto* mmio = Devsman::FindResource(node, DeviceResourceType::PciBarMmio, 0);
		if (!mmio || mmio->length < 0x300u) {
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::MissingMmio);
			return false;
		}
		if (!mmio->start || mmio->start > uint64(~stduint(0)) || mmio->length > uint64(~stduint(0)) - mmio->start) {
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::AddressRange);
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

		const stduint base = stduint(mmio->start);
		const uint32 lpss_capabilities = ReadLpssRegister(base, LpssPrivateCapabilities);
		if (lpss_capabilities == 0xFFFFFFFFu) {
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::CapabilitiesUnavailable, lpss_capabilities);
			return false;
		}
		if ((lpss_capabilities & LpssCapabilitiesTypeMask) != LpssCapabilitiesTypeI2c) {
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::WrongControllerType, lpss_capabilities);
			return false;
		}

		uint32 reset = ReadLpssRegister(base, LpssPrivateReset);
		if ((reset & LpssResetDeasserted) != LpssResetDeasserted) {
			WriteLpssRegister(base, LpssPrivateReset, LpssResetDeasserted);
			reset = ReadLpssRegister(base, LpssPrivateReset);
		}

		void* storage = uni_hostenv_allocator->allocate(sizeof(IntelLpssI2cInstance), 4);
		if (!storage) {
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::Allocation, lpss_capabilities);
			return false;
		}
		auto* instance = new (storage) IntelLpssI2cInstance(base, node);
		instance->lpss_capabilities = lpss_capabilities;
		instance->lpss_reset = reset;
		if (!instance->controller.Probe(instance->capabilities)) {
			const uint32 component = instance->capabilities.component_type;
			instance->~IntelLpssI2cInstance();
			uni_hostenv_allocator->deallocate(instance);
			RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::ComponentSignature, lpss_capabilities, component);
			return false;
		}

		const auto bus_plan = PlanIntelLpssI2cBuses(node);
		if (bus_plan.bus_count) {
			instance->input_clock_hz = IntelLpssI2cInputClock(node->fields.device_id);
			instance->bus_speed_hz = bus_plan.speed_hz;
			if (!instance->input_clock_hz) {
				instance->~IntelLpssI2cInstance();
				uni_hostenv_allocator->deallocate(instance);
				RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::ClockUnavailable, lpss_capabilities, node->fields.device_id);
				return false;
			}
			if (!bus_plan.valid) {
				instance->~IntelLpssI2cInstance();
				uni_hostenv_allocator->deallocate(instance);
				RecordIntelLpssI2cFailure(node, IntelLpssI2cFailure::Initialization, lpss_capabilities, _IMM(uni::IICDesignWareStatus::InvalidArgument));
				return false;
			}
			PrepareIntelLpssI2c(base);
			instance->lpss_reset = ReadLpssRegister(base, LpssPrivateReset);
			const auto status = instance->controller.Initialize(instance->input_clock_hz, instance->bus_speed_hz, DesignWareI2cTransferBudget);
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
		}

		instance->next = lpss_i2c_instances;
		lpss_i2c_instances = instance;
		if (!first_lpss_i2c_instance) first_lpss_i2c_instance = instance;
		node->fields.binding.driver_data = &instance->controller;
		++lpss_i2c_started;
		return true;
	}

	void LogIntelLpssI2cSummary() {
		if (!lpss_i2c_attempted) return;
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
		if (first_lpss_i2c_failure_node) {
			const auto* node = first_lpss_i2c_failure_node;
			plogwarn("[I2C-LPSS-FAIL] count=%u first=%04x:%02x:%02x.%x reason=%s caps=%[32H] detail=%[32H]",
				lpss_i2c_failed, (stduint)node->fields.pci_segment,
				(stduint)node->fields.pci_bus, (stduint)node->fields.pci_device,
				(stduint)node->fields.pci_function,
				IntelLpssI2cFailureName(first_lpss_i2c_failure),
				first_lpss_i2c_failure_capabilities,
				first_lpss_i2c_failure_detail);
		}
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
		const auto fast_initialize_status = fixture->controller.Initialize(100000000u, 400000u, 32u);
		const uint32 control = fixture->controller.ReadRegister(uni::IICDesignWareReg::Control);
		const uint32 standard_high = fixture->controller.ReadRegister(uni::IICDesignWareReg::StandardSclHighCount);
		const uint32 standard_low = fixture->controller.ReadRegister(uni::IICDesignWareReg::StandardSclLowCount);
		const uint32 fast_high = fixture->controller.ReadRegister(uni::IICDesignWareReg::FastSclHighCount);
		const uint32 fast_low = fixture->controller.ReadRegister(uni::IICDesignWareReg::FastSclLowCount);
		const bool fast_initialize_passed = fast_initialize_status == uni::IICDesignWareStatus::Success && control == 0x65u && standard_high == 427u && standard_low == 499u && fast_high == 87u && fast_low == 159u;
		const auto standard_initialize_status = fixture->controller.Initialize(100000000u, 100000u, 32u);
		const bool standard_initialize_passed = standard_initialize_status == uni::IICDesignWareStatus::Success && fixture->controller.ReadRegister(uni::IICDesignWareReg::Control) == 0x63u;
		const auto apollo_lake_initialize_status = fixture->controller.Initialize(IntelLpssApolloLakeI2cClock, 400000u, 32u);
		const bool apollo_lake_initialize_passed = apollo_lake_initialize_status == uni::IICDesignWareStatus::Success && fixture->controller.ReadRegister(uni::IICDesignWareReg::StandardSclHighCount) == 569u && fixture->controller.ReadRegister(uni::IICDesignWareReg::StandardSclLowCount) == 664u && fixture->controller.ReadRegister(uni::IICDesignWareReg::FastSclHighCount) == 117u && fixture->controller.ReadRegister(uni::IICDesignWareReg::FastSclLowCount) == 212u;
		fixture->model.hold_enable_status = true;
		fixture->model.registers[_IMM(uni::IICDesignWareReg::EnableStatus) / sizeof(uint32)] = 1;
		const auto disable_timeout_status = fixture->controller.Initialize(100000000u, 400000u, 8u);
		const bool disable_timeout_passed = disable_timeout_status == uni::IICDesignWareStatus::DisableTimeout;
		fixture->model.registers[_IMM(uni::IICDesignWareReg::EnableStatus) / sizeof(uint32)] = 0;
		const auto enable_timeout_status = fixture->controller.Initialize(100000000u, 400000u, 8u);
		const bool enable_timeout_passed = enable_timeout_status == uni::IICDesignWareStatus::EnableTimeout;

		fixture->model.hold_enable_status = false;
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
		const bool route_initialized = fixture->controller.Initialize(100000000u, 400000u, 32u) == uni::IICDesignWareStatus::Success;
		const bool route_bound = route_initialized && SelectDesignWareI2cBusSpeed(bus) == 400000u && BindDesignWareI2cBus(bus, &fixture->controller);
		bool route_passed = false;
		if (route_bound) {
			fixture->command[0] = 0x30u;
			fixture->command[1] = 0xC3u;
			const bool route_write = Devsman::Send(endpoint, fixture->command, 2u) == 2;
			fixture->command[0] = 0x30u;
			const bool route_select = Devsman::Send(endpoint, fixture->command, 1u) == 1;
			fixture->received[0] = 0;
			const bool route_read = Devsman::Read(endpoint, fixture->received, 1u) == 1;
			route_passed = route_write && route_select && route_read && fixture->received[0] == 0xC3u;
			designware_i2c_test_fixture = fixture;
		}

		ploginfo("[I2C-DW-TEST] fast=%s standard=%s apl=%s disable-timeout=%s enable-timeout=%s write=%s restart-read=%s fifo=%s nack=%s transfer-timeout=%s route=%s", fast_initialize_passed ? "success" : "failed", standard_initialize_passed ? "success" : "failed", apollo_lake_initialize_passed ? "success" : "failed", disable_timeout_passed ? "success" : "failed", enable_timeout_passed ? "success" : "failed", write_passed ? "success" : "failed", combined_passed ? "success" : "failed", fifo_passed ? "success" : "failed", nack_passed ? "success" : "failed", transfer_timeout_passed ? "success" : "failed", route_passed ? "success" : "failed");
		if (!route_bound) {
			fixture->~DesignWareI2cTestFixture();
			uni_hostenv_allocator->deallocate(fixture);
		}
	}
}

_ESYM_C void R_INTEL_LPSS_I2C_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_INTEL_LPSS_I2C{
	.init = R_INTEL_LPSS_I2C_INIT,
	.name = "IntelLpssI2c",
};

void R_INTEL_LPSS_I2C_INIT() {
	if (!Devsman::RegisterDriverStarter("intel-lpss-i2c", StartIntelLpssI2c)) {
		plogwarn("[I2C-LPSS] failed to register driver starter");
		return;
	}
	Devsman::StartKnownDrivers();
	LogIntelLpssI2cSummary();
	RunDesignWareI2cInitializationTest();
}
#endif
