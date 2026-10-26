// UTF-8 CPP-ISO11 TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: General Purpose Input Output Device Binding
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"
#include <cpp/Device/GPIO>

#if defined(_MCCA) && ((_MCCA & 0xFF00) == 0x8600)

namespace {
	struct DeviceGPIOControllerRuntime {
		DeviceNode* node;
		uni::GeneralPurposeInputOutputPortBackend backend;
		DeviceGPIOLease** claims;
		DeviceGPIOControllerRuntime* next;
		stduint port_id;
	};

	DeviceGPIOControllerRuntime* gpio_controller_runtimes = nullptr;

	#if CONFIG_ACPI_BOOT_TEST
	struct GPIOModelPin {
		uni::GPIOMode mode;
		uni::GPIOInterruptTrigger trigger;
		uni::GPIOInterruptPolarity polarity;
		Handler_t handler;
		bool value;
		bool pull_up;
		bool interrupt_configured;
		bool interrupt_enabled;
		bool pending;
		bool shared;
		bool wake_capable;
		uint16 debounce_timeout;
	};

	struct GPIOModel {
		GPIOModelPin* pins;
		stduint pin_count;
	};
	#endif

	DeviceGPIOControllerRuntime* GPIOControllerRuntime(DeviceNode* controller) {
		for (auto* runtime = gpio_controller_runtimes; runtime; runtime = runtime->next) if (runtime->node == controller) return runtime;
		return nullptr;
	}

	bool ValidGPIOBackend(const uni::GeneralPurposeInputOutputPortBackend& backend) {
		if (!backend.context || !backend.pin_count || !backend.configure_pin) return false;
		if ((backend.capabilities & uni::GeneralPurposeInputOutputCapability_Input) && !backend.read_pin) return false;
		if ((backend.capabilities & uni::GeneralPurposeInputOutputCapability_Output) && !backend.write_pin) return false;
		if ((backend.capabilities & (uni::GeneralPurposeInputOutputCapability_InterruptEdge | uni::GeneralPurposeInputOutputCapability_InterruptLevel)) && !backend.configure_interrupt) return false;
		if ((backend.capabilities & (uni::GeneralPurposeInputOutputCapability_PullUp | uni::GeneralPurposeInputOutputCapability_PullDown)) && !backend.set_pull) return false;
		if ((backend.capabilities & uni::GeneralPurposeInputOutputCapability_Pending) && !backend.interrupt_pending) return false;
		if ((backend.capabilities & uni::GeneralPurposeInputOutputCapability_Mask) && !backend.enable_interrupt) return false;
		if ((backend.capabilities & uni::GeneralPurposeInputOutputCapability_Acknowledge) && !backend.acknowledge_interrupt) return false;
		return true;
	}

	bool SameGPIOConfiguration(const DeviceAcpiGPIOResource& left, const DeviceAcpiGPIOResource& right) {
		return left.type == right.type && left.trigger == right.trigger && left.polarity == right.polarity && left.pull == right.pull && left.io_restriction == right.io_restriction && left.pin_configuration == right.pin_configuration && left.drive_strength == right.drive_strength && left.debounce_timeout == right.debounce_timeout && left.consumer == right.consumer && left.wake_capable == right.wake_capable;
	}

	void UpdateGPIOBindings(DeviceNode* node, DeviceNode* controller, DeviceGPIOBindingStatus status, stduint port_id) {
		for (auto* current = node; current; current = reinterpret_cast<DeviceNode*>(current->link.next)) {
			if (current->fields.acpi_info) {
				for0(index, current->fields.acpi_info->gpio_resource_count) {
					auto& resource = current->fields.acpi_info->gpio_resources[index];
					if (resource.controller_node != controller) continue;
					if (resource.dependency_status != DeviceAcpiGPIODependencyStatus::NotDeclared && resource.dependency_status != DeviceAcpiGPIODependencyStatus::Satisfied) {
						resource.port_id = 0;
						resource.binding_status = DeviceGPIOBindingStatus::Failed;
						continue;
					}
					resource.port_id = port_id;
					resource.binding_status = status;
				}
			}
			if (current->link.subf) UpdateGPIOBindings(reinterpret_cast<DeviceNode*>(current->link.subf), controller, status, port_id);
		}
	}

