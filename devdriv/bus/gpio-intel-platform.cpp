// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: Intel ACPI GPIO Community Driver
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"

#if defined(_MCCA) && ((_MCCA & 0xFF00) == 0x8600)
#include "../../../unisym/lib/cpp/Device/GPIO/GPIO-Intel.hpp"

namespace {
	constexpr uint32 IntelGPIOResourceRequirements = DeviceControllerRequirement_Access | DeviceControllerRequirement_Interrupt | DeviceControllerRequirement_Namespace | DeviceControllerRequirement_Identity;
	constexpr uint32 IntelGPIOCapabilities = uni::GeneralPurposeInputOutputCapability_Input | uni::GeneralPurposeInputOutputCapability_Output | uni::GeneralPurposeInputOutputCapability_PullUp | uni::GeneralPurposeInputOutputCapability_PullDown | uni::GeneralPurposeInputOutputCapability_InterruptEdge | uni::GeneralPurposeInputOutputCapability_InterruptLevel | uni::GeneralPurposeInputOutputCapability_ActiveHigh | uni::GeneralPurposeInputOutputCapability_ActiveLow | uni::GeneralPurposeInputOutputCapability_ActiveBoth | uni::GeneralPurposeInputOutputCapability_Pending | uni::GeneralPurposeInputOutputCapability_Mask | uni::GeneralPurposeInputOutputCapability_Acknowledge;

	enum class IntelGPIOFailure : uint8 {
		None,
		CompatibilityUnavailable,
		ResourceContract,
		MissingMMIO,
		AddressRange,
		Mapping,
		Allocation,
		Probe,
		Registration,
	};

	struct IntelGPIOInstance {
		DeviceNode* node;
		const DeviceGPIOControllerCompatibilityDescription* compatibility;
		uni::IntelGPIOCommunity community;
		Spinlock lock;
		stduint base;
		stduint length;
		uni::IntelGPIOStatus last_status;
		uint32 firmware_pin;
		uni::IntelGPIOPinState firmware_pin_state;
		bool firmware_pin_found;

		IntelGPIOInstance(DeviceNode* controller, const DeviceGPIOControllerCompatibilityDescription* description, stduint mapped_base, stduint mapped_length) : node(controller), compatibility(description), base(mapped_base), length(mapped_length), last_status(uni::IntelGPIOStatus::Success), firmware_pin(0), firmware_pin_state{}, firmware_pin_found(false) {}
	};

	#if CONFIG_ACPI_BOOT_TEST
	stduint intel_GPIO_attempted = 0;
	stduint intel_GPIO_started = 0;
	stduint intel_GPIO_failed = 0;
	IntelGPIOFailure first_intel_GPIO_failure = IntelGPIOFailure::None;
	DeviceNode* first_intel_GPIO_failure_node = nullptr;
	IntelGPIOInstance* first_intel_GPIO_instance = nullptr;
	#endif

	const char* IntelGPIOFailureName(IntelGPIOFailure failure) {
		switch (failure) {
		case IntelGPIOFailure::CompatibilityUnavailable: return "compatibility-unavailable";
		case IntelGPIOFailure::ResourceContract: return "resource-contract";
		case IntelGPIOFailure::MissingMMIO: return "missing-mmio";
		case IntelGPIOFailure::AddressRange: return "address-range";
		case IntelGPIOFailure::Mapping: return "mapping";
		case IntelGPIOFailure::Allocation: return "allocation";
		case IntelGPIOFailure::Probe: return "probe";
		case IntelGPIOFailure::Registration: return "registration";
		default: return "none";
		}
	}

	const char* IntelGPIOStatusName(uni::IntelGPIOStatus status) {
		switch (status) {
		case uni::IntelGPIOStatus::Success: return "success";
		case uni::IntelGPIOStatus::InvalidArgument: return "invalid-argument";
		case uni::IntelGPIOStatus::InvalidLayout: return "invalid-layout";
		case uni::IntelGPIOStatus::AccessFailure: return "access-failure";
		case uni::IntelGPIOStatus::OutOfRange: return "out-of-range";
		case uni::IntelGPIOStatus::NotHostOwned: return "not-host-owned";
		case uni::IntelGPIOStatus::ConfigurationLocked: return "configuration-locked";
		case uni::IntelGPIOStatus::OutputLocked: return "output-locked";
		case uni::IntelGPIOStatus::FirmwareOwned: return "firmware-owned";
		case uni::IntelGPIOStatus::Unsupported: return "unsupported";
		default: return "unknown";
		}
	}

