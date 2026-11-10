// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// #pragma GCC optimize("O2")
#include "../include/mecocoa.hpp"

#define PRINT_GPIO_DETAIL 0 // STM32

#if (_MCCA & 0xFF00) == 0x8600 || defined(_MCU_STM32H7x)
#include <cpp/Device/GPIO>
#endif

#if (_MCCA & 0xFF00) == 0x8600
#include <cpp/Device/Bus/Serial/IIC-HID.hpp>
#include <cpp/Device/IIC>
#endif

static rostr text_device_node_type(uint16 node_type) {
	switch (DeviceNodeType(node_type)) {
	case DeviceNodeType::SystemRoot: return "system-root";
	case DeviceNodeType::BusRoot:    return "bus-root";
	case DeviceNodeType::PCI_Root:   return "pci-root";
	case DeviceNodeType::PciBus:     return "pci-bus";
	case DeviceNodeType::PciDevice:  return "pci-dev";
	case DeviceNodeType::IsaBus:     return "isa-bus";
	case DeviceNodeType::StorageDevice: return "storage-dev";
	case DeviceNodeType::UsbBus:     return "usb-bus";
	case DeviceNodeType::UsbRootHub: return "usb-root-hub";
	case DeviceNodeType::UsbPort:    return "usb-port";
	case DeviceNodeType::UsbDevice:  return "usb-dev";
	case DeviceNodeType::UsbInterface: return "usb-if";
	case DeviceNodeType::PlatformDevice: return "platform-dev";
	case DeviceNodeType::SerioController: return "serio-ctl";
	case DeviceNodeType::SerioDevice: return "serio-dev";
	case DeviceNodeType::I2cBus: return "i2c-bus";
	case DeviceNodeType::I2cDevice: return "i2c-dev";
	case DeviceNodeType::SpiBus: return "spi-bus";
	case DeviceNodeType::SpiDevice: return "spi-dev";
	case DeviceNodeType::UartBus: return "uart-bus";
	case DeviceNodeType::UartDevice: return "uart-dev";
	default: return "unknown";
	}
}

static rostr text_device_bus_type(uint16 bus_type) {
	switch (DeviceBusType(bus_type)) {
	case DeviceBusType::PCI: return "pci";
	case DeviceBusType::ISA: return "isa";
	case DeviceBusType::USB: return "usb";
	case DeviceBusType::Platform: return "platform";
	case DeviceBusType::I2C: return "i2c";
	case DeviceBusType::SPI: return "spi";
	case DeviceBusType::Serio: return "serio";
	case DeviceBusType::Virtio: return "virtio";
	case DeviceBusType::UART: return "uart";
	default: return "none";
	}
}

static rostr text_driver_binding_state(uint32 state) {
	switch (DriverBindingState(state)) {
	case DriverBindingState::None: return "none";
	case DriverBindingState::Matched: return "matched";
	case DriverBindingState::Probed: return "probed";
	case DriverBindingState::Started: return "started";
	case DriverBindingState::Failed: return "failed";
	default: return "?";
	}
}

struct DriverMatchSummary {
	stduint matched = 0;
	stduint unsupported = 0;
	stduint missing_resource = 0;
	stduint ambiguous = 0;
	stduint rejected_by_capability = 0;
};

static void collect_driver_match_summary(const DeviceNode* node, DriverMatchSummary& summary) {
	for (auto* current = node; current; current = reinterpret_cast<const DeviceNode*>(current->link.next)) {
		switch (current->fields.binding.match_result) {
		case DriverMatchResult::Matched: ++summary.matched; break;
		case DriverMatchResult::Unsupported: ++summary.unsupported; break;
		case DriverMatchResult::MissingResource: ++summary.missing_resource; break;
		case DriverMatchResult::Ambiguous: ++summary.ambiguous; break;
		case DriverMatchResult::RejectedByCapability: ++summary.rejected_by_capability; break;
		default: break;
		}
		if (current->link.subf) collect_driver_match_summary(reinterpret_cast<const DeviceNode*>(current->link.subf), summary);
	}
}

static rostr text_device_resource_type(uint16 type) {
	switch (DeviceResourceType(type)) {
	case DeviceResourceType::PciBarMmio:        return "BAR-MMIO";
	case DeviceResourceType::PciBarIo:          return "BAR-IO";
	case DeviceResourceType::IoPortRange:       return "IOPORT";
	case DeviceResourceType::IrqLine:           return "IRQ";
	case DeviceResourceType::PciBridgeBusRange: return "BUS-RANGE";
	case DeviceResourceType::UsbLocation:       return "USB-LOC";
	case DeviceResourceType::UsbEndpoint:       return "USB-EP";
	case DeviceResourceType::DmaChannel:        return "DMA";
	case DeviceResourceType::MemoryRange:       return "MMIO";
	case DeviceResourceType::GpioPin:           return "GPIN";
	case DeviceResourceType::BusAddress:        return "BUS-ADDR";
	case DeviceResourceType::Configuration:     return "CONFIG";
	default: return "RES";
	}
}


static rostr text_usb_endpoint_transfer_type(stduint type) {
	switch (type) {
	case 0: return "control";
	case 1: return "iso";
	case 2: return "bulk";
	case 3: return "interrupt";
	default: return "?";
	}
}

static void dump_device_tree_indent(OstreamTrait& com1, stduint depth);