	uint32 RequiredGPIOCapabilities(const DeviceAcpiGPIOResource& resource) {
		uint32 required = 0;
		if (resource.type == DeviceAcpiGPIOConnectionType::Interrupt) {
			required |= uni::GeneralPurposeInputOutputCapability_Input | uni::GeneralPurposeInputOutputCapability_Pending | uni::GeneralPurposeInputOutputCapability_Mask;
			required |= resource.trigger == DeviceAcpiGPIOTrigger::Edge ? uni::GeneralPurposeInputOutputCapability_InterruptEdge | uni::GeneralPurposeInputOutputCapability_Acknowledge : uni::GeneralPurposeInputOutputCapability_InterruptLevel;
			switch (resource.polarity) {
			case DeviceAcpiGPIOPolarity::ActiveHigh: required |= uni::GeneralPurposeInputOutputCapability_ActiveHigh; break;
			case DeviceAcpiGPIOPolarity::ActiveLow: required |= uni::GeneralPurposeInputOutputCapability_ActiveLow; break;
			case DeviceAcpiGPIOPolarity::ActiveBoth: required |= uni::GeneralPurposeInputOutputCapability_ActiveBoth; break;
			default: break;
			}
		}
		else {
			switch (resource.io_restriction) {
			case DeviceAcpiGPIOIORestriction::InputOnly: required |= uni::GeneralPurposeInputOutputCapability_Input; break;
			case DeviceAcpiGPIOIORestriction::OutputOnly: required |= uni::GeneralPurposeInputOutputCapability_Output; break;
			default: required |= uni::GeneralPurposeInputOutputCapability_Input | uni::GeneralPurposeInputOutputCapability_Output; break;
			}
		}
		if (resource.pull == DeviceAcpiGPIOPull::Up) required |= uni::GeneralPurposeInputOutputCapability_PullUp;
		else if (resource.pull == DeviceAcpiGPIOPull::Down) required |= uni::GeneralPurposeInputOutputCapability_PullDown;
		if (resource.sharing == DeviceAcpiGPIOSharing::Shared) required |= uni::GeneralPurposeInputOutputCapability_Shared;
		if (resource.wake_capable) required |= uni::GeneralPurposeInputOutputCapability_Wake;
		return required;
	}

	bool ConfigureGPIOResource(DeviceGPIOControllerRuntime& runtime, DeviceAcpiGPIOResource& resource) {
		const uint32 required = RequiredGPIOCapabilities(resource);
		if ((runtime.backend.capabilities & required) != required) return false;
		auto& pin = uni::GPIO[runtime.port_id][resource.pin];
		if (resource.type == DeviceAcpiGPIOConnectionType::Interrupt) {
			pin.setMode(resource.pull == DeviceAcpiGPIOPull::None ? uni::GPIOMode::IN_Floating : uni::GPIOMode::IN_Pull);
			if (resource.pull == DeviceAcpiGPIOPull::Up || resource.pull == DeviceAcpiGPIOPull::Down) pin.setPull(resource.pull == DeviceAcpiGPIOPull::Up);
			const auto trigger = resource.trigger == DeviceAcpiGPIOTrigger::Edge ? uni::GPIOInterruptTrigger::Edge : uni::GPIOInterruptTrigger::Level;
			uni::GPIOInterruptPolarity polarity = uni::GPIOInterruptPolarity::ActiveHigh;
			if (resource.polarity == DeviceAcpiGPIOPolarity::ActiveLow) polarity = uni::GPIOInterruptPolarity::ActiveLow;
			else if (resource.polarity == DeviceAcpiGPIOPolarity::ActiveBoth) polarity = uni::GPIOInterruptPolarity::ActiveBoth;
			// ACPI Specification 6.5, section 6.4.3.8.1 defines the GPIO interrupt configuration fields passed here.
			const uni::GPIOInterruptConfiguration configuration{ trigger, polarity, resource.debounce_timeout, resource.sharing == DeviceAcpiGPIOSharing::Shared, resource.wake_capable };
			pin.ConfigureInterrupt(configuration);
			return true;
		}
		if (resource.io_restriction == DeviceAcpiGPIOIORestriction::OutputOnly) pin.setMode(uni::GPIOMode::OUT_PushPull);
		else pin.setMode(resource.pull == DeviceAcpiGPIOPull::None ? uni::GPIOMode::IN_Floating : uni::GPIOMode::IN_Pull);
		if (resource.pull == DeviceAcpiGPIOPull::Up || resource.pull == DeviceAcpiGPIOPull::Down) pin.setPull(resource.pull == DeviceAcpiGPIOPull::Up);
		return true;
	}