	void TrackIntelGPIOFailure(DeviceNode* node, IntelGPIOFailure failure) {
		#if CONFIG_ACPI_BOOT_TEST
		++intel_GPIO_failed;
		if (first_intel_GPIO_failure == IntelGPIOFailure::None) {
			first_intel_GPIO_failure = failure;
			first_intel_GPIO_failure_node = node;
		}
		#endif
		if (node) node->fields.binding.probe_result = -static_cast<int32>(failure);
	}

	void RecordIntelGPIOFailure(DeviceNode* node, IntelGPIOFailure failure, const DeviceControllerResourceContract* contract = nullptr, const DeviceGPIOControllerCompatibilityDescription* compatibility = nullptr) {
		TrackIntelGPIOFailure(node, failure);
		if (failure == IntelGPIOFailure::MissingMMIO && contract && compatibility) plogerro("[GPIO-INTEL] controller=%s status=%s access=%u index=%u expected-index=%u length=%[64H] minimum=%[32H]", node && node->fields.acpi_info ? node->fields.acpi_info->path : "(unknown)", IntelGPIOFailureName(failure), stduint(contract->access_type), contract->access_index, stduint(compatibility->access_index), contract->access_length, compatibility->minimum_access_length);
		else plogerro("[GPIO-INTEL] controller=%s status=%s", node && node->fields.acpi_info ? node->fields.acpi_info->path : "(unknown)", IntelGPIOFailureName(failure));
	}

	bool EnsureIntelGPIOMapping(stduint base, stduint length) {
		#if CONFIG_ENABLE_MMU
		if (!base || !length || length > ~stduint(0) - base || base + length > ~stduint(0) - (PAGE_SIZE - 1)) return false;
		const stduint begin = base & ~stduint(PAGE_SIZE - 1);
		const stduint end = (base + length + PAGE_SIZE - 1) & ~stduint(PAGE_SIZE - 1);
		if (end <= begin) return false;
		for (stduint address = begin; address < end; address += PAGE_SIZE) {
			pageint page = address;
			PageEntry* entry = kernel_paging.getEntry(page);
			if (_IMM(entry) == ~_IMM0 && !Memory::MapPages(kernel_paging, address, address, PAGE_SIZE, PAGESIZE_4KB, PGPROP_present | PGPROP_writable | PGTYPE_device | PGOPTS_omit_warnings)) return false;
			if (_IMM(kernel_paging[address]) != address) return false;
			entry = kernel_paging.getEntry(page);
			stduint properties;
			if (_IMM(entry) == ~_IMM0 || !entry->DecodeLeafProperties(page.crt_level, properties) || !(properties & PGPROP_writable)) return false;
			const stduint type = properties & PGPROP_MASK_TYPE;
			if (type != PGTYPE_device && type != PGTYPE_uncacheable) return false;
		}
		#else
		(void)base;
		(void)length;
		#endif
		return true;
	}

	bool ReadIntelGPIORegister(void* context, uint32 offset, uint32& value) {
		auto* instance = static_cast<IntelGPIOInstance*>(context);
		if (!instance || offset > instance->length || sizeof(uint32) > instance->length - offset) return false;
		value = *reinterpret_cast<volatile uint32*>(instance->base + offset);
		return true;
	}

	bool WriteIntelGPIORegister(void* context, uint32 offset, uint32 value) {
		auto* instance = static_cast<IntelGPIOInstance*>(context);
		if (!instance || offset > instance->length || sizeof(uint32) > instance->length - offset) return false;
		*reinterpret_cast<volatile uint32*>(instance->base + offset) = value;
		return true;
	}

