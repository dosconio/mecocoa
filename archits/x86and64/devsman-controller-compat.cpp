// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Architecture] x86 Controller Compatibility Descriptions
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"

#if (_MCCA & 0xFF00) == 0x8600

namespace {
	struct PciControllerCompatibilityEntry {
		uint16 vendor_id;
		uint16 device_id;
		uint16 subsystem_vendor_id;
		uint16 subsystem_vendor_mask;
		uint16 subsystem_device_id;
		uint16 subsystem_device_mask;
		uint8 revision;
		uint8 revision_mask;
		const DeviceControllerCompatibilityDescription* description;
	};

	constexpr DeviceControllerCompatibilityDescription intel_lpss_i2c_description(const char* family_name, const char* product_name, uint32 sda_hold_time_ns, uint32 evidence_flags) {
		DeviceControllerCompatibilityDescription description{};
		description.family_name = family_name;
		description.product_name = product_name;
		description.driver_name = "intel-lpss-i2c";
		description.ip_type = DeviceControllerIpType::DesignWareI2c;
		description.clock_model = DeviceControllerClockModel::Fixed;
		description.limitation_flags = DeviceControllerCompatibilityLimitation_DriverInterruptUnsupported | DeviceControllerCompatibilityLimitation_DriverDmaUnsupported;
		description.evidence_flags = evidence_flags;
		description.input_clock_hz = 133000000u;
		description.minimum_access_length = 0x300u;
		description.core_register_offset = 0x000u;
		description.private_register_offset = 0x200u;
		description.capabilities_register_offset = description.private_register_offset + 0xFCu;
		description.capabilities_type_mask = 0xF0u;
		description.capabilities_type_value = 0x00u;
		description.reset_register_offset = description.private_register_offset + 0x04u;
		description.reset_mask = 0x07u;
		description.reset_deassert_value = 0x07u;
		description.remap_address_low_offset = description.private_register_offset + 0x40u;
		description.remap_address_high_offset = description.private_register_offset + 0x44u;
		description.i2c_sda_hold_time_ns = sda_hold_time_ns;
		description.i2c_sda_falling_time_ns = 171u;
		description.i2c_scl_falling_time_ns = 208u;
		description.access_index = 0;
		return description;
	}

	// Linux intel-lpss-pci maps Broxton A/B-step IDs to bxt_i2c_info.
	constexpr auto BroxtonLpssI2c = intel_lpss_i2c_description("broxton", "Broxton LPSS I2C controller", 42u, DeviceControllerCompatibilityEvidence_PublicReference);
	// Linux intel-lpss-pci maps Apollo Lake IDs to apl_i2c_info; Mecocoa also validated this layout on physical hardware.
	constexpr auto ApolloLakeLpssI2c = intel_lpss_i2c_description("apollo-lake", "Apollo Lake LPSS I2C controller", 207u, DeviceControllerCompatibilityEvidence_PublicReference | DeviceControllerCompatibilityEvidence_RegisterModel | DeviceControllerCompatibilityEvidence_PhysicalHardware);