	#if CONFIG_ACPI_BOOT_TEST
	bool GPIOModelReadPin(void* context, stduint pin, bool& value) {
		auto* model = static_cast<GPIOModel*>(context);
		if (!model || pin >= model->pin_count) return false;
		value = model->pins[pin].value;
		return true;
	}

	bool GPIOModelWritePin(void* context, stduint pin, bool value) {
		auto* model = static_cast<GPIOModel*>(context);
		if (!model || pin >= model->pin_count || (model->pins[pin].mode != uni::GPIOMode::OUT_PushPull && model->pins[pin].mode != uni::GPIOMode::OUT_OpenDrain)) return false;
		model->pins[pin].value = value;
		return true;
	}

	bool GPIOModelConfigurePin(void* context, stduint pin, uni::GPIOMode mode, uni::GPIOSpeed) {
		auto* model = static_cast<GPIOModel*>(context);
		if (!model || pin >= model->pin_count) return false;
		model->pins[pin].mode = mode;
		return true;
	}

	bool GPIOModelIsInput(void* context, stduint pin, bool& input) {
		auto* model = static_cast<GPIOModel*>(context);
		if (!model || pin >= model->pin_count) return false;
		input = model->pins[pin].mode == uni::GPIOMode::IN_Floating || model->pins[pin].mode == uni::GPIOMode::IN_Pull;
		return true;
	}

	bool GPIOModelSetPull(void* context, stduint pin, bool pull_up) {
		auto* model = static_cast<GPIOModel*>(context);
		if (!model || pin >= model->pin_count) return false;
		model->pins[pin].pull_up = pull_up;
		return true;
	}

	bool GPIOModelReadPort(void* context, stduint& value) {
		auto* model = static_cast<GPIOModel*>(context);
		if (!model) return false;
		value = 0;
		const stduint count = model->pin_count < sizeof(stduint) * 8 ? model->pin_count : sizeof(stduint) * 8;
		for0(pin, count) if (model->pins[pin].value) value |= _IMM1S(pin);
		return true;
	}

	bool GPIOModelWritePort(void* context, stduint value) {
		auto* model = static_cast<GPIOModel*>(context);
		if (!model) return false;
		const stduint count = model->pin_count < sizeof(stduint) * 8 ? model->pin_count : sizeof(stduint) * 8;
		for0(pin, count) if (model->pins[pin].mode == uni::GPIOMode::OUT_PushPull || model->pins[pin].mode == uni::GPIOMode::OUT_OpenDrain) model->pins[pin].value = (value & _IMM1S(pin)) != 0;
		return true;
	}

	bool GPIOModelModifyPort(void* context, stduint set_mask, stduint reset_mask) {
		auto* model = static_cast<GPIOModel*>(context);
		if (!model || (set_mask & reset_mask)) return false;
		const stduint count = model->pin_count < sizeof(stduint) * 8 ? model->pin_count : sizeof(stduint) * 8;
		for0(pin, count) {
			if (model->pins[pin].mode != uni::GPIOMode::OUT_PushPull && model->pins[pin].mode != uni::GPIOMode::OUT_OpenDrain) continue;
			const stduint mask = _IMM1S(pin);
			if (set_mask & mask) model->pins[pin].value = true;
			else if (reset_mask & mask) model->pins[pin].value = false;
		}
		return true;
	}

	bool GPIOModelConfigureInterrupt(void* context, stduint pin, const uni::GPIOInterruptConfiguration& configuration) {
		auto* model = static_cast<GPIOModel*>(context);
		if (!model || pin >= model->pin_count) return false;
		model->pins[pin].trigger = configuration.trigger;
		model->pins[pin].polarity = configuration.polarity;
		model->pins[pin].shared = configuration.shared;
		model->pins[pin].wake_capable = configuration.wake_capable;
		model->pins[pin].debounce_timeout = configuration.debounce_timeout;
		model->pins[pin].interrupt_configured = true;
		return true;
	}