	void RecordIntelGPIOProbeFailure(DeviceNode* node, IntelGPIOInstance* instance, const uni::IntelGPIOCommunityDescription& layout, const char* phase, uint32 pin, bool pin_valid) {
		TrackIntelGPIOFailure(node, IntelGPIOFailure::Probe);
		uint32 revision_register = 0;
		uint32 pad_base_register = 0;
		if (instance) {
			ReadIntelGPIORegister(instance, layout.revision_offset, revision_register);
			ReadIntelGPIORegister(instance, layout.pad_base_offset, pad_base_register);
		}
		const uint64 last_pad_end = uint64(pad_base_register) + (layout.pin_count ? uint64(layout.pin_count - 1) * layout.pad_configuration_stride + sizeof(uint32) : 0u);
		plogerro("[GPIO-INTEL] controller=%s status=probe phase=%s core=%s base=%[x] len=%[x] rev=%[32H] pad=%[32H] end=%[64H] pins=%u stride=%u pin=%u/%u", node && node->fields.acpi_info ? node->fields.acpi_info->path : "(unknown)", phase, instance ? IntelGPIOStatusName(instance->last_status) : "unavailable", instance ? instance->base : 0, instance ? instance->length : 0, revision_register, pad_base_register, last_pad_end, stduint(layout.pin_count), stduint(layout.pad_configuration_stride), stduint(pin_valid), pin);
	}

	bool IntelGPIOReadPin(void* context, stduint pin, bool& value) {
		auto* instance = static_cast<IntelGPIOInstance*>(context);
		if (!instance) return false;
		SpinlockLocal guard(&instance->lock);
		instance->last_status = instance->community.ReadPin(pin, value);
		return instance->last_status == uni::IntelGPIOStatus::Success;
	}

	bool IntelGPIOWritePin(void* context, stduint pin, bool value) {
		auto* instance = static_cast<IntelGPIOInstance*>(context);
		if (!instance) return false;
		SpinlockLocal guard(&instance->lock);
		instance->last_status = instance->community.WritePin(pin, value);
		return instance->last_status == uni::IntelGPIOStatus::Success;
	}

	bool IntelGPIOConfigurePin(void* context, stduint pin, uni::GPIOMode mode, uni::GPIOSpeed) {
		auto* instance = static_cast<IntelGPIOInstance*>(context);
		if (!instance) return false;
		SpinlockLocal guard(&instance->lock);
		instance->last_status = instance->community.ConfigurePin(pin, mode);
		return instance->last_status == uni::IntelGPIOStatus::Success;
	}

	bool IntelGPIOIsInput(void* context, stduint pin, bool& input) {
		auto* instance = static_cast<IntelGPIOInstance*>(context);
		if (!instance) return false;
		SpinlockLocal guard(&instance->lock);
		instance->last_status = instance->community.IsInput(pin, input);
		return instance->last_status == uni::IntelGPIOStatus::Success;
	}

	bool IntelGPIOSetPull(void* context, stduint pin, bool pull_up) {
		auto* instance = static_cast<IntelGPIOInstance*>(context);
		if (!instance) return false;
		SpinlockLocal guard(&instance->lock);
		instance->last_status = instance->community.SetPull(pin, pull_up);
		return instance->last_status == uni::IntelGPIOStatus::Success;
	}

	bool IntelGPIOConfigureInterrupt(void* context, stduint pin, const uni::GPIOInterruptConfiguration& configuration) {
		auto* instance = static_cast<IntelGPIOInstance*>(context);
		if (!instance) return false;
		SpinlockLocal guard(&instance->lock);
		instance->last_status = instance->community.ConfigureInterrupt(pin, configuration);
		return instance->last_status == uni::IntelGPIOStatus::Success;
	}

	bool IntelGPIOEnableInterrupt(void* context, stduint pin, bool enable) {
		auto* instance = static_cast<IntelGPIOInstance*>(context);
		if (!instance) return false;
		SpinlockLocal guard(&instance->lock);
		instance->last_status = instance->community.EnableInterrupt(pin, enable);
		return instance->last_status == uni::IntelGPIOStatus::Success;
	}

	bool IntelGPIOInterruptPending(void* context, stduint pin, bool& pending) {
		auto* instance = static_cast<IntelGPIOInstance*>(context);
		if (!instance) return false;
		SpinlockLocal guard(&instance->lock);
		instance->last_status = instance->community.InterruptPending(pin, pending);
		return instance->last_status == uni::IntelGPIOStatus::Success;
	}

