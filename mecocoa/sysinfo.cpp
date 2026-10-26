// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// #pragma GCC optimize("O2")
#include "../include/mecocoa.hpp"

#include <c/cpuid.h>
#include <c/driver/mouse.h>
#include <c/driver/keyboard.h>
#include <c/driver/RealtimeClock.h>
#if (_MCCA & 0xFF00) == 0x8600
#include <cpp/Device/Bus/Serial/IIC-HID.hpp>
#include <cpp/Device/IIC>
#endif
#include "../include/console.hpp"
#include "../depends/desktop.hpp"

String dump_availmem();
rostr text_cpu_factory();

Procontroller_t cpu_type = PCU_Unknown;

loglevel_t _log_limit = _LOG_INFO;

void mecfetch() {
	#if ((_MCCA & 0xFF00) == 0x8600)
	const rostr blue = "\033[48;2;88;200;248m"; 	//  ento_gui ? "\xFE\xF8\xC8\x58" : "\xFF\x30";
	const rostr pink = "\033[48;2;248;168;184m";	//  ento_gui ? "\xFE\xB8\xA8\xF8" : "\xFF\x50";
	const rostr white = "\033[48;2;255;255;255m";	//  ento_gui ? "\xFE\xFF\xFF\xFF" : "\xFF\x70";
	const unsigned attrl = Consman::ento_gui ? 4 : 2;
	const unsigned width = Consman::ento_gui ? 48 : 16;
	const unsigned height = Consman::ento_gui ? 3 : 1;

	#if CONFIG_ECHO_LOGO
	Console.OutFormat(blue, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\033[0m\n\r%s", blue); }
	Console.OutFormat(pink, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\033[0m\n\r%s", pink); }
	Console.OutFormat(white, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\033[0m\n\r%s", white); }
	Console.OutFormat(pink, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\033[0m\n\r%s", pink); }
	Console.OutFormat(blue, attrl);
	for0(j, height) { for0(i, width) Console.OutChar(' '); Console.OutFormat("\033[0m\n\r%s", blue); }
	Console.OutFormat("\033[0m");// Console.out("\xFF\xFF", 2);
	#endif

	printlog(_LOG_TRACE, "メココア");
	printlog(cpu_type ? _LOG_GOOD : _LOG_WARN, "CPU Factory: %s", text_cpu_factory());
	ploginfo("CPU Brand: %s", text_brand());

	tm datime = {};
	#if _MCCA == 0x8632 //((_MCCA & 0xFF00) == 0x8600)
	CMOS_Readtime(&datime);
	#endif
	Console.OutFormat("Date Time: %d/%d/%d %d:%d:%d\n\r",
		datime.tm_year, datime.tm_mon, datime.tm_mday,
		datime.tm_hour, datime.tm_min, datime.tm_sec
	);
	#endif

	Console.OutFormat("Avail Mem: %s\n\r", dump_availmem().reference());
}// like neofetch

#if ((_MCCA & 0xFF00) == 0x8600)

char _buf[64];
String ker_buf(_buf, byteof(_buf));
extern uint32 _start_eax, _start_ebx;

void ScreenLogPanic();

void kernel_fail(void* _serious, ...) {
	Letvar(serious, loglevel_t, _IMM(_serious));
	if (serious == _LOG_FATAL) {
		#ifndef _UEFI_AUFDBG
		outsfmt("\n\rKernel panic!\n\r");
		#else
		ScreenLogPanic();
		#endif
		__asm("cli; hlt");
	}
}

rostr text_cpu_factory() {
	unsigned a[1], b[1], c[1], d[1];
	unsigned* ker_ptr = (unsigned*)ker_buf.reflect();
	_IO_CPUID(0, 0, a, b, c, d);
	*ker_ptr++ = *b;
	*ker_ptr++ = *d;
	*ker_ptr++ = *c;
	*ker_ptr = 0;
	if (StrCompare("GenuineIntel", ker_buf.reference()) == 0) cpu_type = PCU_Intel;
	else if (StrCompare("AuthenticAMD", ker_buf.reference()) == 0) cpu_type = PCU_AMD;
	else cpu_type = PCU_Unknown;
	return ker_buf.reference();
}

rostr text_brand() {
	CpuBrand(ker_buf.reflect());
	ker_buf.reflect()[_CPU_BRAND_SIZE] = 0;
	const char* ret = ker_buf.reference();
	while (*ret == ' ') ret++;
	return ret;
}

#endif

extern file_system_type* registered_filesystems;
static void dump_filesys(OstreamTrait& com1) {
	for (file_system_type* fs_type = registered_filesystems; fs_type; fs_type = fs_type->next) {
		com1.OutFormat("- %s \n\r", fs_type->name);
	}
}

String dump_availmem() {
	stduint mem = Memory::total_memsize;
	stduint frac = 0;
	char unit[]{ ' ', 'K', 'M', 'G', 'T' };
	int level = 0;
	// assert mem != 0
	while (mem >= 1024 && level < 4) {
		frac = (mem % 1024) * 100 / 1024;
		mem /= 1024;
		level++;
	}
	String ret;
	if (level) ret.Format("%u.%02u %cB", (unsigned int)mem, (unsigned int)frac, unit[level]);
	else ret.Format("0x%[x] B", Memory::total_memsize);
	return ret;
}

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

static void dump_device_tree_resources(OstreamTrait& com1, const DeviceNode& node, stduint depth) {
	for0(i, node.fields.resource_count) {
		const auto& res = node.fields.resources[i];
		dump_device_tree_indent(com1, depth);
		switch (DeviceResourceType(res.type)) {
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
			dump_device_tree_pci_description(com1, *crt, depth + 1);
			dump_device_tree_controller_resources(com1, *crt, depth + 1);
			dump_device_tree_intel_lpss_i2c(com1, *crt, depth + 1);
			dump_device_tree_driver_match(com1, *crt, depth + 1);
			dump_device_tree_acpi_GPIO(com1, *crt, depth + 1);
		}
		if (verbose && crt->fields.resource_count) {
			dump_device_tree_resources(com1, *crt, depth + 1);
		}
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

void dump_threads(OstreamTrait& com1) {
	// print all threads
	extern Spinlock scheduler_lock;
	SpinlockLocal guard(&scheduler_lock);
	auto pad_column = [&com1](int used, int width) {
		for (; used < width; used++) com1.OutChar(' ');
	};
	const int pointer_column = sizeof(stduint) * 2 + 4; // 0x + hex digits + two spaces
	com1.OutFormat("\033[1mTID  PID   STATE     REASON   CPU \033[35mPRI  \033[32mNAME\033[0m\n\r");
	int header_used = 0;
	com1.OutFormat("\033[1;36m    ");
	com1.OutFormat("IP%n", &header_used);
	pad_column(header_used, pointer_column);
	com1.OutFormat("SP%n", &header_used);
	pad_column(header_used, pointer_column);
	com1.OutFormat("\033[33m");
	com1.OutFormat("LINE_PEAK%n", &header_used);
	pad_column(header_used, 10);
	com1.OutFormat("KERN_PEAK\033[0m\n\r");
	com1.OutFormat("\033[1;34m    SEND     RECV     QHEAD    QNEXT    BR       UMSG\033[0m\n\r");
	for (auto nod = Taskman::thchain.Root(); nod; nod = nod->next) {
		auto th = cast<ThreadBlock*>(nod->offs);
		int used = 0;

		// TID
		com1.OutFormat("%[u]%n", th->tid, &used);
		pad_column(used, 5);

		// PID
		stduint pid = th->parent_process ? th->parent_process->pid : 0;
		com1.OutFormat("%[u]%n", pid, &used);
		pad_column(used, 6);

		// STATE
		const char* state_name = "Unknown";
		switch (th->state) {
		case ThreadBlock::State::Running: state_name = "Running"; break;
		case ThreadBlock::State::Ready:   state_name = "Ready";   break;
		case ThreadBlock::State::Pended:  state_name = "Pended";  break;
		case ThreadBlock::State::Uninit:  state_name = "Uninit";  break;
		case ThreadBlock::State::Exited:  state_name = "Exited";  break;
		case ThreadBlock::State::Hanging: state_name = "Hanging"; break;
		case ThreadBlock::State::Invalid: state_name = "Invalid"; break;
		}
		com1.OutFormat("%s%n", state_name, &used);
		pad_column(used, 10);

		// REASON
		const char* reason_name = "None";
		if (th->state == ThreadBlock::State::Pended) {
			if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_Resting)) reason_name = "Rest";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_SendMsg)) reason_name = "Send";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_RecvMsg)) reason_name = "Recv";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_Waiting)) reason_name = "Wait";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_Lock)) reason_name = "Lock";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_SendPipe)) reason_name = "PipeW";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_RecvPipe)) reason_name = "PipeR";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_SendSock)) reason_name = "SockW";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_RecvSock)) reason_name = "SockR";
			else if (_IMM(th->block_reason) & _IMM(ThreadBlock::BlockReason::BR_Exiting)) reason_name = "Exit";
		}
		com1.OutFormat("%s%n", reason_name, &used);
		pad_column(used, 9);

		// CPU
		if (th->processor_id == CORE_ID_INVALID) {
			com1.OutFormat("-%n", &used);
		}
		else {
			com1.OutFormat("%[u]%n", th->processor_id, &used);
		}
		pad_column(used, 4);

		// PRI
		com1.OutFormat("\033[35m");
		com1.OutFormat("%[i]%n", (stdsint)th->priority, &used);
		pad_column(used, 5);
		com1.OutFormat("\033[32m%s\033[0m\n\r", th->name.get());

		// IP, SP and sampled stack peaks
		com1.OutFormat("\033[36m    ");
		com1.OutFormat("%p%n", th->context.IP, &used);
		pad_column(used, pointer_column);
		com1.OutFormat("%p%n", th->context.SP, &used);
		pad_column(used, pointer_column);
		com1.OutFormat("\033[33m");
		#if CONFIG_DEBUG_STACKSIZE
		if (th->stack_lineaddr && th->stack_lineaddr != th->stack_levladdr)
			com1.OutFormat("%[u]%n", th->max_linestack_usage, &used);
		else
			com1.OutFormat("-%n", &used);
		pad_column(used, 10);
		if (th->stack_levladdr)
			com1.OutFormat("%[u]\033[0m\n\r", th->max_phyzstack_usage);
		else
			com1.OutFormat("-\033[0m\n\r");
		#else
		com1.OutFormat("-         -\033[0m\n\r");
		#endif

		// IPC state
		String str[6]{ "(null)", "(null)", "(null)", "(null)", "(null)", "0" };
		if (th->send_to_whom) {
			str[0].Format("%u", th->send_to_whom->getID());
		}
		if (th->recv_fo_whom) {
			if ((stduint)th->recv_fo_whom == INTRUPT) str[1] = "(RUPT)";
			else if ((stduint)th->recv_fo_whom == ANYPROC) str[1] = "(ANY)";
			else str[1].Format("%u", th->recv_fo_whom->getID());
		}
		if (th->queue_send_queuehead) {
			str[2].Format("%u", th->queue_send_queuehead->getID());
		}
		if (th->queue_send_queuenext) {
			str[3].Format("%u", th->queue_send_queuenext->getID());
		}
		if (th->unsolved_msg) {
			if (_IMM(th->unsolved_msg) == ~_IMM0) str[4] = "(DEAD)";
			else str[4].Format("%p", (void*)th->unsolved_msg);
		}
		str[5].Format("%u", _IMM(th->block_reason));

		com1.OutFormat("\033[34m    ");
		com1.OutFormat("%s%n", str[0].reference(), &used);
		pad_column(used, 9);
		com1.OutFormat("%s%n", str[1].reference(), &used);
		pad_column(used, 9);
		com1.OutFormat("%s%n", str[2].reference(), &used);
		pad_column(used, 9);
		com1.OutFormat("%s%n", str[3].reference(), &used);
		pad_column(used, 9);
		com1.OutFormat("%s%n", str[5].reference(), &used);
		pad_column(used, 9);
		com1.OutFormat("%s\033[0m\n\r", str[4].reference());
	}
}