	bool GPIOModelSetInterrupt(void* context, stduint pin, Handler_t handler) {
		auto* model = static_cast<GPIOModel*>(context);
		if (!model || pin >= model->pin_count) return false;
		model->pins[pin].handler = handler;
		return true;
	}

	bool GPIOModelSetInterruptPriority(void* context, stduint pin, byte, byte) {
		auto* model = static_cast<GPIOModel*>(context);
		return model && pin < model->pin_count;
	}

	bool GPIOModelEnableInterrupt(void* context, stduint pin, bool enable) {
		auto* model = static_cast<GPIOModel*>(context);
		if (!model || pin >= model->pin_count || !model->pins[pin].interrupt_configured) return false;
		model->pins[pin].interrupt_enabled = enable;
		return true;
	}

	bool GPIOModelInterruptPending(void* context, stduint pin, bool& pending) {
		auto* model = static_cast<GPIOModel*>(context);
		if (!model || pin >= model->pin_count) return false;
		pending = model->pins[pin].pending;
		return true;
	}

	bool GPIOModelAcknowledgeInterrupt(void* context, stduint pin) {
		auto* model = static_cast<GPIOModel*>(context);
		if (!model || pin >= model->pin_count || model->pins[pin].trigger != uni::GPIOInterruptTrigger::Edge) return false;
		model->pins[pin].pending = false;
		return true;
	}

	uni::GeneralPurposeInputOutputPortBackend GPIOModelBackend(GPIOModel* model) {
		uni::GeneralPurposeInputOutputPortBackend backend{};
		backend.context = model;
		backend.pin_count = model ? model->pin_count : 0;
		backend.capabilities = uni::GeneralPurposeInputOutputCapability_Input | uni::GeneralPurposeInputOutputCapability_Output | uni::GeneralPurposeInputOutputCapability_PullUp | uni::GeneralPurposeInputOutputCapability_PullDown | uni::GeneralPurposeInputOutputCapability_InterruptEdge | uni::GeneralPurposeInputOutputCapability_InterruptLevel | uni::GeneralPurposeInputOutputCapability_ActiveHigh | uni::GeneralPurposeInputOutputCapability_ActiveLow | uni::GeneralPurposeInputOutputCapability_ActiveBoth | uni::GeneralPurposeInputOutputCapability_Shared | uni::GeneralPurposeInputOutputCapability_Wake | uni::GeneralPurposeInputOutputCapability_Pending | uni::GeneralPurposeInputOutputCapability_Mask | uni::GeneralPurposeInputOutputCapability_Acknowledge;
		backend.read_pin = GPIOModelReadPin;
		backend.write_pin = GPIOModelWritePin;
		backend.configure_pin = GPIOModelConfigurePin;
		backend.is_input = GPIOModelIsInput;
		backend.set_pull = GPIOModelSetPull;
		backend.read_port = GPIOModelReadPort;
		backend.write_port = GPIOModelWritePort;
		backend.modify_port = GPIOModelModifyPort;
		backend.configure_interrupt = GPIOModelConfigureInterrupt;
		backend.set_interrupt = GPIOModelSetInterrupt;
		backend.set_interrupt_priority = GPIOModelSetInterruptPriority;
		backend.enable_interrupt = GPIOModelEnableInterrupt;
		backend.interrupt_pending = GPIOModelInterruptPending;
		backend.acknowledge_interrupt = GPIOModelAcknowledgeInterrupt;
		return backend;
	}

	DeviceNode* FindAcpiHardwareId(DeviceNode* node, const char* hardware_id) {
		for (auto* current = node; current; current = reinterpret_cast<DeviceNode*>(current->link.next)) {
			const auto* info = current->fields.acpi_info;
			if (info && (info->flags & DeviceAcpiInfoFlag_HasHardwareId) && StrCompare(info->hardware_id, hardware_id) == 0) return current;
			if (current->link.subf) {
				auto* found = FindAcpiHardwareId(reinterpret_cast<DeviceNode*>(current->link.subf), hardware_id);
				if (found) return found;
			}
		}
		return nullptr;
	}