	bool IntelGPIOAcknowledgeInterrupt(void* context, stduint pin) {
		auto* instance = static_cast<IntelGPIOInstance*>(context);
		if (!instance) return false;
		SpinlockLocal guard(&instance->lock);
		instance->last_status = instance->community.AcknowledgeInterrupt(pin);
		return instance->last_status == uni::IntelGPIOStatus::Success;
	}

	uni::GeneralPurposeInputOutputPortBackend IntelGPIOBackend(IntelGPIOInstance* instance) {
		uni::GeneralPurposeInputOutputPortBackend backend{};
		backend.context = instance;
		backend.pin_count = instance ? instance->community.PinCount() : 0;
		backend.capabilities = IntelGPIOCapabilities;
		backend.read_pin = IntelGPIOReadPin;
		backend.write_pin = IntelGPIOWritePin;
		backend.configure_pin = IntelGPIOConfigurePin;
		backend.is_input = IntelGPIOIsInput;
		backend.set_pull = IntelGPIOSetPull;
		backend.configure_interrupt = IntelGPIOConfigureInterrupt;
		backend.enable_interrupt = IntelGPIOEnableInterrupt;
		backend.interrupt_pending = IntelGPIOInterruptPending;
		backend.acknowledge_interrupt = IntelGPIOAcknowledgeInterrupt;
		return backend;
	}

	DeviceAcpiGPIOResource* FindIntelGPIOFirmwarePin(DeviceNode* root, DeviceNode* controller) {
		for (auto* node = root; node;) {
			if (node->fields.acpi_info) {
				for0(index, node->fields.acpi_info->gpio_resource_count) {
					auto& resource = node->fields.acpi_info->gpio_resources[index];
					if (resource.status == DeviceAcpiGPIOResourceStatus::Success && resource.controller_node == controller) return &resource;
				}
			}
			if (node->link.subf) {
				node = reinterpret_cast<DeviceNode*>(node->link.subf);
				continue;
			}
			while (node != root && !node->link.next) node = reinterpret_cast<DeviceNode*>(node->link.getParent());
			if (node == root) break;
			node = reinterpret_cast<DeviceNode*>(node->link.next);
		}
		return nullptr;
	}

	bool StartIntelGPIOCommunity(DeviceNode* node) {
		#if CONFIG_ACPI_BOOT_TEST
		++intel_GPIO_attempted;
		#endif
		const auto* compatibility = Devsman::FindGPIOControllerCompatibility(node);
		if (!compatibility || !compatibility->register_layout) {
			RecordIntelGPIOFailure(node, IntelGPIOFailure::CompatibilityUnavailable);
			return false;
		}
		auto* contract = Devsman::EnsureControllerResourceContract(node);
		if (!contract) {
			RecordIntelGPIOFailure(node, IntelGPIOFailure::Allocation);
			return false;
		}
		contract->requirements = IntelGPIOResourceRequirements;
		contract->status = DeviceControllerContractStatus::NotEvaluated;
		if (Devsman::ValidateControllerResourceContract(*contract) != DeviceControllerContractStatus::Ready) {
			RecordIntelGPIOFailure(node, IntelGPIOFailure::ResourceContract);
			return false;
		}
		if (contract->access_type != DeviceControllerAccessType::MMIO || contract->access_index != compatibility->access_index || contract->access_length < compatibility->minimum_access_length) {
			RecordIntelGPIOFailure(node, IntelGPIOFailure::MissingMMIO, contract, compatibility);
			return false;
		}
		if (!contract->access_base || contract->access_base > uint64(~stduint(0)) || contract->access_length > uint64(~uint32(0)) || contract->access_length > uint64(~stduint(0)) - contract->access_base) {
			RecordIntelGPIOFailure(node, IntelGPIOFailure::AddressRange);
			return false;
		}
		const stduint base = stduint(contract->access_base);
		const stduint length = stduint(contract->access_length);
		if (!EnsureIntelGPIOMapping(base, length)) {
			RecordIntelGPIOFailure(node, IntelGPIOFailure::Mapping);
			return false;
		}
		auto* instance = new IntelGPIOInstance(node, compatibility, base, length);
		if (!instance) {
			RecordIntelGPIOFailure(node, IntelGPIOFailure::Allocation);
			return false;
		}
		const auto* register_layout = static_cast<const uni::IntelGPIOCommunityDescription*>(compatibility->register_layout);
		instance->last_status = instance->community.Initialize(*register_layout, instance, ReadIntelGPIORegister, WriteIntelGPIORegister, uint32(length));
		if (instance->last_status != uni::IntelGPIOStatus::Success) {
			RecordIntelGPIOProbeFailure(node, instance, *register_layout, "init", 0, false);
			delete instance;
			return false;
		}
		if (auto* resource = FindIntelGPIOFirmwarePin(Devsman::Root(), node)) {
			instance->firmware_pin = resource->pin;
			instance->last_status = instance->community.InspectPin(resource->pin, instance->firmware_pin_state);
			if (instance->last_status != uni::IntelGPIOStatus::Success) {
				RecordIntelGPIOProbeFailure(node, instance, *register_layout, "pin", resource->pin, true);
				delete instance;
				return false;
			}
			instance->firmware_pin_found = true;
		}
		const auto backend = IntelGPIOBackend(instance);
		if (Devs::RegisterGPIOController(node, backend) != DeviceGPIOStatus::Success) {
			delete instance;
			RecordIntelGPIOFailure(node, IntelGPIOFailure::Registration);
			return false;
		}
		#if CONFIG_ACPI_BOOT_TEST
		if (!first_intel_GPIO_instance) first_intel_GPIO_instance = instance;
		++intel_GPIO_started;
		#endif
		node->fields.binding.driver_data = instance;
		return true;
	}

