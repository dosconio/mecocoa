#ifndef DEVSMAN_HPP_
#define DEVSMAN_HPP_

#include <c/nnode.h>

enum class DeviceNodeType : uint16 {
	SystemRoot = 1,
	PCI_Root,
	PciBus,
	PciDevice,
};

enum class DeviceBusType : uint16 {
	None = 0,
	PCI,
};

struct DeviceResource {
	uint16 type;
	uint16 flags;
	uint32 index;
	uint64 start;
	uint64 length;
	uint64 extra;
};

struct DriverBinding {
	const char* driver_name;
	uint32 state;
	void* driver_data;
};

struct DevExt {
	uint16 node_type;
	uint16 flags;

	uint16 bus_type;
	uint16 dev_class;

	uint16 vendor_id;
	uint16 device_id;

	uint8 revision;
	uint8 class_base;
	uint8 class_sub;
	uint8 class_if;

	uint16 pci_segment;
	uint8 pci_bus;
	uint8 pci_device;
	uint8 pci_function;

	uint16 resource_count;
	uint16 resource_capacity;
	DeviceResource* resources;

	void* acpi_handle;
	DriverBinding binding;
};

struct DeviceNode {
	Nnode link;
	DevExt fields;
};

namespace uni {
	class PCI;
}

class Devsman {
public:
	static bool Initialize();
	static bool AttachPCIDevices(uni::PCI& pci);
	static DeviceNode* Root();
	static DeviceNode* PCI_Root();
	static DeviceNode* PrimaryPciBus();

};


#endif /* DEVSMAN_HPP_ */