	bool StartGPIOModel(DeviceNode* node) {
		if (!node || GPIOControllerRuntime(node)) return node != nullptr;
		auto* model = new GPIOModel{};
		if (!model) return false;
		model->pin_count = 64;
		model->pins = new GPIOModelPin[model->pin_count]{};
		if (!model->pins) {
			delete model;
			return false;
		}
		const auto status = Devs::RegisterGPIOController(node, GPIOModelBackend(model));
		if (status != DeviceGPIOStatus::Success) {
			delete[] model->pins;
			delete model;
			return false;
		}
		return true;
	}

	void RunGPIOControllerTest() {
		auto* controller = FindAcpiHardwareId(Devsman::Root(), "MCOA0005");
		if (!controller) return;
		auto* endpoint = FindAcpiHardwareId(Devsman::Root(), "ALPS0001");
		auto* edge_endpoint = FindAcpiHardwareId(Devsman::Root(), "MCOA0010");
		auto* runtime = GPIOControllerRuntime(controller);
		if (!endpoint || !edge_endpoint || !runtime || !endpoint->fields.acpi_info || !edge_endpoint->fields.acpi_info) {
			plogerro("[GPIO-CONTROLLER-TEST] setup=failed");
			return;
		}
		stduint interrupt_index = ~stduint(0);
		stduint io_index = ~stduint(0);
		for0(index, endpoint->fields.acpi_info->gpio_resource_count) {
			const auto& resource = endpoint->fields.acpi_info->gpio_resources[index];
			if (resource.controller_node != controller) continue;
			if (resource.type == DeviceAcpiGPIOConnectionType::Interrupt && interrupt_index == ~stduint(0)) interrupt_index = index;
			else if (resource.type == DeviceAcpiGPIOConnectionType::IO && io_index == ~stduint(0)) io_index = index;
		}
		if (interrupt_index == ~stduint(0) || io_index == ~stduint(0)) {
			plogerro("[GPIO-CONTROLLER-TEST] endpoint-resource=failed");
			return;
		}
		stduint edge_index = ~stduint(0);
		for0(index, edge_endpoint->fields.acpi_info->gpio_resource_count) {
			const auto& resource = edge_endpoint->fields.acpi_info->gpio_resources[index];
			if (resource.controller_node == controller && resource.type == DeviceAcpiGPIOConnectionType::Interrupt) {
				edge_index = index;
				break;
			}
		}
		if (edge_index == ~stduint(0)) {
			plogerro("[GPIO-CONTROLLER-TEST] edge-resource=failed");
			return;
		}
		auto& interrupt_resource = endpoint->fields.acpi_info->gpio_resources[interrupt_index];
		auto& io_resource = endpoint->fields.acpi_info->gpio_resources[io_index];
		auto& edge_resource = edge_endpoint->fields.acpi_info->gpio_resources[edge_index];
		auto* model = static_cast<GPIOModel*>(runtime->backend.context);

		const uint32 expected_capabilities = uni::GeneralPurposeInputOutputCapability_Input | uni::GeneralPurposeInputOutputCapability_Output | uni::GeneralPurposeInputOutputCapability_InterruptEdge | uni::GeneralPurposeInputOutputCapability_InterruptLevel | uni::GeneralPurposeInputOutputCapability_Pending | uni::GeneralPurposeInputOutputCapability_Mask | uni::GeneralPurposeInputOutputCapability_Acknowledge;
		const bool capability_passed = (uni::GPIO[runtime->port_id].getCapabilities() & expected_capabilities) == expected_capabilities && uni::GPIO[runtime->port_id].getPinCount() == model->pin_count;
		const bool firmware_source_passed = interrupt_resource.trigger == DeviceAcpiGPIOTrigger::Level && interrupt_resource.polarity == DeviceAcpiGPIOPolarity::ActiveLow && interrupt_resource.wake_capable && io_resource.sharing == DeviceAcpiGPIOSharing::Shared && edge_resource.trigger == DeviceAcpiGPIOTrigger::Edge && edge_resource.polarity == DeviceAcpiGPIOPolarity::ActiveHigh && edge_resource.sharing == DeviceAcpiGPIOSharing::Shared;

		DeviceGPIOLease interrupt_lease{};
		const bool claim_passed = Devs::ClaimGPIO(endpoint, interrupt_index, interrupt_lease) == DeviceGPIOStatus::Success && interrupt_lease.active;
		const bool level_configuration_passed = claim_passed && model->pins[interrupt_resource.pin].trigger == uni::GPIOInterruptTrigger::Level && model->pins[interrupt_resource.pin].polarity == uni::GPIOInterruptPolarity::ActiveLow && !model->pins[interrupt_resource.pin].shared && model->pins[interrupt_resource.pin].wake_capable && model->pins[interrupt_resource.pin].debounce_timeout == interrupt_resource.debounce_timeout;
		DeviceGPIOLease duplicate_lease{};
		const bool duplicate_passed = Devs::ClaimGPIO(endpoint, interrupt_index, duplicate_lease) == DeviceGPIOStatus::AlreadyClaimed;
		const bool release_passed = Devs::ReleaseGPIO(interrupt_lease) == DeviceGPIOStatus::Success && !interrupt_lease.active;

		const uint32 saved_pin = io_resource.pin;
		io_resource.pin = uint32(model->pin_count);
		DeviceGPIOLease range_lease{};
		const bool range_passed = Devs::ClaimGPIO(endpoint, io_index, range_lease) == DeviceGPIOStatus::OutOfRange;
		io_resource.pin = saved_pin;

		DeviceGPIOLease io_lease{};
		const bool input_claimed = Devs::ClaimGPIO(endpoint, io_index, io_lease) == DeviceGPIOStatus::Success;
		model->pins[io_resource.pin].value = true;
		const bool input_passed = input_claimed && bool(uni::GPIO[runtime->port_id][io_resource.pin]);
		auto* other_owner = new DeviceNode{};
		auto* other_info = new DeviceAcpiInfo{};
		auto* other_resource = new DeviceAcpiGPIOResource{};
		if (!other_owner || !other_info || !other_resource) {
			plogerro("[GPIO-CONTROLLER-TEST] allocation=failed");
			delete other_owner;
			delete other_info;
			delete other_resource;
			Devs::ReleaseGPIO(io_lease);
			return;
		}
		*other_resource = io_resource;
		other_resource->io_restriction = DeviceAcpiGPIOIORestriction::OutputOnly;
		other_info->gpio_resource_count = 1;
		other_info->gpio_resources = other_resource;
		other_owner->fields.acpi_info = other_info;
		DeviceGPIOLease conflict_lease{};
		const auto conflict_status = input_claimed ? Devs::ClaimGPIO(other_owner, 0, conflict_lease) : DeviceGPIOStatus::InvalidArgument;
		const bool conflict_passed = conflict_status == DeviceGPIOStatus::ConfigurationConflict;
		if (conflict_lease.active) Devs::ReleaseGPIO(conflict_lease);
		delete other_resource;
		delete other_info;
		delete other_owner;
		const auto saved_restriction = io_resource.io_restriction;
		Devs::ReleaseGPIO(io_lease);
		io_resource.io_restriction = DeviceAcpiGPIOIORestriction::OutputOnly;
		const bool output_claimed = Devs::ClaimGPIO(endpoint, io_index, io_lease) == DeviceGPIOStatus::Success;
		uni::GPIO[runtime->port_id][io_resource.pin] = true;
		const bool output_passed = output_claimed && model->pins[io_resource.pin].value;
		Devs::ReleaseGPIO(io_lease);
		io_resource.io_restriction = saved_restriction;

		DeviceGPIOLease edge_lease{};
		const bool edge_claimed = Devs::ClaimGPIO(edge_endpoint, edge_index, edge_lease) == DeviceGPIOStatus::Success;
		auto& interrupt_pin = uni::GPIO[runtime->port_id][edge_resource.pin];
		model->pins[edge_resource.pin].pending = true;
		const bool pending_passed = edge_claimed && interrupt_pin.InterruptPending();
		interrupt_pin.enInterrupt(false);
		const bool mask_passed = !model->pins[edge_resource.pin].interrupt_enabled;
		interrupt_pin.enInterrupt(true);
		const bool unmask_passed = model->pins[edge_resource.pin].interrupt_enabled;
		const bool acknowledge_passed = interrupt_pin.AcknowledgeInterrupt() && !model->pins[edge_resource.pin].pending;
		const bool edge_configuration_passed = edge_claimed && model->pins[edge_resource.pin].trigger == uni::GPIOInterruptTrigger::Edge && model->pins[edge_resource.pin].polarity == uni::GPIOInterruptPolarity::ActiveHigh && model->pins[edge_resource.pin].shared && !model->pins[edge_resource.pin].wake_capable && model->pins[edge_resource.pin].debounce_timeout == edge_resource.debounce_timeout;
		Devs::ReleaseGPIO(edge_lease);

		const bool unregistered = Devs::UnregisterGPIOController(controller) == DeviceGPIOStatus::Success;
		const bool waiting_passed = unregistered && interrupt_resource.binding_status == DeviceGPIOBindingStatus::WaitingController && io_resource.binding_status == DeviceGPIOBindingStatus::WaitingController && edge_resource.binding_status == DeviceGPIOBindingStatus::WaitingController;
		const bool registered = Devs::RegisterGPIOController(controller, GPIOModelBackend(model)) == DeviceGPIOStatus::Success;

		const bool firmware_passed = firmware_source_passed && level_configuration_passed && edge_configuration_passed;
		ploginfo("[GPIO-CONTROLLER-TEST] capability=%s claim=%s release=%s duplicate=%s range=%s conflict=%s input=%s output=%s pending=%s mask=%s unmask=%s acknowledge=%s firmware=%s waiting=%s recovery=%s", capability_passed ? "success" : "failed", claim_passed ? "success" : "failed", release_passed ? "success" : "failed", duplicate_passed ? "success" : "failed", range_passed ? "success" : "failed", conflict_passed ? "success" : "failed", input_passed ? "success" : "failed", output_passed ? "success" : "failed", pending_passed ? "success" : "failed", mask_passed ? "success" : "failed", unmask_passed ? "success" : "failed", acknowledge_passed ? "success" : "failed", firmware_passed ? "success" : "failed", waiting_passed ? "success" : "failed", registered ? "success" : "failed");
	}
	#endif
}