	#if CONFIG_ACPI_BOOT_TEST
	struct IntelGPIOTestModel {
		uint32* registers;
		stduint register_count;
		stduint read_count;
		stduint write_count;
	};

	bool ReadIntelGPIOTestRegister(void* context, uint32 offset, uint32& value) {
		auto* model = static_cast<IntelGPIOTestModel*>(context);
		if (!model || offset % sizeof(uint32) || offset / sizeof(uint32) >= model->register_count) return false;
		value = model->registers[offset / sizeof(uint32)];
		++model->read_count;
		return true;
	}

	bool WriteIntelGPIOTestRegister(void* context, uint32 offset, uint32 value) {
		auto* model = static_cast<IntelGPIOTestModel*>(context);
		if (!model || offset % sizeof(uint32) || offset / sizeof(uint32) >= model->register_count) return false;
		if (offset >= 0x100u && offset < 0x110u) model->registers[offset / sizeof(uint32)] &= ~value;
		else model->registers[offset / sizeof(uint32)] = value;
		++model->write_count;
		return true;
	}

	void RunIntelGPIOTest() {
		constexpr stduint RegisterBytes = 0x1000u;
		constexpr stduint TestPin = 20u;
		constexpr uni::IntelGPIOCommunityDescription TestLayout{32u, 32u, 4u, 8u, 0x114u, 0x000u, 0x00Cu, 0x020u, 0x060u, 0x080u, 0x100u, 0x110u};
		auto* model = new IntelGPIOTestModel{};
		if (!model) {
			plogerro("[GPIO-INTEL-TEST] allocation failed");
			return;
		}
		model->register_count = RegisterBytes / sizeof(uint32);
		model->registers = new uint32[model->register_count]{};
		if (!model->registers) {
			delete model;
			plogerro("[GPIO-INTEL-TEST] allocation failed");
			return;
		}
		model->registers[TestLayout.revision_offset / sizeof(uint32)] = 0x1234u << 16;
		model->registers[TestLayout.pad_base_offset / sizeof(uint32)] = 0x200u;
		model->registers[TestLayout.host_software_ownership_offset / sizeof(uint32)] = 1u << TestPin;
		model->registers[(0x200u + TestPin * 8u) / sizeof(uint32)] = 1u << 1;
		auto* compatibility_node = new DeviceNode{};
		auto* compatibility_acpi = new DeviceAcpiInfo{};
		bool compatibility_passed = false;
		if (compatibility_node && compatibility_acpi) {
			compatibility_node->fields.node_type = uint16(DeviceNodeType::PlatformDevice);
			compatibility_node->fields.acpi_info = compatibility_acpi;
			compatibility_acpi->flags = DeviceAcpiInfoFlag_HasHardwareId | DeviceAcpiInfoFlag_HasUniqueId;
			MemCopyN(compatibility_acpi->hardware_id, "INT3452", 8);
			MemCopyN(compatibility_acpi->unique_id, "1", 2);
			const auto* hardware_match = Devsman::FindGPIOControllerCompatibility(compatibility_node);
			compatibility_acpi->flags = DeviceAcpiInfoFlag_HasCompatibleId | DeviceAcpiInfoFlag_HasUniqueId;
			MemCopyN(compatibility_acpi->compatible_ids, "INT3452", 8);
			MemCopyN(compatibility_acpi->unique_id, "2", 2);
			const auto* compatible_match = Devsman::FindGPIOControllerCompatibility(compatibility_node);
			compatibility_acpi->unique_id[0] = '5';
			const auto* rejected_match = Devsman::FindGPIOControllerCompatibility(compatibility_node);
			const auto* hardware_layout = hardware_match ? static_cast<const uni::IntelGPIOCommunityDescription*>(hardware_match->register_layout) : nullptr;
			const auto* compatible_layout = compatible_match ? static_cast<const uni::IntelGPIOCommunityDescription*>(compatible_match->register_layout) : nullptr;
			compatibility_passed = hardware_layout && hardware_layout->pin_count == 78u && hardware_layout->minimum_access_length == 0x11Cu && hardware_match->minimum_access_length == hardware_layout->minimum_access_length && compatible_layout && compatible_layout->pin_count == 77u && compatible_layout->minimum_access_length == 0x11Cu && compatible_match->minimum_access_length == compatible_layout->minimum_access_length && !rejected_match;
		}

		uni::IntelGPIOCommunity community;
		const bool probe_passed = community.Initialize(TestLayout, model, ReadIntelGPIOTestRegister, WriteIntelGPIOTestRegister, RegisterBytes) == uni::IntelGPIOStatus::Success && community.Revision() == 0x1234u && community.PadBase() == 0x200u;
		uni::IntelGPIOCommunity boundary_community;
		uni::IntelGPIOPinState boundary_state{};
		constexpr uint32 BoundaryLength = 0x200u + (TestLayout.pin_count - 1u) * TestLayout.pad_configuration_stride + sizeof(uint32);
		const bool boundary_passed = boundary_community.Initialize(TestLayout, model, ReadIntelGPIOTestRegister, WriteIntelGPIOTestRegister, BoundaryLength) == uni::IntelGPIOStatus::Success && boundary_community.InspectPin(TestLayout.pin_count - 1u, boundary_state) == uni::IntelGPIOStatus::AccessFailure;
		const bool read_only_passed = model->write_count == 0;
		bool input_value = false;
		const bool input_passed = community.ReadPin(TestPin, input_value) == uni::IntelGPIOStatus::Success && input_value;
		const bool output_passed = community.ConfigurePin(TestPin, uni::GPIOMode::OUT_PushPull) == uni::IntelGPIOStatus::Success && community.WritePin(TestPin, true) == uni::IntelGPIOStatus::Success;
		const bool pull_passed = community.SetPull(TestPin, true) == uni::IntelGPIOStatus::Success;
		const uni::GPIOInterruptConfiguration interrupt_configuration{uni::GPIOInterruptTrigger::Edge, uni::GPIOInterruptPolarity::ActiveLow, 0, false, false};
		const bool interrupt_passed = community.ConfigureInterrupt(TestPin, interrupt_configuration) == uni::IntelGPIOStatus::Success;
		const bool mask_passed = community.EnableInterrupt(TestPin, true) == uni::IntelGPIOStatus::Success && (model->registers[TestLayout.interrupt_enable_offset / sizeof(uint32)] & (1u << TestPin));
		model->registers[TestLayout.interrupt_status_offset / sizeof(uint32)] |= 1u << TestPin;
		bool pending = false;
		const bool pending_passed = community.InterruptPending(TestPin, pending) == uni::IntelGPIOStatus::Success && pending;
		const bool acknowledge_passed = community.AcknowledgeInterrupt(TestPin) == uni::IntelGPIOStatus::Success && !(model->registers[TestLayout.interrupt_status_offset / sizeof(uint32)] & (1u << TestPin));
		model->registers[(TestLayout.pad_ownership_offset + 8u) / sizeof(uint32)] = 1u << 16;
		const bool ownership_passed = community.ReadPin(TestPin, input_value) == uni::IntelGPIOStatus::NotHostOwned;
		model->registers[(TestLayout.pad_ownership_offset + 8u) / sizeof(uint32)] = 0;
		model->registers[TestLayout.pad_configuration_lock_offset / sizeof(uint32)] = 1u << TestPin;
		const bool lock_passed = community.ConfigurePin(TestPin, uni::GPIOMode::IN_Floating) == uni::IntelGPIOStatus::ConfigurationLocked;
		const bool range_passed = community.ReadPin(TestLayout.pin_count, input_value) == uni::IntelGPIOStatus::OutOfRange;

		ploginfo("[GPIO-INTEL-TEST] compat=%s probe=%s boundary=%s read-only=%s input=%s output=%s pull=%s interrupt=%s mask=%s pending=%s acknowledge=%s ownership=%s lock=%s range=%s", compatibility_passed ? "success" : "failed", probe_passed ? "success" : "failed", boundary_passed ? "success" : "failed", read_only_passed ? "success" : "failed", input_passed ? "success" : "failed", output_passed ? "success" : "failed", pull_passed ? "success" : "failed", interrupt_passed ? "success" : "failed", mask_passed ? "success" : "failed", pending_passed ? "success" : "failed", acknowledge_passed ? "success" : "failed", ownership_passed ? "success" : "failed", lock_passed ? "success" : "failed", range_passed ? "success" : "failed");
		delete compatibility_acpi;
		delete compatibility_node;
		delete[] model->registers;
		delete model;
	}