static void dump_device_tree_driver_match(OstreamTrait& com1, const DeviceNode& node, stduint depth) {
	const auto result = node.fields.binding.match_result;
	if (result != DriverMatchResult::MissingResource && result != DriverMatchResult::Ambiguous && result != DriverMatchResult::RejectedByCapability) return;
	dump_device_tree_indent(com1, depth);
	com1.OutFormat("- MATCH result=%s priority=%u candidates=%u", Devsman::DriverMatchResultName(result), (stduint)node.fields.binding.match_priority, (stduint)node.fields.binding.match_candidate_count);
	if (result == DriverMatchResult::MissingResource) com1.OutFormat(" missing=%s", text_device_resource_type(node.fields.binding.missing_resource_type));
	com1.OutFormat("\n\r");
}

static rostr text_controller_resource_source(DeviceControllerResourceSource source) {
	switch (source) {
	case DeviceControllerResourceSource::AcpiResource: return "acpi-resource";
	case DeviceControllerResourceSource::AcpiDsd: return "acpi-dsd";
	case DeviceControllerResourceSource::AcpiDsm: return "acpi-dsm";
	case DeviceControllerResourceSource::PciConfiguration: return "pci-config";
	case DeviceControllerResourceSource::PlatformDescription: return "platform-description";
	case DeviceControllerResourceSource::HardwareCapability: return "hardware-capability";
	case DeviceControllerResourceSource::FirmwareConfigured: return "firmware-configured";
	case DeviceControllerResourceSource::DriverPolicy: return "driver-policy";
	default: return "none";
	}
}

static rostr text_controller_contract_status(DeviceControllerContractStatus status) {
	switch (status) {
	case DeviceControllerContractStatus::NotEvaluated: return "not-evaluated";
	case DeviceControllerContractStatus::Ready: return "ready";
	case DeviceControllerContractStatus::MissingAccess: return "missing-access";
	case DeviceControllerContractStatus::MissingInterrupt: return "missing-interrupt";
	case DeviceControllerContractStatus::MissingClock: return "missing-clock";
	case DeviceControllerContractStatus::MissingReset: return "missing-reset";
	case DeviceControllerContractStatus::MissingPower: return "missing-power";
	case DeviceControllerContractStatus::MissingNamespace: return "missing-namespace";
	case DeviceControllerContractStatus::MissingIdentity: return "missing-identity";
	case DeviceControllerContractStatus::MissingDma: return "missing-dma";
	default: return "unknown";
	}
}

static rostr text_controller_access_type(DeviceControllerAccessType type) {
	switch (type) {
	case DeviceControllerAccessType::MMIO: return "mmio";
	case DeviceControllerAccessType::PortIo: return "port-io";
	default: return "none";
	}
}

static rostr text_controller_interrupt_mode(DeviceControllerInterruptMode mode) {
	switch (mode) {
	case DeviceControllerInterruptMode::IRQ: return "irq";
	case DeviceControllerInterruptMode::Polling: return "polling";
	default: return "none";
	}
}

static rostr text_controller_reset_type(DeviceControllerResetType type) {
	switch (type) {
	case DeviceControllerResetType::Register: return "register";
	case DeviceControllerResetType::FirmwareConfigured: return "firmware-configured";
	default: return "none";
	}
}

static rostr text_controller_power_state(DeviceControllerPowerState state) {
	switch (state) {
	case DeviceControllerPowerState::On: return "on";
	case DeviceControllerPowerState::FirmwareConfigured: return "firmware-configured";
	default: return "unknown";
	}
}

static rostr text_controller_dma_mode(DeviceControllerDmaMode mode) {
	switch (mode) {
	case DeviceControllerDmaMode::NotRequired: return "not-required";
	case DeviceControllerDmaMode::Supported: return "supported";
	case DeviceControllerDmaMode::Required: return "required";
	default: return "unknown";
	}
}

static void dump_device_tree_controller_resources(OstreamTrait& com1, const DeviceNode& node, stduint depth) {
	const auto* contract = node.fields.controller_resources;
	if (!contract) return;
	dump_device_tree_indent(com1, depth);
	com1.OutFormat("- CONTRACT status=%s access=%s:%[64H]+%[64H]/%s interrupt=%s:%u/%s clock=%u/%s\n\r", text_controller_contract_status(contract->status), text_controller_access_type(contract->access_type), contract->access_base, contract->access_length, text_controller_resource_source(contract->access_source), text_controller_interrupt_mode(contract->interrupt_mode), (stduint)contract->interrupt, text_controller_resource_source(contract->interrupt_source), contract->input_clock_hz, text_controller_resource_source(contract->clock_source));
	dump_device_tree_indent(com1, depth);
	com1.OutFormat("- CONTRACT reset=%s:%[32H]/%[32H]/%[32H]/%s power=%s/%s", text_controller_reset_type(contract->reset_type), contract->reset_register_offset, contract->reset_mask, contract->reset_deassert_value, text_controller_resource_source(contract->reset_source), text_controller_power_state(contract->power_state), text_controller_resource_source(contract->power_source));
	if (contract->identity_type == DeviceControllerIdentityType::Pci) com1.OutFormat(" identity=pci:%04x:%02x:%02x.%x/%s", (stduint)contract->pci_segment, (stduint)contract->pci_bus, (stduint)contract->pci_device, (stduint)contract->pci_function, text_controller_resource_source(contract->identity_source));
	else if (contract->identity_type == DeviceControllerIdentityType::Platform) com1.OutFormat(" identity=platform:%s/%s", contract->platform_identity ? contract->platform_identity : "none", text_controller_resource_source(contract->identity_source));
	else com1.OutFormat(" identity=none");
	com1.OutFormat(" acpi=%s dma=%s/%s\n\r", contract->acpi_path ? contract->acpi_path : "none", text_controller_dma_mode(contract->dma_mode), text_controller_resource_source(contract->dma_source));
}

