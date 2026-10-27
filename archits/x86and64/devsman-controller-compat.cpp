// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Architecture] x86 Controller Compatibility Descriptions
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"

#if (_MCCA & 0xFF00) == 0x8600
#include "../../../unisym/lib/cpp/Device/GPIO/GPIO-Intel.hpp"

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

	struct AcpiGPIOControllerCompatibilityEntry {
		const char* hardware_id;
		const char* compatible_id;
		const char* unique_id;
		const DeviceGPIOControllerCompatibilityDescription* description;
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

	constexpr uni::IntelGPIOCommunityDescription intel_broxton_GPIO_community(uint16 pin_count) {
		// The fixed register block ends after the per-group GPI_IE array; PADBAR supplies the separately bounded pad-register base.
		const uint32 group_count = (uint32(pin_count) + 31u) / 32u;
		return { pin_count, 32u, 4u, 8u, 0x110u + group_count * uint32(sizeof(uint32)), 0x000u, 0x00Cu, 0x020u, 0x060u, 0x080u, 0x100u, 0x110u };
	}

	constexpr auto ApolloLakeGPIONorthLayout = intel_broxton_GPIO_community(78u);
	constexpr auto ApolloLakeGPIONorthwestLayout = intel_broxton_GPIO_community(77u);
	constexpr auto ApolloLakeGPIOWestLayout = intel_broxton_GPIO_community(47u);
	constexpr auto ApolloLakeGPIOSouthwestLayout = intel_broxton_GPIO_community(43u);

	constexpr DeviceGPIOControllerCompatibilityDescription intel_apollo_lake_GPIO_description(const char* product_name, const uni::IntelGPIOCommunityDescription& layout) {
		return { "apollo-lake", product_name, "intel-gpio-community", &layout, DeviceControllerCompatibilityEvidence_PublicReference | DeviceControllerCompatibilityEvidence_RegisterModel | DeviceControllerCompatibilityEvidence_FirmwareReplay | DeviceControllerCompatibilityEvidence_PhysicalHardware, layout.minimum_access_length, 0u };
	}

	// ACPI Specification 6.5 section 6.1.12 makes _UID the persistent device differentiator.
	// Linux pinctrl-broxton.c maps ACPI INT3452 _UID 1..4 to these Apollo Lake communities.
	constexpr auto ApolloLakeGPIONorth = intel_apollo_lake_GPIO_description("Apollo Lake GPIO North community", ApolloLakeGPIONorthLayout);
	constexpr auto ApolloLakeGPIONorthwest = intel_apollo_lake_GPIO_description("Apollo Lake GPIO Northwest community", ApolloLakeGPIONorthwestLayout);
	constexpr auto ApolloLakeGPIOWest = intel_apollo_lake_GPIO_description("Apollo Lake GPIO West community", ApolloLakeGPIOWestLayout);
	constexpr auto ApolloLakeGPIOSouthwest = intel_apollo_lake_GPIO_description("Apollo Lake GPIO Southwest community", ApolloLakeGPIOSouthwestLayout);

	constexpr AcpiGPIOControllerCompatibilityEntry acpi_GPIO_controller_compatibility_table[] = {
		{"INT3452", "INT3452", "1", &ApolloLakeGPIONorth},
		{"INT3452", "INT3452", "2", &ApolloLakeGPIONorthwest},
		{"INT3452", "INT3452", "3", &ApolloLakeGPIOWest},
		{"INT3452", "INT3452", "4", &ApolloLakeGPIOSouthwest},
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

	bool acpi_compatible_id_matches(const char* ids, const char* expected) {
		if (!ids || !expected) return false;
		const stduint expected_length = StrLength(expected);
		const char* cursor = ids;
		while (*cursor) {
			const char* end = cursor;
			while (*end && *end != ',') ++end;
			if (stduint(end - cursor) == expected_length && StrCompareN(cursor, expected, expected_length) == 0) return true;
			cursor = *end ? end + 1 : end;
		}
		return false;
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

const DeviceGPIOControllerCompatibilityDescription* Devsman::FindGPIOControllerCompatibility(const DeviceNode* node) {
	if (!node || DeviceNodeType(node->fields.node_type) != DeviceNodeType::PlatformDevice || !node->fields.acpi_info) return nullptr;
	const auto& acpi = *node->fields.acpi_info;
	if (!(acpi.flags & DeviceAcpiInfoFlag_HasUniqueId)) return nullptr;
	for (const auto& entry : acpi_GPIO_controller_compatibility_table) {
		const bool hardware_id_matches = (acpi.flags & DeviceAcpiInfoFlag_HasHardwareId) && StrCompare(acpi.hardware_id, entry.hardware_id) == 0;
		const bool compatible_id_matches = (acpi.flags & DeviceAcpiInfoFlag_HasCompatibleId) && acpi_compatible_id_matches(acpi.compatible_ids, entry.compatible_id);
		if ((hardware_id_matches || compatible_id_matches) && StrCompare(acpi.unique_id, entry.unique_id) == 0) return entry.description;
	}
	return nullptr;
}

#endif
