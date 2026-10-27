// UTF-8 CPP-ISO11 TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: General Purpose Input Output Device Binding
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"
#include "../../mecocoa/devsman-acpi-gsi.h"
#include <cpp/Device/GPIO>

#if defined(_MCCA) && ((_MCCA & 0xFF00) == 0x8600)

namespace {
	struct DeviceGPIOControllerRuntime {
		DeviceNode* node;
		uni::GeneralPurposeInputOutputPortBackend backend;
		DeviceGPIOLease** claims;
		DeviceGPIOControllerRuntime* next;
		ACPIGSIBinding GSI_binding;
		uni::Atomic<uint32> interrupt_dispatching;
		stduint port_id;
		uint32 GSI;
		bool GSI_ready;
	};

	constexpr stduint DeviceGPIOInterruptCapacity = 32;
	constexpr uint32 DeviceGPIOInterruptEventHandle = 0x4750494Fu;

	struct DeviceGPIOInterruptSlot {
		DeviceGPIOLease lease;
		DeviceGPIOControllerRuntime* controller;
		DeviceGPIOInterruptHandler handler;
		void* context;
		uni::Atomic<uint32> dispatching;
		uni::Atomic<uint32> pending_count;
		uni::Atomic<uint8> active;
		uni::Atomic<uint8> event_queued;
		uni::Atomic<uint8> awaiting_completion;
		DeviceNode* owner;
		stduint resource_index;
		uint32 generation;
		bool waiting_controller;
		bool level_triggered;
	};

	DeviceGPIOControllerRuntime* gpio_controller_runtimes = nullptr;
	DeviceGPIOInterruptSlot GPIO_interrupt_slots[DeviceGPIOInterruptCapacity]{};
	uint32 GPIO_interrupt_generation = 0;
	#if CONFIG_ACPI_BOOT_TEST
	bool GPIO_interrupt_direct_test_delivery = false;
	#endif

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

	bool FindGPIOControllerGSI(const DeviceGPIOControllerRuntime& runtime, ACPIGSIConfiguration& configuration) {
		const auto* resource = Devsman::FindResource(runtime.node, DeviceResourceType::IrqLine, 0);
		uint32 flags = 0;
		uint64 GSI = 0;
		if (resource) {
			GSI = resource->start;
			flags = resource->flags;
		}
		else if (runtime.node && runtime.node->fields.acpi_info && runtime.node->fields.acpi_info->irq_resource_count) {
			const auto& IRQ = runtime.node->fields.acpi_info->irq_resources[0];
			GSI = IRQ.interrupt;
			flags = IRQ.flags;
		}
		else return false;
		if (GSI > ~uint32(0)) return false;
		configuration.gsi = uint32(GSI);
		configuration.level_triggered = (flags & DeviceResourceFlag_IrqLevel) != 0;
		configuration.active_low = (flags & DeviceResourceFlag_IrqActiveLow) != 0;
		configuration.shareable = (flags & DeviceResourceFlag_IrqShareable) != 0;
		return true;
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
		return required;
	}