DeviceGPIOStatus Devs::RegisterGPIOController(DeviceNode* controller, const uni::GeneralPurposeInputOutputPortBackend& backend) {
	if (!controller || !ValidGPIOBackend(backend)) return DeviceGPIOStatus::InvalidArgument;
	if (GPIOControllerRuntime(controller)) return DeviceGPIOStatus::AlreadyClaimed;
	auto* runtime = new DeviceGPIOControllerRuntime{};
	if (!runtime) return DeviceGPIOStatus::ResourceUnavailable;
	runtime->claims = new DeviceGPIOLease*[backend.pin_count]{};
	if (!runtime->claims) {
		delete runtime;
		return DeviceGPIOStatus::ResourceUnavailable;
	}
	runtime->node = controller;
	runtime->backend = backend;
	if (!uni::GPIO.Attach(runtime->backend, runtime->port_id)) {
		delete[] runtime->claims;
		delete runtime;
		return DeviceGPIOStatus::ResourceUnavailable;
	}
	runtime->next = gpio_controller_runtimes;
	gpio_controller_runtimes = runtime;
	UpdateGPIOBindings(Devsman::Root(), controller, DeviceGPIOBindingStatus::Ready, runtime->port_id);
	return DeviceGPIOStatus::Success;
}

DeviceGPIOStatus Devs::UnregisterGPIOController(DeviceNode* controller) {
	auto* runtime = GPIOControllerRuntime(controller);
	if (!runtime) return DeviceGPIOStatus::ControllerUnavailable;
	for0(pin, runtime->backend.pin_count) if (runtime->claims[pin]) return DeviceGPIOStatus::AlreadyClaimed;
	if (!uni::GPIO.Detach(runtime->port_id, runtime->backend.context)) return DeviceGPIOStatus::ControllerUnavailable;
	UpdateGPIOBindings(Devsman::Root(), controller, DeviceGPIOBindingStatus::WaitingController, 0);
	auto** cursor = &gpio_controller_runtimes;
	while (*cursor && *cursor != runtime) cursor = &(*cursor)->next;
	if (*cursor) *cursor = runtime->next;
	delete[] runtime->claims;
	delete runtime;
	return DeviceGPIOStatus::Success;
}