extern "C" void* ring3_iret_stacks[PCU_CORES_MAX];
void dump_processors(OstreamTrait& com1) {
	extern Spinlock scheduler_lock;
	SpinlockLocal guard(&scheduler_lock);
	com1.OutFormat("CPU   STATE     LAPIC CURRENT_TID   SWITCH_TID TRAMPOLINE KSTACK\n\r");
	for (stduint i = 0; i < Taskman::PCU_CORES; i++) {
		#if (_MCCA & 0xFF00) == 0x8600
		auto percore = Taskman::PCU_CORES_PERCORE[i];
		if (!percore) continue;

		// CPU Core ID
		com1.OutFormat("%[u]", i);
		if (i < 10) com1.OutFormat("     ");
		else com1.OutFormat("    ");

		// State
		const char* state_name = "Unknown";
		switch (percore->state) {
		case CoreState::Empty:    state_name = "Empty";    break;
		case CoreState::Prepared: state_name = "Prepared"; break;
		case CoreState::Booting:  state_name = "Booting";  break;
		case CoreState::Online:   state_name = "Online";   break;
		case CoreState::Failed:   state_name = "Failed";   break;
		}
		com1.OutFormat("%s", state_name);
		stduint state_len = 0;
		while (state_name[state_len]) state_len++;
		for (stduint s = state_len; s < 10; s++) com1.OutFormat(" ");

		// LAPIC ID
		if (percore->lapic_id == CORE_ID_INVALID) {
			com1.OutFormat("-     ");
		}
		else {
			com1.OutFormat("%[u]", (stduint)percore->lapic_id);
			if (percore->lapic_id < 10) com1.OutFormat("     ");
			else if (percore->lapic_id < 100) com1.OutFormat("    ");
			else com1.OutFormat("   ");
		}

		// Current Thread
		if (percore->current_thread) {
			com1.OutFormat("%10u    ", percore->current_thread->tid);
			stduint tid = percore->current_thread->tid;
		}
		else {
			com1.OutFormat("-             ");
		}

		// Switching Out Thread
		if (percore->switching_out_thread) {
			com1.OutFormat("%10u ", percore->switching_out_thread->tid);
		}
		else {
			com1.OutFormat("-          ");
		}

		// TRAMPS
		com1.OutFormat("%p ", ring3_iret_stacks[i]);

		// Kernel Stack
		com1.OutFormat("%p\n\r", percore->kernel_stack);

		#else
		// For non-x86 architectures
		com1.OutFormat("%[u]     Online    -     ", i);

		auto cur_th = Taskman::current_thread(i);
		if (cur_th) {
			com1.OutFormat("%[u]", cur_th->tid);
			stduint tid = cur_th->tid;
			if (tid < 10) com1.OutFormat("             ");
			else if (tid < 100) com1.OutFormat("            ");
			else com1.OutFormat("           ");
		}
		else {
			com1.OutFormat("-             ");
		}

		auto sw_th = Taskman::switching_out_threads(i);
		if (sw_th) {
			com1.OutFormat("%[u]", sw_th->tid);
			stduint tid = sw_th->tid;
			if (tid < 10) com1.OutFormat("             ");
			else if (tid < 100) com1.OutFormat("            ");
			else com1.OutFormat("           ");
		}
		else {
			com1.OutFormat("-             ");
		}

		com1.OutFormat("-\n\r");
		#endif
	}
}

