// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: Intel PIIX4 ACPI
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"
#include <cpp/Device/Bus/PCI.hpp>

#if (_MCCA & 0xFF00) == 0x8600

namespace Devs {
	constexpr uint8 Piix4PciCommandOffset = 0x04u;
	constexpr uint8 Piix4PmBaseOffset = 0x40u;
	constexpr uint8 Piix4PmMiscOffset = 0x80u;
	constexpr uint8 Piix4SmbusBaseOffset = 0x90u;
	constexpr uint8 Piix4SmbusHostConfigDwordOffset = 0xD0u;
	constexpr uint32 Piix4PciCommandIoEnable = 1u << 0;
	constexpr uint32 Piix4IoBaseResourceIndicator = 1u << 0;
	constexpr uint32 Piix4PmBaseMask = 0x0000FFC0u;
	constexpr uint32 Piix4PmIoSpaceEnable = 1u << 0;
	constexpr uint32 Piix4SmbusBaseMask = 0x0000FFF0u;
	constexpr uint32 Piix4SmbusHostInterfaceEnable = 1u << 0;
	constexpr uint64 Piix4PmIoLength = 0x40u;
	constexpr uint64 Piix4SmbusIoLength = 0x10u;

	uint32 read_piix4_config(const DeviceNode& node, uint8 offset) {
		return uni::PCI::read_config_register(
			node.fields.pci_bus,
			node.fields.pci_device,
			node.fields.pci_function,
			offset);
	}

	bool probe_piix4_acpi_device(DeviceNode* node) {
		if (!node) return false;
		if (DeviceNodeType(node->fields.node_type) != DeviceNodeType::PciDevice) return false;
		if (node->fields.vendor_id != 0x8086u || node->fields.device_id != 0x7113u) return false;
		if (node->fields.class_base != 0x06u ||
			node->fields.class_sub != 0x80u ||
			node->fields.class_if != 0x00u) {
			plogwarn("[PIIX4-ACPI] %s has unexpected PCI class %[8H].%[8H].%[8H]",
				node->link.addr ? node->link.addr : "(unnamed)",
				node->fields.class_base,
				node->fields.class_sub,
				node->fields.class_if);
			return false;
		}

		const uint32 pci_command = read_piix4_config(*node, Piix4PciCommandOffset);
		const uint32 pm_base_register = read_piix4_config(*node, Piix4PmBaseOffset);
		const uint32 pm_misc = read_piix4_config(*node, Piix4PmMiscOffset);
		const uint32 smbus_base_register = read_piix4_config(*node, Piix4SmbusBaseOffset);
		const uint32 smbus_host_config_dword = read_piix4_config(*node, Piix4SmbusHostConfigDwordOffset);
		const uint16 pm_base = uint16(pm_base_register & Piix4PmBaseMask);
		const uint16 smbus_base = uint16(smbus_base_register & Piix4SmbusBaseMask);
		const bool pm_base_valid = pm_base &&
			(pm_base_register & Piix4IoBaseResourceIndicator);
		const bool pm_io_enabled = pm_base_valid && (pm_misc & Piix4PmIoSpaceEnable);
		const bool pci_io_enabled = pci_command & Piix4PciCommandIoEnable;
		const uint8 smbus_host_config = uint8(smbus_host_config_dword >> 16);
		const bool smbus_base_valid = smbus_base &&
			(smbus_base_register & Piix4IoBaseResourceIndicator);
		const bool smbus_host_enabled = smbus_host_config & Piix4SmbusHostInterfaceEnable;
		const bool smbus_io_enabled = smbus_base_valid && pci_io_enabled && smbus_host_enabled;

		if (pm_io_enabled &&
			!Devsman::AddIoPortResource(node, 0, pm_base, Piix4PmIoLength)) {
			plogwarn("[PIIX4-ACPI] Failed to publish PM I/O resource");
			return false;
		}
		if (smbus_io_enabled &&
			!Devsman::AddIoPortResource(node, 1, smbus_base, Piix4SmbusIoLength)) {
			plogwarn("[PIIX4-ACPI] Failed to publish SMBus I/O resource");
			return false;
		}
		node->fields.binding.probe_result = 0;
		ploginfo("[PIIX4-ACPI] Probed %s revision=%[8H] PMIO=%[16H] active=%u SMBIO=%[16H] pci-io=%u host=%u active=%u",
			node->link.addr ? node->link.addr : "(unnamed)",
			node->fields.revision,
			pm_base,
			pm_io_enabled ? 1u : 0u,
			smbus_base,
			pci_io_enabled ? 1u : 0u,
			smbus_host_enabled ? 1u : 0u,
			smbus_io_enabled ? 1u : 0u);
		return true;
	}
}

#endif