DeviceGPIOStatus Devs::ClaimGPIO(DeviceNode* owner, stduint resource_index, DeviceGPIOLease& lease) {
	if (!owner || lease.active || !owner->fields.acpi_info || resource_index >= owner->fields.acpi_info->gpio_resource_count) return DeviceGPIOStatus::InvalidArgument;
	auto& resource = owner->fields.acpi_info->gpio_resources[resource_index];
	if (resource.status != DeviceAcpiGPIOResourceStatus::Success || (resource.dependency_status != DeviceAcpiGPIODependencyStatus::NotDeclared && resource.dependency_status != DeviceAcpiGPIODependencyStatus::Satisfied)) return DeviceGPIOStatus::ResourceUnavailable;
	auto* runtime = GPIOControllerRuntime(resource.controller_node);
	if (!runtime || resource.binding_status == DeviceGPIOBindingStatus::WaitingController) return DeviceGPIOStatus::ControllerUnavailable;
	if (resource.pin >= runtime->backend.pin_count) return DeviceGPIOStatus::OutOfRange;
	if ((runtime->backend.capabilities & RequiredGPIOCapabilities(resource)) != RequiredGPIOCapabilities(resource)) return DeviceGPIOStatus::Unsupported;
	for (auto* current = runtime->claims[resource.pin]; current; current = current->next) {
		if (current->owner == owner && current->resource == &resource) return DeviceGPIOStatus::AlreadyClaimed;
		if (!current->shared || resource.sharing != DeviceAcpiGPIOSharing::Shared) return DeviceGPIOStatus::ConfigurationConflict;
		if (!SameGPIOConfiguration(*current->resource, resource)) return DeviceGPIOStatus::ConfigurationConflict;
	}
	if (!runtime->claims[resource.pin] && !ConfigureGPIOResource(*runtime, resource)) return DeviceGPIOStatus::Unsupported;
	lease.owner = owner;
	lease.controller = resource.controller_node;
	lease.resource = &resource;
	lease.next = runtime->claims[resource.pin];
	lease.port_id = runtime->port_id;
	lease.pin = resource.pin;
	lease.shared = resource.sharing == DeviceAcpiGPIOSharing::Shared;
	lease.active = true;
	runtime->claims[resource.pin] = &lease;
	resource.port_id = runtime->port_id;
	resource.binding_status = DeviceGPIOBindingStatus::Claimed;
	return DeviceGPIOStatus::Success;
}