static void dump_device_tree_pci_description(OstreamTrait& com1, const DeviceNode& node, stduint depth) {
	if (DeviceNodeType(node.fields.node_type) != DeviceNodeType::PciDevice) return;
	#if (_MCCA & 0xFF00) == 0x8600
	const rostr class_name = Devsman::LookupPciClassName(
		node.fields.class_base, node.fields.class_sub, node.fields.class_if);
	const rostr vendor_name = node.fields.text_manufacturer ? node.fields.text_manufacturer :
		Devsman::LookupPciVendorName(node.fields.vendor_id);
	rostr product_name = node.fields.text_product;
	if (!product_name || !product_name[0]) {
		product_name = Devsman::LookupPciDeviceName(
			node.fields.vendor_id, node.fields.device_id, node.fields.class_base, node.fields.class_sub);
	}
	if ((!class_name || !class_name[0]) && (!product_name || !product_name[0])) return;
	dump_device_tree_indent(com1, depth);
	com1.OutFormat("- ");
	if (vendor_name && vendor_name[0]) {
		com1.OutFormat("[%s] ", vendor_name);
	}
	if (class_name && class_name[0]) {
		com1.OutFormat("%s", class_name);
		if (product_name && product_name[0]) {
			com1.OutFormat(": %s", product_name);
		}
		com1.OutFormat("\n\r");
		return;
	}
	com1.OutFormat("%s\n\r", product_name);
	#endif
}

static void dump_device_tree_resources(OstreamTrait& com1, const DeviceNode& node, stduint depth) {
	for0(i, node.fields.resource_count) {
		const auto& res = node.fields.resources[i];
		dump_device_tree_indent(com1, depth);
		switch (DeviceResourceType(res.type)) {
		case DeviceResourceType::MemoryRange:
			com1.OutFormat("- MMIO[%[u]] base=0x%[64H] len=0x%[64H]\n\r",
				res.index, res.start, res.length);
			break;
		case DeviceResourceType::GpioPin: {
			const uint32 port = uint32(res.start >> 32);
			com1.OutFormat("- GPIO[%[u]] %s ", res.index, Devsman::GetResourceName(res));
			if (port >= 'A' && port <= 'Z')
				com1.OutFormat("P%c%u", (int)port, (unsigned)uint32(res.start));
			else com1.OutFormat("port=%u pin=%u", (unsigned)port, (unsigned)uint32(res.start));
			if (res.length != DevicePinNoAlternateFunction)
				com1.OutFormat(" AF=%[u]", (stduint)res.length);
			com1.OutFormat("\n\r");
			break;
		}
		case DeviceResourceType::BusAddress:
			com1.OutFormat("- BUS-ADDR[%[u]] address=0x%[64H]", res.index, res.start);
			if (res.length) com1.OutFormat(" bits=%[u]", (stduint)res.length);
			com1.OutFormat("\n\r");
			break;
		case DeviceResourceType::Configuration:
			com1.OutFormat("- CONFIG[%[u]] %s=", res.index, Devsman::GetResourceName(res));
			if (res.start <= uint64(~stduint(0))) com1.OutFormat("%[u]", (stduint)res.start);
			else com1.OutFormat("0x%[64H]", res.start);
			com1.OutFormat("\n\r");
			break;
		case DeviceResourceType::PciBarMmio:
			com1.OutFormat("- %s[%[u]] base=%p flags=%04X\n\r",
				text_device_resource_type(res.type), res.index, (void*)res.start, (unsigned)res.flags);
			break;
		case DeviceResourceType::PciBarIo:
			com1.OutFormat("- %s[%[u]] base=%p\n\r",
				text_device_resource_type(res.type), res.index, (void*)res.start);
			break;
		case DeviceResourceType::IoPortRange:
			com1.OutFormat("- %s[%[u]] base=%p len=%p\n\r",
				text_device_resource_type(res.type), res.index, (void*)res.start, (void*)res.length);
			break;
		case DeviceResourceType::IrqLine:
			com1.OutFormat("- %s line=%[u] pin=%[u]\n\r",
				text_device_resource_type(res.type), (stduint)res.start, (stduint)res.extra);
			break;
		case DeviceResourceType::PciBridgeBusRange:
			com1.OutFormat("- %s primary=%[u] secondary=%[u] subordinate=%[u]\n\r",
				text_device_resource_type(res.type), (stduint)res.start, (stduint)res.length, (stduint)res.extra);
			break;
		case DeviceResourceType::UsbLocation:
			com1.OutFormat("- %s port=%[u] slot=%[u]\n\r",
				text_device_resource_type(res.type), (stduint)res.start, (stduint)res.extra);
			break;
		case DeviceResourceType::UsbEndpoint:
			com1.OutFormat("- %s[%[u]] addr=%[u] type=%s mps=%[u] interval=%[u]\n\r",
				text_device_resource_type(res.type), res.index,
				(stduint)res.start,
				text_usb_endpoint_transfer_type(res.extra & 0xFFu),
				(stduint)res.length,
				(stduint)((res.extra >> 8) & 0xFFu));
			break;
		case DeviceResourceType::DmaChannel:
			com1.OutFormat("- %s[%[u]] channel=%[u] width=%[u]\n\r",
				text_device_resource_type(res.type), res.index,
				(stduint)res.start, (stduint)res.extra);
			break;
		default:
			com1.OutFormat("- %s[%[u]] start=%p len=%p extra=%p\n\r",
				text_device_resource_type(res.type), res.index, (void*)res.start, (void*)res.length, (void*)res.extra);
			break;
		}
	}
}


