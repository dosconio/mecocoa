#ifndef DEVSMAN_HPP_
#define DEVSMAN_HPP_

#include <c/nnode.h>

enum class DeviceNodeType : uint16 {
	SystemRoot = 1,
	BusRoot,
	PCI_Root,
	PciBus,
	PciDevice,
	PlatformDevice,
	SerioController,
	SerioDevice,
};

enum class DeviceBusType : uint16 {
	None = 0,
	PCI,
	USB,
	Platform,
	I2C,
	SPI,
	Serio,
	Virtio,
};

enum class DeviceResourceType : uint16 {
	None = 0,
	PciBarMmio,
	PciBarIo,
	IoPortRange,
	IrqLine,
	PciBridgeBusRange,
};

enum DeviceResourceFlags : uint16 {
	DeviceResourceFlag_None = 0,
	DeviceResourceFlag_Prefetchable = 1 << 0,
	DeviceResourceFlag_Bar64 = 1 << 1,
};

constexpr uint16 DeviceNodeInlineResourceCapacity = 8;

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
	DeviceResource inline_resources[DeviceNodeInlineResourceCapacity];
};

namespace uni {
	class PCI;
}

class Devsman {
public:
	static bool Initialize();
	static bool AttachPCIDevices(uni::PCI& pci);
	static DeviceNode* RegisterPlatformDevice(const char* name);
	static DeviceNode* RegisterSerioController(const char* name);
	static DeviceNode* RegisterSerioDevice(DeviceNode* parent, const char* name);
	static bool AddIoPortResource(DeviceNode* node, uint32 index, uint64 base, uint64 length);
	static bool AddIrqResource(DeviceNode* node, uint64 vector, uint64 pin = 0);
	static DeviceNode* Root();
	static DeviceNode* PCI_Root();
	static DeviceNode* PrimaryPciBus();
	static DeviceNode* FindPCIDeviceByClass(uint8 class_base, uint8 class_sub, uint8 class_if);
	static const DeviceResource* FindResource(const DeviceNode* node, DeviceResourceType type, uint32 index = 0);

};


#endif /* DEVSMAN_HPP_ */