DeviceGPIOStatus Devs::ReleaseGPIO(DeviceGPIOLease& lease) {
	if (!lease.active || !lease.controller || !lease.resource) return DeviceGPIOStatus::InvalidArgument;
	auto* runtime = GPIOControllerRuntime(lease.controller);
	if (!runtime || lease.pin >= runtime->backend.pin_count) return DeviceGPIOStatus::ControllerUnavailable;
	auto** cursor = runtime->claims + lease.pin;
	while (*cursor && *cursor != &lease) cursor = &(*cursor)->next;
	if (!*cursor) return DeviceGPIOStatus::OwnershipMismatch;
	*cursor = lease.next;
	lease.resource->binding_status = DeviceGPIOBindingStatus::Ready;
	lease = {};
	return DeviceGPIOStatus::Success;
}

#if CONFIG_ACPI_BOOT_TEST
_ESYM_C void R_GPIO_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_GPIO{
	.init = R_GPIO_INIT,
	.name = "GPIO",
};

void R_GPIO_INIT() {
	if (!Devsman::RegisterDriverStarter("gpio-model", StartGPIOModel)) {
		plogerro("[GPIO] failed to register model driver starter");
		return;
	}
	Devsman::BindKnownDrivers();
	Devsman::StartKnownDrivers();
	RunGPIOControllerTest();
}
#endif

#endif