// ==== ==== ==== ==== IIC ==== ==== ==== ==== //

#if (_MCCA & 0xFF00) == 0x8600
static rostr text_intel_lpss_i2c_failure(int32 result) {
	switch (-result) {
	case 1: return "missing-mmio";
	case 2: return "address-range";
	case 3: return "caps-unavailable";
	case 4: return "wrong-controller-type";
	case 5: return "allocation";
	case 6: return "component-signature";
	case 7: return "missing-clock";
	case 8: return "initialization";
	case 9: return "resource-contract";
	case 10: return "compatibility-unavailable";
	default: return "unknown";
	}
}

static void dump_device_tree_intel_lpss_i2c(OstreamTrait& com1, const DeviceNode& node, stduint depth) {
	if (!node.fields.binding.driver_name || StrCompare(node.fields.binding.driver_name, "intel-lpss-i2c") != 0) return;
	if (DriverBindingState(node.fields.binding.state) == DriverBindingState::Failed) {
		dump_device_tree_indent(com1, depth);
		com1.OutFormat("- LPSS-I2C failure=%s result=%d\n\r", text_intel_lpss_i2c_failure(node.fields.binding.probe_result), node.fields.binding.probe_result);
		return;
	}
	if (DriverBindingState(node.fields.binding.state) != DriverBindingState::Started || !node.fields.binding.driver_data) return;
	auto* controller = static_cast<uni::IIC_DESIGNWARE*>(node.fields.binding.driver_data);
	uni::IICDesignWareCapabilities capabilities{};
	if (!controller->Probe(capabilities)) {
		dump_device_tree_indent(com1, depth);
		com1.OutFormat("- LPSS-I2C probe=failed component=%[32H]\n\r", capabilities.component_type);
		return;
	}
	const stduint controller_mmio = controller->getAddress();
	if (!controller_mmio) {
		dump_device_tree_indent(com1, depth);
		com1.OutFormat("- LPSS-I2C backend=callback dw-version=%[32H] tx-fifo=%u rx-fifo=%u\n\r", capabilities.component_version, (stduint)capabilities.transmit_fifo_depth, (stduint)capabilities.receive_fifo_depth);
		return;
	}
	const auto* compatibility = Devsman::FindControllerCompatibility(&node);
	const auto* resources = node.fields.controller_resources;
	if (!compatibility || !resources || !resources->access_base) return;
	const stduint mmio = stduint(resources->access_base);
	const uint32 lpss_reset = *reinterpret_cast<volatile uint32*>(mmio + compatibility->reset_register_offset);
	const uint32 lpss_capabilities = *reinterpret_cast<volatile uint32*>(mmio + compatibility->capabilities_register_offset);
	dump_device_tree_indent(com1, depth);
	com1.OutFormat("- LPSS-I2C mmio=%p caps=%[32H] reset=%[32H] dw-version=%[32H] tx-fifo=%u rx-fifo=%u\n\r", (void*)mmio, lpss_capabilities, lpss_reset, capabilities.component_version, (stduint)capabilities.transmit_fifo_depth, (stduint)capabilities.receive_fifo_depth);
}
#else
static void dump_device_tree_intel_lpss_i2c(OstreamTrait&, const DeviceNode&, stduint) {}
#endif

static void dump_device_tree_indent(OstreamTrait& com1, stduint depth) {
	for0(i, depth) com1.OutFormat("  ");
}

// ==== ==== ==== ==== GPIO ==== ==== ==== ==== //
static rostr text_acpi_GPIO_type(DeviceAcpiGPIOConnectionType type) {
	return type == DeviceAcpiGPIOConnectionType::Interrupt ? "interrupt" : "io";
}

static rostr text_acpi_GPIO_status(DeviceAcpiGPIOResourceStatus status) {
	switch (status) {
	case DeviceAcpiGPIOResourceStatus::Success: return "success";
	case DeviceAcpiGPIOResourceStatus::MalformedDescriptor: return "malformed-descriptor";
	case DeviceAcpiGPIOResourceStatus::UnsupportedRevision: return "unsupported-revision";
	case DeviceAcpiGPIOResourceStatus::UnsupportedConnectionType: return "unsupported-type";
	case DeviceAcpiGPIOResourceStatus::ReservedValue: return "reserved-value";
	case DeviceAcpiGPIOResourceStatus::InvalidPinTable: return "invalid-pin-table";
	case DeviceAcpiGPIOResourceStatus::InvalidResourceSource: return "invalid-source";
	case DeviceAcpiGPIOResourceStatus::InvalidVendorData: return "invalid-vendor-data";
	case DeviceAcpiGPIOResourceStatus::DuplicatePin: return "duplicate-pin";
	case DeviceAcpiGPIOResourceStatus::DuplicateResource: return "duplicate-resource";
	case DeviceAcpiGPIOResourceStatus::MalformedResourceSource: return "malformed-source";
	case DeviceAcpiGPIOResourceStatus::ControllerNotFound: return "controller-not-found";
	case DeviceAcpiGPIOResourceStatus::ControllerNotDevice: return "controller-not-device";
	default: return "unknown";
	}
}