	void LogIntelGPIOSummary() {
		if (first_intel_GPIO_instance) {
			const auto* instance = first_intel_GPIO_instance;
			printlog(intel_GPIO_failed ? _LOG_WARN : _LOG_INFO, "[GPIO-INTEL-SUM] attempted=%u started=%u failed=%u example=%s uid=%s base=%[x] length=%[x] revision=%[32H] pad-base=%[32H] firmware-pin=%s pin=%u host-owned=%u locked=%u pending=%u", intel_GPIO_attempted, intel_GPIO_started, intel_GPIO_failed, instance->node->fields.acpi_info ? instance->node->fields.acpi_info->path : "(unknown)", instance->node->fields.acpi_info ? instance->node->fields.acpi_info->unique_id : "", instance->base, instance->length, instance->community.Revision(), instance->community.PadBase(), instance->firmware_pin_found ? "yes" : "no", instance->firmware_pin, instance->firmware_pin_state.host_owned, instance->firmware_pin_state.configuration_locked, instance->firmware_pin_state.interrupt_pending);
		}
		else printlog(intel_GPIO_failed ? _LOG_WARN : _LOG_INFO, "[GPIO-INTEL-SUM] attempted=%u started=%u failed=%u failure=%s example=%s", intel_GPIO_attempted, intel_GPIO_started, intel_GPIO_failed, IntelGPIOFailureName(first_intel_GPIO_failure), first_intel_GPIO_failure_node && first_intel_GPIO_failure_node->fields.acpi_info ? first_intel_GPIO_failure_node->fields.acpi_info->path : "none");
	}
	#endif
}

_ESYM_C void R_INTEL_GPIO_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_INTEL_GPIO{
	.init = R_INTEL_GPIO_INIT,
	.name = "IntelGPIO",
};

void R_INTEL_GPIO_INIT() {
	if (!Devsman::RegisterDriverStarter("intel-gpio-community", StartIntelGPIOCommunity)) {
		plogerro("[GPIO-INTEL] failed to register driver starter");
		return;
	}
	Devsman::BindKnownDrivers();
	Devsman::StartKnownDrivers();
	#if CONFIG_ACPI_BOOT_TEST
	RunIntelGPIOTest();
	LogIntelGPIOSummary();
	#endif
}

#endif
