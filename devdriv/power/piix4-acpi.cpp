// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: Intel PIIX4 ACPI
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/mecocoa.hpp"

#if (_MCCA & 0xFF00) == 0x8600

namespace Devs {
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
		node->fields.binding.probe_result = 0;
		ploginfo("[PIIX4-ACPI] Probed %s revision=%[8H]",
			node->link.addr ? node->link.addr : "(unnamed)",
			node->fields.revision);
		return true;
	}
}

#endif