static rostr text_acpi_GPIO_dependency(DeviceAcpiGPIODependencyStatus status) {
	switch (status) {
	case DeviceAcpiGPIODependencyStatus::NotDeclared: return "not-declared";
	case DeviceAcpiGPIODependencyStatus::Satisfied: return "satisfied";
	case DeviceAcpiGPIODependencyStatus::ControllerNotListed: return "controller-not-listed";
	case DeviceAcpiGPIODependencyStatus::ControllerUnavailable: return "controller-unavailable";
	case DeviceAcpiGPIODependencyStatus::Malformed: return "malformed";
	case DeviceAcpiGPIODependencyStatus::EvaluationFailed: return "evaluation-failed";
	default: return "unknown";
	}
}

static rostr text_acpi_GPIO_trigger(DeviceAcpiGPIOTrigger trigger) {
	switch (trigger) {
	case DeviceAcpiGPIOTrigger::Level: return "level";
	case DeviceAcpiGPIOTrigger::Edge: return "edge";
	default: return "n/a";
	}
}

static rostr text_acpi_GPIO_polarity(DeviceAcpiGPIOPolarity polarity) {
	switch (polarity) {
	case DeviceAcpiGPIOPolarity::ActiveHigh: return "active-high";
	case DeviceAcpiGPIOPolarity::ActiveLow: return "active-low";
	case DeviceAcpiGPIOPolarity::ActiveBoth: return "active-both";
	default: return "n/a";
	}
}

static rostr text_acpi_GPIO_pull(DeviceAcpiGPIOPull pull) {
	switch (pull) {
	case DeviceAcpiGPIOPull::Default: return "default";
	case DeviceAcpiGPIOPull::Up: return "up";
	case DeviceAcpiGPIOPull::Down: return "down";
	case DeviceAcpiGPIOPull::None: return "none";
	default: return "vendor";
	}
}

static rostr text_acpi_GPIO_IO_restriction(DeviceAcpiGPIOIORestriction restriction) {
	switch (restriction) {
	case DeviceAcpiGPIOIORestriction::None: return "none";
	case DeviceAcpiGPIOIORestriction::InputOnly: return "input";
	case DeviceAcpiGPIOIORestriction::OutputOnly: return "output";
	case DeviceAcpiGPIOIORestriction::NoneAndPreserve: return "preserve";
	default: return "n/a";
	}
}

static void dump_device_tree_acpi_GPIO(OstreamTrait& com1, const DeviceNode& node, stduint depth) {
	const auto* info = node.fields.acpi_info;
	if (!info || !info->gpio_resources) return;
	for0(index, info->gpio_resource_count) {
		const auto& resource = info->gpio_resources[index];
		dump_device_tree_indent(com1, depth);
		com1.OutFormat("- ACPI-GPIO endpoint=%s type=%s pin=%u source=%s controller=%s status=%s dependency=%s\n\r", info->path[0] ? info->path : "<unknown>", text_acpi_GPIO_type(resource.type), resource.pin, resource.resource_source[0] ? resource.resource_source : "<none>", resource.controller_path[0] ? resource.controller_path : "<unresolved>", text_acpi_GPIO_status(resource.status), text_acpi_GPIO_dependency(resource.dependency_status));
		dump_device_tree_indent(com1, depth + 1);
		com1.OutFormat("consumer=%u trigger=%s polarity=%s sharing=%s wake=%u pull=%s io=%s drive=%u debounce=%u flags=%04X/%04X\n\r", stduint(resource.consumer), text_acpi_GPIO_trigger(resource.trigger), text_acpi_GPIO_polarity(resource.polarity), resource.sharing == DeviceAcpiGPIOSharing::Shared ? "shared" : "exclusive", stduint(resource.wake_capable), text_acpi_GPIO_pull(resource.pull), text_acpi_GPIO_IO_restriction(resource.io_restriction), (stduint)resource.drive_strength, (stduint)resource.debounce_timeout, (stduint)resource.general_flags, (stduint)resource.connection_flags);
	}
}

// ==== ==== ==== ==== . ==== ==== ==== ==== //




static void dump_device_tree_usb_strings(OstreamTrait& com1, const DeviceNode& node, stduint depth) {
	if (DeviceNodeType(node.fields.node_type) != DeviceNodeType::UsbDevice) return;
	if (node.fields.text_manufacturer) {
		dump_device_tree_indent(com1, depth);
		com1.OutFormat("- USB-STR manufacturer=\"%s\"\n\r", node.fields.text_manufacturer);
	}
	if (node.fields.text_product) {
		dump_device_tree_indent(com1, depth);
		com1.OutFormat("- USB-STR product=\"%s\"\n\r", node.fields.text_product);
	}
	if (node.fields.text_serial) {
		dump_device_tree_indent(com1, depth);
		com1.OutFormat("- USB-STR serial=\"%s\"\n\r", node.fields.text_serial);
	}
}