void dump_ready_queue(OstreamTrait& com1) {
	extern Spinlock scheduler_lock;
	SpinlockLocal guard(&scheduler_lock);

	com1.OutFormat("=== Active Ready Queues ===\n\r");
	bool active_any = false;
	for (int idx = 31; idx >= 0; idx--) {
		if (Taskman::priority_queues[idx].head) {
			active_any = true;
			com1.OutFormat("  Pri %[i] (idx %[i]):", (stdsint)idx - 16, idx);
			ThreadBlock* node = Taskman::priority_queues[idx].head;
			stduint guard_cnt = 0;
			while (node) {
				com1.OutFormat(" -> Th%[u]", node->tid);
				node = node->queue_state_next;
				if (++guard_cnt > 4096) break;
			}
			com1.OutFormat("\n\r");
		}
	}
	if (!active_any) {
		com1.OutFormat("  (empty)\n\r");
	}

	com1.OutFormat("=== Expired Ready Queues ===\n\r");
	bool expired_any = false;
	for (int idx = 31; idx >= 0; idx--) {
		if (Taskman::expired_queues[idx].head) {
			expired_any = true;
			com1.OutFormat("  Pri %[i] (idx %[i]):", (stdsint)idx - 16, idx);
			ThreadBlock* node = Taskman::expired_queues[idx].head;
			stduint guard_cnt = 0;
			while (node) {
				com1.OutFormat(" -> Th%[u]", node->tid);
				node = node->queue_state_next;
				if (++guard_cnt > 4096) break;
			}
			com1.OutFormat("\n\r");
		}
	}
	if (!expired_any) {
		com1.OutFormat("  (empty)\n\r");
	}
}