	bool ConfigureGPIOResource(DeviceGPIOControllerRuntime& runtime, DeviceAcpiGPIOResource& resource) {
		const uint32 required = RequiredGPIOCapabilities(resource);
		if ((runtime.backend.capabilities & required) != required) return false;
		if (resource.type == DeviceAcpiGPIOConnectionType::Interrupt) {
			const auto mode = resource.pull == DeviceAcpiGPIOPull::None ? uni::GPIOMode::IN_Floating : uni::GPIOMode::IN_Pull;
			if (!runtime.backend.configure_pin(runtime.backend.context, resource.pin, mode, uni::GPIOSpeed::Default)) return false;
			if ((resource.pull == DeviceAcpiGPIOPull::Up || resource.pull == DeviceAcpiGPIOPull::Down) && (!runtime.backend.set_pull || !runtime.backend.set_pull(runtime.backend.context, resource.pin, resource.pull == DeviceAcpiGPIOPull::Up))) return false;
			const auto trigger = resource.trigger == DeviceAcpiGPIOTrigger::Edge ? uni::GPIOInterruptTrigger::Edge : uni::GPIOInterruptTrigger::Level;
			uni::GPIOInterruptPolarity polarity = uni::GPIOInterruptPolarity::ActiveHigh;
			if (resource.polarity == DeviceAcpiGPIOPolarity::ActiveLow) polarity = uni::GPIOInterruptPolarity::ActiveLow;
			else if (resource.polarity == DeviceAcpiGPIOPolarity::ActiveBoth) polarity = uni::GPIOInterruptPolarity::ActiveBoth;
			// ACPI Specification 6.5, section 6.4.3.8.1 defines the GPIO interrupt configuration fields passed here.
			// WakeCapable remains firmware metadata when this runtime interrupt backend cannot arm system wake.
			const bool wake_enabled = resource.wake_capable && (runtime.backend.capabilities & uni::GeneralPurposeInputOutputCapability_Wake);
			const uni::GPIOInterruptConfiguration configuration{ trigger, polarity, resource.debounce_timeout, resource.sharing == DeviceAcpiGPIOSharing::Shared, wake_enabled };
			return runtime.backend.configure_interrupt && runtime.backend.configure_interrupt(runtime.backend.context, resource.pin, configuration);
		}
		const auto mode = resource.io_restriction == DeviceAcpiGPIOIORestriction::OutputOnly ? uni::GPIOMode::OUT_PushPull : resource.pull == DeviceAcpiGPIOPull::None ? uni::GPIOMode::IN_Floating : uni::GPIOMode::IN_Pull;
		if (!runtime.backend.configure_pin(runtime.backend.context, resource.pin, mode, uni::GPIOSpeed::Default)) return false;
		if ((resource.pull == DeviceAcpiGPIOPull::Up || resource.pull == DeviceAcpiGPIOPull::Down) && (!runtime.backend.set_pull || !runtime.backend.set_pull(runtime.backend.context, resource.pin, resource.pull == DeviceAcpiGPIOPull::Up))) return false;
		return true;
	}

	bool DispatchGPIOControllerInterrupt(void* context);

	DeviceGPIOInterruptSlot* AllocateGPIOInterruptSlot(stduint& slot_index) {
		for0(index, DeviceGPIOInterruptCapacity) {
			uint8 expected = 0;
			if (!GPIO_interrupt_slots[index].active.compare_exchange(expected, 2, uni::MemoryOrder_Acq_Rel, uni::MemoryOrder_Acquire)) continue;
			slot_index = index;
			return &GPIO_interrupt_slots[index];
		}
		return nullptr;
	}

	bool GPIOControllerHasInterruptConsumers(const DeviceGPIOControllerRuntime& runtime) {
		for0(index, DeviceGPIOInterruptCapacity) {
			const auto& slot = GPIO_interrupt_slots[index];
			if (slot.active.load(uni::MemoryOrder_Acquire) == 1 && slot.controller == &runtime) return true;
		}
		return false;
	}

	DeviceGPIOStatus EnsureGPIOControllerGSI(DeviceGPIOControllerRuntime& runtime) {
		if (runtime.GSI_ready) return DeviceGPIOStatus::Success;
		ACPIGSIConfiguration configuration{};
		if (!FindGPIOControllerGSI(runtime, configuration)) return DeviceGPIOStatus::ResourceUnavailable;
		const auto status = Devs::BindACPIGSI(configuration, DispatchGPIOControllerInterrupt, &runtime, runtime.GSI_binding);
		if (status != ACPIGSIStatus::Success) return status == ACPIGSIStatus::ConfigurationConflict ? DeviceGPIOStatus::ConfigurationConflict : DeviceGPIOStatus::ResourceUnavailable;
		runtime.GSI = configuration.gsi;
		runtime.GSI_ready = true;
		return DeviceGPIOStatus::Success;
	}

	void ReleaseGPIOControllerGSIIfUnused(DeviceGPIOControllerRuntime& runtime) {
		if (!runtime.GSI_ready || GPIOControllerHasInterruptConsumers(runtime)) return;
		const auto status = Devs::UnbindACPIGSI(runtime.GSI_binding);
		if (status != ACPIGSIStatus::Success) {
			plogerro("[GPIO-IRQ] controller=%s unbind-status=%u", runtime.node && runtime.node->fields.acpi_info ? runtime.node->fields.acpi_info->path : "(unknown)", stduint(status));
			return;
		}
		runtime.GSI = 0;
		runtime.GSI_ready = false;
	}