	constexpr PciControllerCompatibilityEntry pci_controller_compatibility_table[] = {
		{0x8086u, 0x0AACu, 0, 0, 0, 0, 0, 0, &BroxtonLpssI2c},
		{0x8086u, 0x0AAEu, 0, 0, 0, 0, 0, 0, &BroxtonLpssI2c},
		{0x8086u, 0x0AB0u, 0, 0, 0, 0, 0, 0, &BroxtonLpssI2c},
		{0x8086u, 0x0AB2u, 0, 0, 0, 0, 0, 0, &BroxtonLpssI2c},
		{0x8086u, 0x0AB4u, 0, 0, 0, 0, 0, 0, &BroxtonLpssI2c},
		{0x8086u, 0x0AB6u, 0, 0, 0, 0, 0, 0, &BroxtonLpssI2c},
		{0x8086u, 0x0AB8u, 0, 0, 0, 0, 0, 0, &BroxtonLpssI2c},
		{0x8086u, 0x0ABAu, 0, 0, 0, 0, 0, 0, &BroxtonLpssI2c},
		{0x8086u, 0x1AACu, 0, 0, 0, 0, 0, 0, &BroxtonLpssI2c},
		{0x8086u, 0x1AAEu, 0, 0, 0, 0, 0, 0, &BroxtonLpssI2c},
		{0x8086u, 0x1AB0u, 0, 0, 0, 0, 0, 0, &BroxtonLpssI2c},
		{0x8086u, 0x1AB2u, 0, 0, 0, 0, 0, 0, &BroxtonLpssI2c},
		{0x8086u, 0x1AB4u, 0, 0, 0, 0, 0, 0, &BroxtonLpssI2c},
		{0x8086u, 0x1AB6u, 0, 0, 0, 0, 0, 0, &BroxtonLpssI2c},
		{0x8086u, 0x1AB8u, 0, 0, 0, 0, 0, 0, &BroxtonLpssI2c},
		{0x8086u, 0x1ABAu, 0, 0, 0, 0, 0, 0, &BroxtonLpssI2c},
		{0x8086u, 0x5AACu, 0, 0, 0, 0, 0, 0, &ApolloLakeLpssI2c},
		{0x8086u, 0x5AAEu, 0, 0, 0, 0, 0, 0, &ApolloLakeLpssI2c},
		{0x8086u, 0x5AB0u, 0, 0, 0, 0, 0, 0, &ApolloLakeLpssI2c},
		{0x8086u, 0x5AB2u, 0, 0, 0, 0, 0, 0, &ApolloLakeLpssI2c},
		{0x8086u, 0x5AB4u, 0, 0, 0, 0, 0, 0, &ApolloLakeLpssI2c},
		{0x8086u, 0x5AB6u, 0, 0, 0, 0, 0, 0, &ApolloLakeLpssI2c},
		{0x8086u, 0x5AB8u, 0, 0, 0, 0, 0, 0, &ApolloLakeLpssI2c},
		{0x8086u, 0x5ABAu, 0, 0, 0, 0, 0, 0, &ApolloLakeLpssI2c},
	};

	bool pci_controller_compatibility_mask(uint16 value, uint16 expected, uint16 mask) {
		return !mask || (value & mask) == (expected & mask);
	}

	const DeviceControllerCompatibilityDescription* find_pci_controller_compatibility(uint16 vendor_id, uint16 device_id, bool match_platform_identity, uint16 subsystem_vendor_id, uint16 subsystem_device_id, uint8 revision) {
		const PciControllerCompatibilityEntry* best = nullptr;
		uint8 best_specificity = 0;
		for (const auto& entry : pci_controller_compatibility_table) {
			if (entry.vendor_id != vendor_id || entry.device_id != device_id) continue;
			if (!match_platform_identity && (entry.subsystem_vendor_mask || entry.subsystem_device_mask || entry.revision_mask)) continue;
			if (!pci_controller_compatibility_mask(subsystem_vendor_id, entry.subsystem_vendor_id, entry.subsystem_vendor_mask)) continue;
			if (!pci_controller_compatibility_mask(subsystem_device_id, entry.subsystem_device_id, entry.subsystem_device_mask)) continue;
			if (entry.revision_mask && (revision & entry.revision_mask) != (entry.revision & entry.revision_mask)) continue;
			const uint8 specificity = uint8(2u + !!entry.subsystem_vendor_mask + !!entry.subsystem_device_mask + !!entry.revision_mask);
			if (!best || specificity > best_specificity) {
				best = &entry;
				best_specificity = specificity;
			}
		}
		return best ? best->description : nullptr;
	}
}

const DeviceControllerCompatibilityDescription* Devsman::FindPciControllerCompatibility(uint16 vendor_id, uint16 device_id) {
	return find_pci_controller_compatibility(vendor_id, device_id, false, 0, 0, 0);
}

const DeviceControllerCompatibilityDescription* Devsman::FindPciControllerCompatibility(uint16 vendor_id, uint16 device_id, uint16 subsystem_vendor_id, uint16 subsystem_device_id, uint8 revision) {
	return find_pci_controller_compatibility(vendor_id, device_id, true, subsystem_vendor_id, subsystem_device_id, revision);
}

const DeviceControllerCompatibilityDescription* Devsman::FindControllerCompatibility(const DeviceNode* node) {
	if (!node || DeviceNodeType(node->fields.node_type) != DeviceNodeType::PciDevice) return nullptr;
	return FindPciControllerCompatibility(node->fields.vendor_id, node->fields.device_id, node->fields.subsystem_vendor_id, node->fields.subsystem_device_id, node->fields.revision);
}

#endif
