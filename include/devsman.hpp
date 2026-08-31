#ifndef DEVSMAN_HPP_
#define DEVSMAN_HPP_

#include <c/nnode.h>
#include <cpp/System/Audiosys.hpp>

struct DeviceNode;

struct DeviceNodeOps {
	stdsint (*read)(DeviceNode* node, void* buf, stduint count, stduint idx, stduint flags);
	stdsint (*send)(DeviceNode* node, const void* buf, stduint count, stduint idx, stduint flags);
	stdsint (*ctrl)(DeviceNode* node, stduint cmd, void* args, stduint flags);
};

struct PwcallDeviceHandleEntry {
	DeviceNode* node = nullptr;
	uint32 flags = 0;

	bool operator==(const PwcallDeviceHandleEntry& other) const {
		return node == other.node && flags == other.flags;
	}
};

enum class DeviceCtrlCommand : uint32 {
	None = 0,
	GetBlockSize,
	GetUnitCount,
	GetByteSize,
	GetBackingObject,
};

enum class VideoCtrlCommand : uint32 {
	Base = uint32(DeviceCtrlCommand::GetBackingObject),
	GetFramebufferInfo,
	SetVideoMode,
};

enum class DeviceNodeType : uint16 {
	SystemRoot = 1,
	BusRoot,
	PCI_Root,
	PciBus,
	PciDevice,
	IsaBus,
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
	ISA,
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
	DmaChannel,
};

enum DeviceResourceFlags : uint16 {
	DeviceResourceFlag_None = 0,
	DeviceResourceFlag_Prefetchable = 1 << 0,
	DeviceResourceFlag_Bar64 = 1 << 1,
	DeviceResourceFlag_SizeEstimated = 1 << 2,
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
	const DeviceNodeOps* ops;

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
	class StorageTrait;
	class DiscPartition;
	namespace Network {
		struct IPv4Address;
		class LinkDevice;
		struct MacAddress;
		struct UDPDatagramContext;
	}
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
	static DeviceNode* RegisterSerioController(DeviceNode* parent, const char* name);
	static DeviceNode* RegisterSerioDevice(DeviceNode* parent, const char* name);
	static bool AddIoPortResource(DeviceNode* node, uint32 index, uint64 base, uint64 length);
	static bool AddIrqResource(DeviceNode* node, uint64 vector, uint64 pin = 0);
	static bool AddDmaResource(DeviceNode* node, uint32 index, uint8 channel, uint8 width_bits);
	static DeviceNode* Root();
	static DeviceNode* PCI_Root();
	static DeviceNode* PrimaryPciBus();
	static DeviceNode* FindNamedNode(DeviceNodeType node_type, const char* name);
	static DeviceNode* FindPCIDeviceByClass(uint8 class_base, uint8 class_sub, uint8 class_if);
	static DeviceNode* FindPCIDeviceByVendorDevice(uint16 vendor_id, uint16 device_id);
	static const DeviceResource* FindResource(const DeviceNode* node, DeviceResourceType type, uint32 index = 0);
	static bool SetOps(DeviceNode* node, const DeviceNodeOps* ops);
	static const DeviceNodeOps* GetOps(const DeviceNode* node);
	static stdsint Read(DeviceNode* node, void* buf, stduint count, stduint idx = 0, stduint flags = 0);
	static stdsint Send(DeviceNode* node, const void* buf, stduint count, stduint idx = 0, stduint flags = 0);
	static stdsint Ctrl(DeviceNode* node, stduint cmd, void* args, stduint flags = 0);
	static bool AttachStorageOps(DeviceNode* node, uni::StorageTrait* storage);
	static DeviceNode* RegisterStoragePartition(DeviceNode* parent, const char* name,
		uni::StorageTrait& storage, stdsint part_dev, const char* driver_name = "storage-partition");
	#if (_MCCA & 0xFF00) == 0x8600
	static bool RegisterLinkDevice(uni::Network::LinkDevice* device);
	static stduint LinkDeviceCount();
	static uni::Network::LinkDevice* GetLinkDevice(stduint index);
	static bool OpenUdpPort(uint16 port);
	static bool BindUdpPort(uint16 port);
	static bool AllocateUdpPort(uint16& port);
	static bool CloseUdpPort(uint16 port);
	static bool WaitUdp(uint16 port);
	static stdsint ReceiveUdp(uint16 port, uni::Network::UDPDatagramContext& context, void* payload, stduint capacity);
	static stdsint SendUdp(const uni::Network::IPv4Address& target_ip,
		uint16 source_port, uint16 destination_port, const void* payload, stduint length);
	static stdsint SendUdp(const uni::Network::MacAddress& target_mac, const uni::Network::IPv4Address& target_ip,
		uint16 source_port, uint16 destination_port, const void* payload, stduint length);
	static bool GetDefaultIPv4Route(void* route, stduint length);
	static bool GetIPv4Interface(stduint index, void* iface, stduint length);
	static stduint IPv4ArpCacheCount();
	static bool GetIPv4ArpCacheEntry(stduint index, void* entry, stduint length);
	static const char* LookupPciClassName(uint8 class_base, uint8 class_sub, uint8 class_if);
	static const char* LookupPciDeviceName(uint16 vendor_id, uint16 device_id, uint8 class_base = 0, uint8 class_sub = 0);
	static const char* LookupPciVendorName(uint16 vendor_id);
	#endif

};

// ---- AUDIO ----

enum class AudioMsg : stduint {
	TEST,
	PLAY_PCM_U8_MONO,
	STREAM_BEGIN,
	STREAM_WRITE,
	STREAM_DRAIN,
	STREAM_STOP,
};

// Submit a synchronous PCM playback request to the audio service.
bool AudioPlay(const uni::AudioPlayRequest& request);

// Parse a RIFF/WAVE blob and submit the decoded PCM view to the audio service.
bool AudioPlayWav(const void* wav_data, uint32 wav_size);

// Load a RIFF/WAVE file from VFS and submit the decoded PCM view to the audio service.
bool AudioPlayWavFile(const char* path);

// Submit a synchronous U8 mono PCM playback request to the audio service.
bool SoundBlasterPlayPcmU8Mono(const uint8* data, uint32 byte_count,
	uint16 sample_rate = 11025);


#endif /* DEVSMAN_HPP_ */