	bool QueueGPIOInterruptEvent(DeviceGPIOInterruptSlot& slot) {
		if (slot.active.load(uni::MemoryOrder_Acquire) != 1) return false;
		slot.pending_count.fetch_add(1, uni::MemoryOrder_Acq_Rel);
		if (slot.event_queued.exchange(1, uni::MemoryOrder_Acq_Rel)) return true;
		DeviceEvent event{};
		event.kind = _IMM(DeviceEventKind::Interrupt);
		event.flags = slot.level_triggered ? DeviceEventFlag_NeedsAck : DeviceEventFlag_None;
		event.device_handle = DeviceGPIOInterruptEventHandle;
		event.source = uint32(&slot - GPIO_interrupt_slots);
		event.count = 1;
		event.generation = slot.generation;
		#if CONFIG_ACPI_BOOT_TEST
		if (GPIO_interrupt_direct_test_delivery) return Devs::DispatchGPIOInterruptEvent(event);
		#endif
		if (device_event_proc(Task_Devsman, event)) return true;
		slot.event_queued.store(0, uni::MemoryOrder_Release);
		return false;
	}

	bool DispatchGPIOControllerInterrupt(void* context) {
		auto* runtime = static_cast<DeviceGPIOControllerRuntime*>(context);
		if (!runtime) return false;
		runtime->interrupt_dispatching.fetch_add(1, uni::MemoryOrder_Acq_Rel);
		bool claimed = false;
		for0(index, DeviceGPIOInterruptCapacity) {
			auto& leader = GPIO_interrupt_slots[index];
			if (leader.active.load(uni::MemoryOrder_Acquire) != 1 || leader.controller != runtime || !leader.lease.active) continue;
			bool duplicate_pin = false;
			for0(previous, index) {
				const auto& candidate = GPIO_interrupt_slots[previous];
				if (candidate.active.load(uni::MemoryOrder_Acquire) == 1 && candidate.controller == runtime && candidate.lease.active && candidate.lease.pin == leader.lease.pin) {
					duplicate_pin = true;
					break;
				}
			}
			if (duplicate_pin || (leader.level_triggered && leader.awaiting_completion.load(uni::MemoryOrder_Acquire))) continue;
			bool pending = false;
			if (!runtime->backend.interrupt_pending(runtime->backend.context, leader.lease.pin, pending)) continue;
			if (!pending) continue;
			if (leader.level_triggered) {
				if (!runtime->backend.enable_interrupt(runtime->backend.context, leader.lease.pin, false)) continue;
			}
			else if (!runtime->backend.acknowledge_interrupt(runtime->backend.context, leader.lease.pin)) continue;
			claimed = true;
			for0(target_index, DeviceGPIOInterruptCapacity) {
				auto& target = GPIO_interrupt_slots[target_index];
				if (target.active.load(uni::MemoryOrder_Acquire) != 1 || target.controller != runtime || !target.lease.active || target.lease.pin != leader.lease.pin) continue;
				if (target.level_triggered) target.awaiting_completion.store(1, uni::MemoryOrder_Release);
				(void)QueueGPIOInterruptEvent(target);
			}
		}
		runtime->interrupt_dispatching.fetch_sub(1, uni::MemoryOrder_Acq_Rel);
		return claimed;
	}