static bool is_partition_storage_node(const DeviceNode& node) {
	if (DeviceNodeType(node.fields.node_type) != DeviceNodeType::StorageDevice) return false;
	if (node.fields.binding.driver_name &&
		StrCompare(node.fields.binding.driver_name, "storage-partition") == 0) {
		return true;
	}
	if (node.link.addr && StrCompareN(node.link.addr, "partition@", 10) == 0) {
		return true;
	}
	return false;
}

#if (_MCCA & 0xFF00) == 0x8600 || defined(_MCU_STM32H7x)
static rostr text_gpio_clock(DeviceGPIOClockState state) {
	switch (state) {
	case DeviceGPIOClockState::NotApplicable: return "n/a";
	case DeviceGPIOClockState::Disabled: return "disabled";
	case DeviceGPIOClockState::Enabled: return "enabled";
	default: return "unknown";
	}
}

#if (_MCCA & 0xFF00) == 0x8600 || PRINT_GPIO_DETAIL // GPIO pin details are temporarily muted.
static rostr text_gpio_mode(uni::GPIOMode mode) {
	switch (mode) {
	case uni::GPIOMode::IN_Floating: return "input";
	case uni::GPIOMode::IN_Pull: return "input-pull";
	case uni::GPIOMode::OUT_PushPull: return "output-push-pull";
	case uni::GPIOMode::OUT_OpenDrain: return "output-open-drain";
	#if defined(_MCU_STM32H7x)
	case uni::GPIOMode::OUT_AF_PushPull: return "af-push-pull";
	case uni::GPIOMode::OUT_AF_OpenDrain: return "af-open-drain";
	case uni::GPIOMode::IN_Analog: return "analog";
	#endif
	default: return "unknown";
	}
}

static rostr text_gpio_pull(uni::GPIOPull pull) {
	switch (pull) {
	case uni::GPIOPull::None: return "none";
	case uni::GPIOPull::Up: return "up";
	case uni::GPIOPull::Down: return "down";
	default: return "unknown";
	}
}

static void dump_device_tree_gpio_pins(OstreamTrait& com1, const DeviceNode& node,
	const DeviceGPIOControllerInfo& info, stduint depth) {
	for0(pin, info.pin_count) {
		uni::GPIOPinState state{};
		dump_device_tree_indent(com1, depth);
		com1.OutFormat("- PIN[%[u]]", pin);
		if (!Devs::GetGPIOPinState(&node, pin, state)) {
			com1.OutFormat(" state=unavailable\n\r");
			continue;
		}
		com1.OutFormat(" mode=%s", (state.valid_fields & uni::GPIOPinStateField_Mode) ? text_gpio_mode(state.mode) : "unknown");
		if (state.valid_fields & uni::GPIOPinStateField_Alternate) com1.OutFormat(" AF=%u", (unsigned)state.alternate);
		if (state.valid_fields & uni::GPIOPinStateField_Pull) com1.OutFormat(" pull=%s", text_gpio_pull(state.pull));
		if (state.valid_fields & uni::GPIOPinStateField_Speed) com1.OutFormat(" speed=%u", (unsigned)state.speed);
		if (state.valid_fields & uni::GPIOPinStateField_Input) com1.OutFormat(" in=%u", (unsigned)state.input_value);
		if (state.valid_fields & uni::GPIOPinStateField_Output) com1.OutFormat(" out=%u", (unsigned)state.output_value);
		if (state.valid_fields & uni::GPIOPinStateField_Locked) com1.OutFormat(" locked=%u", (unsigned)state.locked);
		if (state.valid_fields & uni::GPIOPinStateField_OutputLocked) com1.OutFormat(" output-locked=%u", (unsigned)state.output_locked);
		com1.OutFormat("\n\r");
	}
}
#endif
#endif