static Spinlock log_lock;
void printlog(loglevel_t level, const char* fmt, ...)
{
	SpinlockLocal lock(&log_lock);
	Letpara(paras, fmt);
	printlogx(level, fmt, paras);
	para_endo(paras);
}

namespace uni {
	extern Mutex vfs_lock;
}

void dump_lock(OstreamTrait& com1) {
	// Declare the external global spinlocks
	extern Spinlock scheduler_lock;
	extern Spinlock timer_lock;
	extern Spinlock mempool_lock;
	extern Spinlock comm_lock;
	extern Spinlock log_lock;

	// Declare the external global mutexes
	extern Mutex outc_mutex;
	#if _MCCA == 0x8632 && _GUI_ENABLE && _GUI_FREETYPE
	extern Mutex ft_lock;
	#endif

	com1.OutFormat("=== Global Spinlocks ===\n\r");
	com1.OutFormat("Address             Locked  HCPUID  Name\n\r");
	com1.OutFormat("%p  %s     %[i]      scheduler_lock\n\r", &scheduler_lock, scheduler_lock.locked ? "Yes" : "No ", (stdsint)scheduler_lock.cpu_id);
	com1.OutFormat("%p  %s     %[i]      timer_lock\n\r", &timer_lock, timer_lock.locked ? "Yes" : "No ", (stdsint)timer_lock.cpu_id);
	com1.OutFormat("%p  %s     %[i]      mempool_lock\n\r", &mempool_lock, mempool_lock.locked ? "Yes" : "No ", (stdsint)mempool_lock.cpu_id);
	com1.OutFormat("%p  %s     %[i]      comm_lock\n\r", &comm_lock, comm_lock.locked ? "Yes" : "No ", (stdsint)comm_lock.cpu_id);
	com1.OutFormat("%p  %s     %[i]      log_lock\n\r", &log_lock, log_lock.locked ? "Yes" : "No ", (stdsint)log_lock.cpu_id);

	com1.OutFormat("\n\r=== Global Mutexes ===\n\r");
	com1.OutFormat("Address             Locked  Waiters Name\n\r");
	com1.OutFormat("%p  %s     %[u]       outc_mutex\n\r", &outc_mutex, outc_mutex.locked ? "Yes" : "No ", outc_mutex.wait_queue.Count());
	com1.OutFormat("%p  %s     %[u]       vfs_lock\n\r", &uni::vfs_lock, uni::vfs_lock.locked ? "Yes" : "No ", uni::vfs_lock.wait_queue.Count());
	#if _MCCA == 0x8632 && _GUI_ENABLE && _GUI_FREETYPE
	com1.OutFormat("%p  %s     %[u]       ft_lock\n\r", &ft_lock, ft_lock.locked ? "Yes" : "No ", ft_lock.wait_queue.Count());
	#endif
	com1.OutFormat("\n\r=== Per-Process Locks ===\n\r");
	com1.OutFormat("PID  Process Name        VMA Lock (Spinlock)    Sys Lock (Mutex)\n\r");
	com1.OutFormat("                         Addr      Locked HCPUID Addr      Locked Waiters\n\r");

	// Never block here: dump_lock() is also used by spinlock timeout diagnostics.
	bool old_if = false;
	if (!scheduler_lock.TryAcquire(old_if)) {
		com1.OutFormat("(skip process lock walk: scheduler_lock busy on CPU %[i])\n\r", (stdsint)scheduler_lock.cpu_id);
		return;
	}
	for (auto nod = Taskman::chain.Root(); nod; nod = nod->next) {
		auto pb = cast<ProcessBlock*>(nod->offs);
		com1.OutFormat("%[u]    ", pb->pid);
		// Align name
		stduint name_len = 0;
		if (pb->main_thread && pb->main_thread->name) {
			com1.OutFormat("%s", pb->main_thread->name.get());
			while (pb->main_thread->name.get()[name_len]) name_len++;
		} else {
			com1.OutFormat("(unknown)");
			name_len = 9;
		}
		for (stduint space = name_len; space < 20; space++) {
			com1.OutFormat(" ");
		}
		auto& fileman_mutex = pb->fileman.raw_mutex();
		com1.OutFormat("%p  %s    %[i]      %p  %s    %[u]\n\r",
			&pb->vma_lock, pb->vma_lock.locked ? "Yes" : "No ", (stdsint)pb->vma_lock.cpu_id,
			&fileman_mutex, fileman_mutex.locked ? "Yes" : "No ", fileman_mutex.wait_queue.Count());
	}
	scheduler_lock.Release(old_if);
}