	DeviceGPIOStatus ActivateGPIOInterrupt(DeviceGPIOInterruptSlot& slot, bool require_GSI) {
		const auto claim_status = Devs::ClaimGPIO(slot.owner, slot.resource_index, slot.lease);
		if (claim_status == DeviceGPIOStatus::ControllerUnavailable) {
			slot.waiting_controller = true;
			return claim_status;
		}
		if (claim_status != DeviceGPIOStatus::Success) return claim_status;
		slot.controller = GPIOControllerRuntime(slot.lease.controller);
		if (!slot.controller || !slot.lease.resource || slot.lease.resource->type != DeviceAcpiGPIOConnectionType::Interrupt) {
			Devs::ReleaseGPIO(slot.lease);
			return DeviceGPIOStatus::InvalidArgument;
		}
		slot.level_triggered = slot.lease.resource->trigger == DeviceAcpiGPIOTrigger::Level;
		if (!slot.controller->backend.enable_interrupt(slot.controller->backend.context, slot.lease.pin, false)) {
			Devs::ReleaseGPIO(slot.lease);
			slot.controller = nullptr;
			return DeviceGPIOStatus::ResourceUnavailable;
		}
		#if CONFIG_ACPI_BOOT_TEST
		const bool GSI_was_ready = slot.controller->GSI_ready;
		#endif
		if (require_GSI) {
			const auto route_status = EnsureGPIOControllerGSI(*slot.controller);
			if (route_status != DeviceGPIOStatus::Success) {
				Devs::ReleaseGPIO(slot.lease);
				slot.controller = nullptr;
				return route_status;
			}
		}
		slot.waiting_controller = false;
		slot.active.store(1, uni::MemoryOrder_Release);
		if (!slot.controller->backend.enable_interrupt(slot.controller->backend.context, slot.lease.pin, true)) {
			slot.active.store(2, uni::MemoryOrder_Release);
			Devs::ReleaseGPIO(slot.lease);
			ReleaseGPIOControllerGSIIfUnused(*slot.controller);
			slot.controller = nullptr;
			return DeviceGPIOStatus::ResourceUnavailable;
		}
		#if CONFIG_ACPI_BOOT_TEST
		if (require_GSI && !GSI_was_ready && !GPIO_interrupt_direct_test_delivery) ploginfo("[GPIO-IRQ-SUM] status=success controller=%s pin=%u gsi=%u vector=%u", slot.controller->node && slot.controller->node->fields.acpi_info ? slot.controller->node->fields.acpi_info->path : "(unknown)", slot.lease.pin, slot.controller->GSI, stduint(slot.controller->GSI_binding.vector));
		#endif
		return DeviceGPIOStatus::Success;
	}

	void ActivateWaitingGPIOInterrupts(DeviceNode* controller) {
		for0(index, DeviceGPIOInterruptCapacity) {
			auto& slot = GPIO_interrupt_slots[index];
			if (slot.active.load(uni::MemoryOrder_Acquire) != 2 || !slot.waiting_controller || !slot.owner || !slot.owner->fields.acpi_info || slot.resource_index >= slot.owner->fields.acpi_info->gpio_resource_count) continue;
			if (slot.owner->fields.acpi_info->gpio_resources[slot.resource_index].controller_node != controller) continue;
			const auto status = ActivateGPIOInterrupt(slot, true);
			if (status != DeviceGPIOStatus::Success && status != DeviceGPIOStatus::ControllerUnavailable) plogerro("[GPIO-IRQ] endpoint=%s delayed-status=%u", slot.owner->fields.acpi_info->path, stduint(status));
		}
	}

	void ClearGPIOInterruptSlot(DeviceGPIOInterruptSlot& slot) {
		slot.lease = {};
		slot.controller = nullptr;
		slot.handler = nullptr;
		slot.context = nullptr;
		slot.pending_count.store(0, uni::MemoryOrder_Release);
		slot.event_queued.store(0, uni::MemoryOrder_Release);
		slot.awaiting_completion.store(0, uni::MemoryOrder_Release);
		slot.owner = nullptr;
		slot.resource_index = 0;
		slot.generation = 0;
		slot.waiting_controller = false;
		slot.level_triggered = false;
		slot.active.store(0, uni::MemoryOrder_Release);
	}