static void dump_device_tree_node(OstreamTrait& com1, const DeviceNode* node, stduint depth, bool verbose) {
	for (auto crt = node; crt; crt = cast<DeviceNode*>(crt->link.next)) {
		if (!verbose && is_partition_storage_node(*crt)) {
			continue;
		}
		dump_device_tree_indent(com1, depth);
		const rostr name = crt->link.addr ? crt->link.addr : "(unnamed)";
		const rostr driver_name0 = crt->fields.binding.driver_name ? crt->fields.binding.driver_name : nullptr;
		String driver_name_str = driver_name0 ? "\033[7m" : "\033[33m";
		if (driver_name0) driver_name_str += driver_name0;
		driver_name_str += driver_name0 ? "\033[27m" : "\033[39m";
		const rostr driver_name = driver_name0 ? driver_name_str.reference() : nullptr;
		const bool has_binding = driver_name && crt->fields.binding.state != static_cast<uint32>(DriverBindingState::None);
		#if (_MCCA & 0xFF00) == 0x8600 || defined(_MCU_STM32H7x)
		DeviceGPIOControllerInfo gpio_info{};
		const bool gpio_controller = Devs::GetGPIOControllerInfo(crt, gpio_info);
		if (gpio_controller) {
			com1.OutFormat("%s <%s bus=%s%s%s%s%s %s=%s pin-slots=%[u]>\n\r", name,
				text_device_node_type(crt->fields.node_type), text_device_bus_type(crt->fields.bus_type),
				driver_name ? " drv=" : "", driver_name ? driver_name : "",
				has_binding ? " state=" : "", has_binding ? text_driver_binding_state(crt->fields.binding.state) : "",
				#if defined(_MCU_STM32H7x)
				"rcc",
				#else
				"clock",
				#endif
				text_gpio_clock(gpio_info.clock_state), gpio_info.pin_count);
			if (gpio_info.clock_state == DeviceGPIOClockState::Disabled) continue;
		}
		else
		#endif
		switch (DeviceNodeType(crt->fields.node_type)) {
		case DeviceNodeType::BusRoot:
		case DeviceNodeType::PCI_Root:
		case DeviceNodeType::IsaBus:
			com1.OutFormat("%s <%s bus=%s%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				text_device_bus_type(crt->fields.bus_type),
				driver_name ? " drv=" : "",
				driver_name ? driver_name : "",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		case DeviceNodeType::PciBus:
			com1.OutFormat("%s <%s seg=%[u] bus=%[u]%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				(stduint)crt->fields.pci_segment, (stduint)crt->fields.pci_bus,
				driver_name ? " drv=" : "",
				driver_name ? driver_name : "",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		case DeviceNodeType::PciDevice:
			com1.OutFormat("%s <%s %02X.%02X.%02X vend=%04X dev=%04X%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				(unsigned)crt->fields.class_base, (unsigned)crt->fields.class_sub, (unsigned)crt->fields.class_if,
				(unsigned)crt->fields.vendor_id, (unsigned)crt->fields.device_id,
				driver_name ? " drv=" : "",
				driver_name ? driver_name : " (unknown) ",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		case DeviceNodeType::StorageDevice:
			com1.OutFormat("%s <%s bus=%s%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				text_device_bus_type(crt->fields.bus_type),
				driver_name ? " drv=" : "",
				driver_name ? driver_name : "",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		case DeviceNodeType::UsbDevice:
			com1.OutFormat("%s <%s %02X.%02X.%02X vid=%04X pid=%04X bus=%s%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				(unsigned)crt->fields.class_base, (unsigned)crt->fields.class_sub, (unsigned)crt->fields.class_if,
				(unsigned)crt->fields.vendor_id, (unsigned)crt->fields.device_id,
				text_device_bus_type(crt->fields.bus_type),
				driver_name ? " drv=" : "",
				driver_name ? driver_name : "",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		case DeviceNodeType::UsbRootHub:
			com1.OutFormat("%s <%s %02X.%02X.%02X bus=%s%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				(unsigned)crt->fields.class_base, (unsigned)crt->fields.class_sub, (unsigned)crt->fields.class_if,
				text_device_bus_type(crt->fields.bus_type),
				driver_name ? " drv=" : "",
				driver_name ? driver_name : "",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		case DeviceNodeType::UsbPort:
			com1.OutFormat("%s <%s bus=%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				text_device_bus_type(crt->fields.bus_type));
			break;
		case DeviceNodeType::UsbInterface:
			com1.OutFormat("%s <%s %02X.%02X.%02X bus=%s%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				(unsigned)crt->fields.class_base, (unsigned)crt->fields.class_sub, (unsigned)crt->fields.class_if,
				text_device_bus_type(crt->fields.bus_type),
				driver_name ? " drv=" : "",
				driver_name ? driver_name : "",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		case DeviceNodeType::I2cDevice:
			#if (_MCCA & 0xFF00) == 0x8600
			if (driver_name0 && StrCompare(driver_name0, "hid-i2c") == 0 && DriverBindingState(crt->fields.binding.state) == DriverBindingState::Started && crt->fields.binding.driver_data) {
				const auto& descriptor = static_cast<const uni::HIDI2CDevice*>(crt->fields.binding.driver_data)->Descriptor();
				com1.OutFormat("%s <%s bus=%s drv=%s state=%s hid-ver=%04X vid=%04X pid=%04X report=%u input=%u output=%u input-caps=%08X>\n\r",
					name, text_device_node_type(crt->fields.node_type), text_device_bus_type(crt->fields.bus_type), driver_name,
					text_driver_binding_state(crt->fields.binding.state), (stduint)descriptor.version,
					(stduint)descriptor.vendor_id, (stduint)descriptor.product_id,
					(stduint)descriptor.report_descriptor_length, (stduint)descriptor.maximum_input_length,
					(stduint)descriptor.maximum_output_length, Devsman::GetInputCapabilities(crt));
				break;
			}
			#endif
			com1.OutFormat("%s <%s bus=%s%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type), text_device_bus_type(crt->fields.bus_type),
				driver_name ? " drv=" : "", driver_name ? driver_name : "",
				has_binding ? " state=" : "", has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		case DeviceNodeType::UsbBus:
		case DeviceNodeType::PlatformDevice:
		case DeviceNodeType::SerioController:
		case DeviceNodeType::SerioDevice:
		case DeviceNodeType::I2cBus:
		case DeviceNodeType::SpiBus:
		case DeviceNodeType::SpiDevice:
		case DeviceNodeType::UartBus:
		case DeviceNodeType::UartDevice:
			com1.OutFormat("%s <%s bus=%s%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				text_device_bus_type(crt->fields.bus_type),
				driver_name ? " drv=" : "",
				driver_name ? driver_name : "",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		default:
			com1.OutFormat("%s <%s%s%s%s%s>\n\r",
				name, text_device_node_type(crt->fields.node_type),
				driver_name ? " drv=" : "",
				driver_name ? driver_name : "",
				has_binding ? " state=" : "",
				has_binding ? text_driver_binding_state(crt->fields.binding.state) : "");
			break;
		}
		if (verbose) {
			if (crt->fields.controller) {
				dump_device_tree_indent(com1, depth + 1);
				com1.OutFormat("- CONTROLLER %s\n\r", crt->fields.controller->link.addr ?
					crt->fields.controller->link.addr : "(unnamed)");
			}
			dump_device_tree_pci_description(com1, *crt, depth + 1);
			dump_device_tree_controller_resources(com1, *crt, depth + 1);
			dump_device_tree_intel_lpss_i2c(com1, *crt, depth + 1);
			dump_device_tree_driver_match(com1, *crt, depth + 1);
			dump_device_tree_acpi_GPIO(com1, *crt, depth + 1);
		}
		if (verbose && crt->fields.resource_count) {
			#if defined(_MCU_STM32H7x)
			if (!gpio_controller) // GPIO MMIO details are temporarily muted.
			#endif
			dump_device_tree_resources(com1, *crt, depth + 1);
		}
		#if (_MCCA & 0xFF00) == 0x8600 || defined(_MCU_STM32H7x)
		#if (_MCCA & 0xFF00) == 0x8600 || PRINT_GPIO_DETAIL
		if (verbose && gpio_controller) dump_device_tree_gpio_pins(com1, *crt, gpio_info, depth + 1);
		#endif
		#endif
		if (verbose) {
			dump_device_tree_usb_strings(com1, *crt, depth + 1);
		}
		if (crt->link.subf) {
			dump_device_tree_node(com1, cast<DeviceNode*>(crt->link.subf), depth + 1, verbose);
		}
	}
}

void dump_device_tree(OstreamTrait& com1, bool verbose) {
	com1.OutFormat("=== Device Tree ===\n\r");
	DriverMatchSummary match_summary{};
	collect_driver_match_summary(Devsman::Root(), match_summary);
	com1.OutFormat("[DRIVER-MATCH-SUM] matched=%u unsupported=%u missing-resource=%u ambiguous=%u rejected-by-capability=%u\n\r", match_summary.matched, match_summary.unsupported, match_summary.missing_resource, match_summary.ambiguous, match_summary.rejected_by_capability);
	auto root = Devsman::Root();
	if (!root) {
		com1.OutFormat("(empty)\n\r");
		return;
	}
	dump_device_tree_node(com1, root, 0, verbose);
}

void dump_device_tree(OstreamTrait& com1) {
	dump_device_tree(com1, true);
}

// The driver registries are read without locking
struct DriverNameSet {
	static constexpr stduint Capacity = 64;
	const char* names[Capacity];
	stduint count = 0;
	bool truncated = false;

	void Add(const char* name) {
		if (!name || !name[0]) return;
		for (stduint i = 0; i < count; i++) {
			if (StrCompare(names[i], name) == 0) return;
		}
		if (count >= Capacity) {
			truncated = true;
			return;
		}
		names[count++] = name;
	}
};

static void collect_bound_driver_names(DeviceNode* node, DriverNameSet& set) {
	for (auto crt = node; crt; crt = cast<DeviceNode*>(crt->link.next)) {
		set.Add(crt->fields.binding.driver_name);
		if (crt->link.subf) collect_bound_driver_names(cast<DeviceNode*>(crt->link.subf), set);
	}
}

// ==== ==== ==== ==== Backends Series ==== ==== ==== ==== //


static void dump_audio_backends(OstreamTrait& com1) {
	com1.OutFormat("=== Audio Backends ===\n\r");
	#if (_MCCA & 0xFF00) == 0x8600
	const stduint count = audio_manager.getCardCount();
	const stduint selected = audio_manager.getSelected();
	if (!count) {
		com1.OutFormat("(none)\n\r");
		return;
	}
	for (stduint i = 0; i < count; i++) {
		const char* name = audio_manager.getCardName(i);
		com1.OutFormat("[%[u]] %s%s\n\r", i, name ? name : "(unnamed)", i == selected ? " (selected)" : "");
	}
	if (selected >= count) com1.OutFormat("(no card selected)\n\r");
	#else
	com1.OutFormat("(none)\n\r");
	#endif
}

static void dump_driver_backends(OstreamTrait& com1) {
	DriverNameSet set;
	#if (_MCCA & 0xFF00) == 0x8600
	for (stduint i = 0; i < audio_manager.getCardCount(); i++) set.Add(audio_manager.getCardName(i));
	#endif
	for (stduint i = 0; i < Devsman::GetDriverStartHookCount(); i++) set.Add(Devsman::GetDriverStartHookName(i));
	for (stduint i = 0; i < Devsman::GetDriverProcessCount(); i++) set.Add(Devsman::GetDriverProcessName(i));
	if (auto* root = Devsman::Root()) collect_bound_driver_names(root, set);

	com1.OutFormat("=== Builtin / Ring1 Drivers (%[u]) ===\n\r", set.count);
	if (!set.count) {
		com1.OutFormat("(none)\n\r");
		return;
	}
	for (stduint i = 0; i < set.count; i++) {
		if (i) com1.OutFormat(", ");
		com1.OutFormat("%s", set.names[i]);
	}
	if (set.truncated) com1.OutFormat(", ...");
	com1.OutFormat("\n\r");
}

void dump_backends(OstreamTrait& com1, SysinfoDumpDevsType type) {
	typedef void (*table_backends_entry)(OstreamTrait & com1);
	static table_backends_entry table_backends[] = {
		dump_driver_backends,
		dump_audio_backends,
	};
	if (_IMM(type) >= numsof(table_backends)) return;
	table_backends[_IMM(type)](com1);
}