#if _GUI_ENABLE
static const char* find_video_driver_name_recursive(const DeviceNode* root, void* driver_data) {
	if (!root) return nullptr;
	for (auto crt = root; crt; crt = cast<DeviceNode*>(crt->link.next)) {
		if (crt->fields.binding.driver_data == driver_data && crt->fields.binding.driver_name) {
			return crt->fields.binding.driver_name;
		}
		if (crt->link.subf) {
			auto res = find_video_driver_name_recursive(cast<DeviceNode*>(crt->link.subf), driver_data);
			if (res) return res;
		}
	}
	return nullptr;
}
#endif

void dump_screens_and_forms(OstreamTrait& com1) {
	com1.OutFormat("=== Display & Screen Devices ===\n\r");
	#if _GUI_ENABLE && ((_MCCA & 0xFF00) == 0x8600)
	if (Consman::current_video_device) {
		auto fb = Consman::current_video_device->GetFramebuffer();
		const char* drv_name = find_video_driver_name_recursive(Devsman::Root(), Consman::current_video_device);
		if (!drv_name) drv_name = "classic-video-driver";

		const char* fmt_str = "Unknown";
		if (fb.format == PixelFormat::ARGB8888) fmt_str = "ARGB8888";
		else if (fb.format == PixelFormat::RGB565) fmt_str = "RGB565";
		else if (fb.format == PixelFormat::RGB888) fmt_str = "RGB888";
		else if (fb.format == PixelFormat::RGBA8888) fmt_str = "RGBA8888";

		com1.OutFormat("Screen 0: [Active] %[u]x%[u] (%[u] bpp, %s) | Driver: %s\n\r",
			(stduint)fb.screen_size.x, (stduint)fb.screen_size.y,
			(stduint)fb.bpp, fmt_str, drv_name);
		com1.OutFormat("          FB Physical Address: %p, Pitch: %[u] bytes, Size: %[u] bytes\n\r",
			(void*)fb.physical_range.address, (stduint)fb.pitch, (stduint)fb.physical_range.length);
	} else {
		com1.OutFormat("No active video device.\n\r");
	}

	com1.OutFormat("\n\r=== Window / Form Topology ===\n\r");
	com1.OutFormat("PID  FormID State        Z-Index  Position & Size       Title\n\r");

	extern Spinlock scheduler_lock;
	bool old_if = false;
	if (!scheduler_lock.TryAcquire(old_if)) {
		com1.OutFormat("(skip form walk: scheduler_lock busy on CPU %[i])\n\r", (stdsint)scheduler_lock.cpu_id);
		return;
	}

	constexpr stduint MAX_PROCS = 128;
	stduint pids[MAX_PROCS];
	stduint proc_count = 0;
	for (auto pnod = Taskman::chain.Root(); pnod && proc_count < MAX_PROCS; pnod = pnod->next) {
		auto p = cast<ProcessBlock*>(pnod->offs);
		pids[proc_count++] = p->pid;
	}
	scheduler_lock.Release(old_if);

	stduint total_forms = 0;
	struct FormItem {
		stduint pid;
		stduint form_id;
		::uni::Witch::Form* pfrm;
	};
	constexpr stduint MAX_DUMP_FORMS = 128;
	FormItem items[MAX_DUMP_FORMS];
	stduint item_count = 0;

	for (stduint i = 0; i < proc_count && item_count < MAX_DUMP_FORMS; i++) {
		ProcessBlock* pb = ProcessBlock::AcquireActiveByPID(pids[i]);
		if (!pb) continue;
		{
			auto pforms = pb->pforms.Lock();
			for (stduint j = 0; j < pforms->Count() && item_count < MAX_DUMP_FORMS; j++) {
				SheetTrait* st = (*pforms)[j];
				if (!st) continue;
				items[item_count++] = { pb->pid, j, static_cast<::uni::Witch::Form*>(st) };
			}
		}
		ProcessBlock::Release(pb);
	}

	for (stduint k = 0; k < item_count; k++) {
		auto& item = items[k];
		auto pfrm = item.pfrm;
		total_forms++;

		// Calculate Z-index in global_layman (from bottom to top)
		stdsint z_index = -1;
		bool is_top = false;
		{
			auto layman = global_layman.Lock();
			stduint cur_z = 0;
			for (auto nod = layman->subl; nod; nod = nod->left) {
				if (nod->offs == pfrm) {
					z_index = (stdsint)cur_z;
				}
				cur_z++;
			}
			for (auto nod = layman->subf ? layman->subf->next : nullptr; nod; nod = nod->next) {
				if (!nod->offs || nod->offs == global_desktop || nod->offs == Cursor::global_cursor) continue;
				auto nod_frm = static_cast<::uni::Witch::Form*>(nod->offs);
				if (!nod_frm->is_dock) {
					if (nod_frm == pfrm) {
						is_top = true;
					}
					break;
				}
			}
		}

		const char* state_str = "[NORMAL]   ";
		if (pfrm->is_dock) {
			state_str = "[DOCK]     ";
		} else if (pfrm->state == ::uni::Witch::FormState::Minimized) {
			state_str = "[MINIMIZED]";
		} else if (pfrm->state == ::uni::Witch::FormState::Maximized) {
			state_str = "[MAXIMIZED]";
		} else if (pfrm->state == ::uni::Witch::FormState::Hidden) {
			state_str = "[HIDDEN]   ";
		}

		com1.OutFormat("%[u]    %[u]      %s  ", item.pid, item.form_id, state_str);

		if (z_index >= 0) {
			if (is_top) {
				com1.OutFormat("Z=%[i] (Top) ", z_index);
			} else {
				com1.OutFormat("Z=%[i]       ", z_index);
			}
		} else {
			com1.OutFormat("-           ");
		}

		com1.OutFormat("(%[i], %[i]) %[u]x%[u] ",
			(stdsint)pfrm->sheet_area.x, (stdsint)pfrm->sheet_area.y,
			(stduint)pfrm->sheet_area.width, (stduint)pfrm->sheet_area.height);

		if (pfrm->Title.reference() && pfrm->Title.reference()[0] != '\0') {
			com1.OutFormat("%s\n\r", pfrm->Title.reference());
		} else if (pfrm->is_dock) {
			com1.OutFormat("(Dock)\n\r");
		} else {
			com1.OutFormat("(Titleless)\n\r");
		}
	}

	if (total_forms == 0) {
		com1.OutFormat("(no active forms in system)\n\r");
	}
	#else
	com1.OutFormat("GUI not enabled or non-x86 architecture.\n\r");
	#endif
}