	DeviceGPIOStatus RegisterGPIOInterruptInternal(DeviceNode* owner, stduint resource_index, DeviceGPIOInterruptHandler handler, void* context, DeviceGPIOInterruptRegistration& registration, bool require_GSI) {
		if (!owner || !handler || registration.active || !owner->fields.acpi_info || resource_index >= owner->fields.acpi_info->gpio_resource_count || owner->fields.acpi_info->gpio_resources[resource_index].type != DeviceAcpiGPIOConnectionType::Interrupt) return DeviceGPIOStatus::InvalidArgument;
		stduint slot_index = 0;
		auto* slot = AllocateGPIOInterruptSlot(slot_index);
		if (!slot) return DeviceGPIOStatus::ResourceUnavailable;
		slot->handler = handler;
		slot->context = context;
		slot->owner = owner;
		slot->resource_index = resource_index;
		slot->generation = ++GPIO_interrupt_generation;
		if (!slot->generation) slot->generation = ++GPIO_interrupt_generation;
		registration.generation = slot->generation;
		registration.slot = uint16(slot_index);
		registration.active = true;
		const auto status = ActivateGPIOInterrupt(*slot, require_GSI);
		if (status == DeviceGPIOStatus::Success || status == DeviceGPIOStatus::ControllerUnavailable) return DeviceGPIOStatus::Success;
		ClearGPIOInterruptSlot(*slot);
		registration = {};
		return status;
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
		if (!model || pin >= model->pin_count) return false;
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
		const uint32 saved_capabilities = runtime->backend.capabilities;
		runtime->backend.capabilities &= ~uni::GeneralPurposeInputOutputCapability_Wake;
		DeviceGPIOLease runtime_wake_lease{};
		const bool runtime_wake_claimed = Devs::ClaimGPIO(endpoint, interrupt_index, runtime_wake_lease) == DeviceGPIOStatus::Success;
		const bool runtime_wake_passed = runtime_wake_claimed && !model->pins[interrupt_resource.pin].wake_capable;
		if (runtime_wake_lease.active) Devs::ReleaseGPIO(runtime_wake_lease);
		runtime->backend.capabilities = saved_capabilities;

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
		const bool passed = capability_passed && claim_passed && release_passed && runtime_wake_passed && duplicate_passed && range_passed && conflict_passed && input_passed && output_passed && pending_passed && mask_passed && unmask_passed && acknowledge_passed && firmware_passed && waiting_passed && registered;
		MCCA_BOOT_TEST_LOG(passed, "[GPIO-CONTROLLER-TEST] capability=%s claim=%s release=%s runtime-wake=%s duplicate=%s range=%s conflict=%s input=%s output=%s pending=%s mask=%s unmask=%s acknowledge=%s firmware=%s waiting=%s recovery=%s", capability_passed ? "success" : "failed", claim_passed ? "success" : "failed", release_passed ? "success" : "failed", runtime_wake_passed ? "success" : "failed", duplicate_passed ? "success" : "failed", range_passed ? "success" : "failed", conflict_passed ? "success" : "failed", input_passed ? "success" : "failed", output_passed ? "success" : "failed", pending_passed ? "success" : "failed", mask_passed ? "success" : "failed", unmask_passed ? "success" : "failed", acknowledge_passed ? "success" : "failed", firmware_passed ? "success" : "failed", waiting_passed ? "success" : "failed", registered ? "success" : "failed");
	}

	struct GPIOInterruptTestContext {
		GPIOModel* model;
		uint32 pin;
		uint32 count;
		bool level;
		bool order_valid;
	};

	void GPIOInterruptTestHandler(DeviceNode*, stduint, uint32 count, void* context) {
		auto* test = static_cast<GPIOInterruptTestContext*>(context);
		if (!test || !test->model || test->pin >= test->model->pin_count) return;
		test->count += count;
		if (test->level) test->order_valid &= !test->model->pins[test->pin].interrupt_enabled;
		else test->order_valid &= !test->model->pins[test->pin].pending;
	}

