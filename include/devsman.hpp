#ifndef DEVSMAN_HPP_
#define DEVSMAN_HPP_

#include <c/nnode.h>

enum class DeviceNodeType : uint16 {
	SystemRoot = 1,
	BusRoot,
	PCI_Root,
	PciBus,
	PciDevice,
	StorageDevice,
	UsbBus,
	UsbRootHub,
	UsbPort,
	UsbDevice,
	UsbInterface,
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
	UsbLocation,
	UsbEndpoint,
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

enum class DriverBindingState : uint32 {
	None = 0,
	Matched,
	Probed,
	Started,
	Failed,
};

struct DriverBinding {
	const char* driver_name;
	uint32 state;
	int32 probe_result;
	void* driver_data;
};

struct DevExt {
	uint16 node_type;
	uint16 flags;

	uint16 bus_type;
	uint16 dev_class;

	uint16 vendor_id;
	uint16 device_id;
	const char* text_manufacturer;
	const char* text_product;
	const char* text_serial;

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
	using DriverStartRoutine = bool (*)(DeviceNode* node);
	static bool Initialize();
	static bool AttachPCIDevices(uni::PCI& pci);
	static void BindKnownDrivers();
	static void ProbeKnownDrivers();
	static void StartKnownDrivers();
	static void RegisterXHCIDeviceTreeHook();
	static bool RegisterDriverStarter(const char* driver_name, DriverStartRoutine starter);
	static DeviceNode* RegisterUSBBus(const char* name, const char* driver_name = nullptr, void* driver_data = nullptr);
	static DeviceNode* RegisterUSBRootHub(DeviceNode* parent, const char* name,
		uint8 class_base, uint8 class_sub, uint8 class_if,
		const char* driver_name = nullptr, void* driver_data = nullptr);
	static DeviceNode* RegisterUSBPort(DeviceNode* parent, const char* name, uint8 port_num);
	static DeviceNode* RegisterUSBDevice(DeviceNode* parent, const char* name,
		uint16 vendor_id, uint16 product_id,
		const char* text_manufacturer, const char* text_product, const char* text_serial,
		uint8 class_base, uint8 class_sub, uint8 class_if,
		uint8 port_num, uint8 slot_id,
		const char* driver_name = nullptr, void* driver_data = nullptr);
	static DeviceNode* RegisterUSBInterface(DeviceNode* parent, const char* name,
		uint8 class_base, uint8 class_sub, uint8 class_if,
		const char* driver_name = nullptr, void* driver_data = nullptr);
	static bool AddUSBEndpointResource(DeviceNode* node, uint32 index,
		uint8 endpoint_addr, uint8 transfer_type, uint16 max_packet_size, uint8 interval);
	static bool RemoveUSBDevice(DeviceNode* parent, const char* name);
	static DeviceNode* RegisterPlatformDevice(const char* name,
		const char* driver_name = nullptr, void* driver_data = nullptr);
	static DeviceNode* RegisterPlatformDevice(DeviceNode* parent, const char* name,
		const char* driver_name = nullptr, void* driver_data = nullptr);
	static DeviceNode* RegisterStorageDevice(DeviceNode* parent, const char* name,
		DeviceBusType bus_type = DeviceBusType::Platform,
		const char* driver_name = nullptr, void* driver_data = nullptr);
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