void sysinfo_classic(OstreamTrait& com1, byte func)
{
	switch (func)
	{
	case 'f':// filesys
		com1.OutFormat("\n\r");
		com1.OutFormat("File Systems\n\r");
		dump_filesys(com1);
		com1.OutFormat("\n\r");
		Filesys::Tree(com1, false);
		break;

	case 'h': case 'H':// hardware
		com1.OutFormat("\n\r");
		if (func == 'H') {
			dump_audio_backends(com1);
			com1.OutFormat("\n\r");
			dump_driver_backends(com1);
			com1.OutFormat("\n\r");
		}
		dump_device_tree(com1, func != 'h');
		break;

	case 'l':// lock
		com1.OutFormat("\n\r");
		dump_lock(com1);
		break;
	case 'm':// mem
		com1.OutFormat("\n\r");
		mempool0.dump_available();
		break;
	case 's':// proc threads ready_queue
		com1.OutFormat("\n\r");
		void dump_processors(OstreamTrait & com1);
		dump_processors(com1);
		com1.OutFormat("\n\r");
		void dump_threads(OstreamTrait & com1);
		dump_threads(com1);
		com1.OutFormat("\n\r");
		void dump_ready_queue(OstreamTrait & com1);
		dump_ready_queue(com1);
		break;

	case 'w': case 'W':// window and display topology
		com1.OutFormat("\n\r");
		dump_screens_and_forms(com1);
		break;

	case '-':
		Coreman::Reboot();
		break;
	case '+':
		Coreman::Shutdown();
		break;

	default:
		break;
	}
}