	void RunGPIOInterruptRoutingTest() {
		auto* controller = FindAcpiHardwareId(Devsman::Root(), "MCOA0005");
		if (!controller) return;
		auto* level_endpoint = FindAcpiHardwareId(Devsman::Root(), "ALPS0001");
		auto* edge_endpoint = FindAcpiHardwareId(Devsman::Root(), "MCOA0010");
		auto* runtime = GPIOControllerRuntime(controller);
		if (!level_endpoint || !edge_endpoint || !runtime || !level_endpoint->fields.acpi_info || !edge_endpoint->fields.acpi_info) {
			plogerro("[GPIO-IRQ-TEST] setup=failed");
			return;
		}
		stduint level_index = ~stduint(0);
		stduint edge_index = ~stduint(0);
		for0(index, level_endpoint->fields.acpi_info->gpio_resource_count) {
			const auto& resource = level_endpoint->fields.acpi_info->gpio_resources[index];
			if (resource.controller_node == controller && resource.type == DeviceAcpiGPIOConnectionType::Interrupt) {
				level_index = index;
				break;
			}
		}
		for0(index, edge_endpoint->fields.acpi_info->gpio_resource_count) {
			const auto& resource = edge_endpoint->fields.acpi_info->gpio_resources[index];
			if (resource.controller_node == controller && resource.type == DeviceAcpiGPIOConnectionType::Interrupt) {
				edge_index = index;
				break;
			}
		}
		if (level_index == ~stduint(0) || edge_index == ~stduint(0)) {
			plogerro("[GPIO-IRQ-TEST] resource=failed");
			return;
		}
		auto* model = static_cast<GPIOModel*>(runtime->backend.context);
		auto& level_resource = level_endpoint->fields.acpi_info->gpio_resources[level_index];
		auto& edge_resource = edge_endpoint->fields.acpi_info->gpio_resources[edge_index];
		auto* shared_owner = new DeviceNode{};
		auto* shared_info = new DeviceAcpiInfo{};
		auto* shared_resource = new DeviceAcpiGPIOResource{};
		if (!model || !shared_owner || !shared_info || !shared_resource) {
			delete shared_resource;
			delete shared_info;
			delete shared_owner;
			plogerro("[GPIO-IRQ-TEST] allocation=failed");
			return;
		}
		*shared_resource = edge_resource;
		shared_info->gpio_resource_count = 1;
		shared_info->gpio_resources = shared_resource;
		shared_owner->fields.acpi_info = shared_info;
		GPIOInterruptTestContext edge_context{model, edge_resource.pin, 0, false, true};
		GPIOInterruptTestContext shared_context{model, edge_resource.pin, 0, false, true};
		GPIOInterruptTestContext level_context{model, level_resource.pin, 0, true, true};
		DeviceGPIOInterruptRegistration edge_registration{};
		DeviceGPIOInterruptRegistration shared_registration{};
		DeviceGPIOInterruptRegistration level_registration{};
		GPIO_interrupt_direct_test_delivery = true;
		const bool edge_registered = RegisterGPIOInterruptInternal(edge_endpoint, edge_index, GPIOInterruptTestHandler, &edge_context, edge_registration, true) == DeviceGPIOStatus::Success;
		const bool shared_registered = RegisterGPIOInterruptInternal(shared_owner, 0, GPIOInterruptTestHandler, &shared_context, shared_registration, true) == DeviceGPIOStatus::Success;
		model->pins[edge_resource.pin].pending = true;
		const bool edge_claimed = edge_registered && shared_registered && DispatchGPIOControllerInterrupt(runtime);
		const bool edge_passed = edge_claimed && edge_context.count == 1 && edge_context.order_valid;
		const bool shared_passed = edge_claimed && shared_context.count == 1 && shared_context.order_valid;
		const bool spurious_passed = !DispatchGPIOControllerInterrupt(runtime) && edge_context.count == 1 && shared_context.count == 1;
		model->pins[edge_resource.pin].pending = true;
		const bool repeated_passed = DispatchGPIOControllerInterrupt(runtime) && edge_context.count == 2 && shared_context.count == 2;
		const bool level_registered = RegisterGPIOInterruptInternal(level_endpoint, level_index, GPIOInterruptTestHandler, &level_context, level_registration, true) == DeviceGPIOStatus::Success;
		model->pins[level_resource.pin].pending = true;
		const bool level_claimed = level_registered && DispatchGPIOControllerInterrupt(runtime);
		const bool level_masked = level_claimed && level_context.count == 1 && level_context.order_valid && !model->pins[level_resource.pin].interrupt_enabled;
		const bool active_low_passed = model->pins[level_resource.pin].polarity == uni::GPIOInterruptPolarity::ActiveLow;
		const bool completion_passed = level_masked && Devs::CompleteGPIOInterrupt(level_registration) == DeviceGPIOStatus::Success && model->pins[level_resource.pin].interrupt_enabled && !model->pins[level_resource.pin].pending;
		const bool edge_released = edge_registration.active && Devs::UnregisterGPIOInterrupt(edge_registration) == DeviceGPIOStatus::Success;
		const bool shared_released = shared_registration.active && Devs::UnregisterGPIOInterrupt(shared_registration) == DeviceGPIOStatus::Success;
		const bool level_released = level_registration.active && Devs::UnregisterGPIOInterrupt(level_registration) == DeviceGPIOStatus::Success;
		GPIO_interrupt_direct_test_delivery = false;
		const bool release_passed = edge_released && shared_released && level_released;
		const bool passed = edge_passed && level_masked && active_low_passed && shared_passed && spurious_passed && repeated_passed && completion_passed && release_passed;
		MCCA_BOOT_TEST_LOG(passed, "[GPIO-IRQ-TEST] edge=%s level=%s active-low=%s shared=%s spurious=%s repeated=%s completion=%s release=%s", edge_passed ? "success" : "failed", level_masked ? "success" : "failed", active_low_passed ? "success" : "failed", shared_passed ? "success" : "failed", spurious_passed ? "success" : "failed", repeated_passed ? "success" : "failed", completion_passed ? "success" : "failed", release_passed ? "success" : "failed");
		delete shared_resource;
		delete shared_info;
		delete shared_owner;
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
	ActivateWaitingGPIOInterrupts(controller);
	return DeviceGPIOStatus::Success;
}

DeviceGPIOStatus Devs::UnregisterGPIOController(DeviceNode* controller) {
	auto* runtime = GPIOControllerRuntime(controller);
	if (!runtime) return DeviceGPIOStatus::ControllerUnavailable;
	for0(pin, runtime->backend.pin_count) if (runtime->claims[pin]) return DeviceGPIOStatus::AlreadyClaimed;
	if (runtime->GSI_ready) {
		if (Devs::UnbindACPIGSI(runtime->GSI_binding) != ACPIGSIStatus::Success) return DeviceGPIOStatus::ResourceUnavailable;
		runtime->GSI_ready = false;
		runtime->GSI = 0;
	}
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

DeviceGPIOStatus Devs::RegisterGPIOInterrupt(DeviceNode* owner, stduint resource_index, DeviceGPIOInterruptHandler handler, void* context, DeviceGPIOInterruptRegistration& registration) {
	return RegisterGPIOInterruptInternal(owner, resource_index, handler, context, registration, true);
}

DeviceGPIOStatus Devs::CompleteGPIOInterrupt(DeviceGPIOInterruptRegistration& registration) {
	if (!registration.active || registration.slot >= DeviceGPIOInterruptCapacity) return DeviceGPIOStatus::InvalidArgument;
	auto& slot = GPIO_interrupt_slots[registration.slot];
	if (slot.active.load(uni::MemoryOrder_Acquire) != 1 || slot.generation != registration.generation || !slot.level_triggered || !slot.lease.active || !slot.controller) return DeviceGPIOStatus::OwnershipMismatch;
	if (!slot.awaiting_completion.exchange(0, uni::MemoryOrder_Acq_Rel)) return DeviceGPIOStatus::InvalidArgument;
	if ((slot.controller->backend.capabilities & uni::GeneralPurposeInputOutputCapability_Acknowledge) && !slot.controller->backend.acknowledge_interrupt(slot.controller->backend.context, slot.lease.pin)) {
		slot.awaiting_completion.store(1, uni::MemoryOrder_Release);
		return DeviceGPIOStatus::ResourceUnavailable;
	}
	if (!slot.controller->backend.enable_interrupt(slot.controller->backend.context, slot.lease.pin, true)) {
		slot.awaiting_completion.store(1, uni::MemoryOrder_Release);
		return DeviceGPIOStatus::ResourceUnavailable;
	}
	return DeviceGPIOStatus::Success;
}

DeviceGPIOStatus Devs::UnregisterGPIOInterrupt(DeviceGPIOInterruptRegistration& registration) {
	if (!registration.active || registration.slot >= DeviceGPIOInterruptCapacity) return DeviceGPIOStatus::InvalidArgument;
	auto& slot = GPIO_interrupt_slots[registration.slot];
	const uint8 state = slot.active.load(uni::MemoryOrder_Acquire);
	if (!state || slot.generation != registration.generation) return DeviceGPIOStatus::OwnershipMismatch;
	auto* controller = slot.controller;
	const uint32 generation = slot.generation;
	slot.active.store(2, uni::MemoryOrder_Release);
	if (slot.lease.active && controller) {
		bool another_consumer = false;
		for0(index, DeviceGPIOInterruptCapacity) {
			const auto& candidate = GPIO_interrupt_slots[index];
			if (&candidate != &slot && candidate.active.load(uni::MemoryOrder_Acquire) == 1 && candidate.controller == controller && candidate.lease.active && candidate.lease.pin == slot.lease.pin) {
				another_consumer = true;
				break;
			}
		}
		if (!another_consumer) (void)controller->backend.enable_interrupt(controller->backend.context, slot.lease.pin, false);
	}
	device_event_cancel(Task_Devsman, DeviceGPIOInterruptEventHandle, generation);
	if (controller) while (controller->interrupt_dispatching.load(uni::MemoryOrder_Acquire)) asm volatile("pause" ::: "memory");
	while (slot.dispatching.load(uni::MemoryOrder_Acquire)) asm volatile("pause" ::: "memory");
	if (slot.lease.active) {
		const auto release_status = Devs::ReleaseGPIO(slot.lease);
		if (release_status != DeviceGPIOStatus::Success) {
			slot.active.store(state, uni::MemoryOrder_Release);
			return release_status;
		}
	}
	if (controller) ReleaseGPIOControllerGSIIfUnused(*controller);
	ClearGPIOInterruptSlot(slot);
	registration = {};
	return DeviceGPIOStatus::Success;
}

bool Devs::DispatchGPIOInterruptEvent(const DeviceEvent& event) {
	if (event.version != DeviceEventProtocolVersion || DeviceEventKind(event.kind) != DeviceEventKind::Interrupt || event.device_handle != DeviceGPIOInterruptEventHandle || event.source >= DeviceGPIOInterruptCapacity) return false;
	auto& slot = GPIO_interrupt_slots[event.source];
	if (slot.active.load(uni::MemoryOrder_Acquire) != 1 || slot.generation != event.generation) return false;
	slot.dispatching.fetch_add(1, uni::MemoryOrder_Acq_Rel);
	if (slot.active.load(uni::MemoryOrder_Acquire) != 1 || slot.generation != event.generation) {
		slot.dispatching.fetch_sub(1, uni::MemoryOrder_Acq_Rel);
		return false;
	}
	slot.event_queued.store(0, uni::MemoryOrder_Release);
	const uint32 count = slot.pending_count.exchange(0, uni::MemoryOrder_Acq_Rel);
	if (count && slot.handler) slot.handler(slot.owner, slot.resource_index, count, slot.context);
	slot.dispatching.fetch_sub(1, uni::MemoryOrder_Acq_Rel);
	return true;
}

#if CONFIG_ACPI_BOOT_TEST
DeviceGPIOStatus Devs::TriggerGPIOModelInterrupt(DeviceNode* owner, stduint resource_index) {
	if (!owner || !owner->fields.acpi_info || resource_index >= owner->fields.acpi_info->gpio_resource_count) return DeviceGPIOStatus::InvalidArgument;
	const auto& resource = owner->fields.acpi_info->gpio_resources[resource_index];
	if (resource.type != DeviceAcpiGPIOConnectionType::Interrupt || !resource.controller_node) return DeviceGPIOStatus::InvalidArgument;
	auto* runtime = GPIOControllerRuntime(resource.controller_node);
	if (!runtime || runtime->backend.interrupt_pending != GPIOModelInterruptPending) return DeviceGPIOStatus::Unsupported;
	auto* model = static_cast<GPIOModel*>(runtime->backend.context);
	if (!model || resource.pin >= model->pin_count) return DeviceGPIOStatus::OutOfRange;
	model->pins[resource.pin].pending = true;
	return DispatchGPIOControllerInterrupt(runtime) ? DeviceGPIOStatus::Success : DeviceGPIOStatus::ResourceUnavailable;
}

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
	Devs::RunACPIGSITest();
	RunGPIOControllerTest();
	RunGPIOInterruptRoutingTest();
}
#endif

#endif
