#ifndef DEVSMAN_HPP_
#define DEVSMAN_HPP_

#include <c/nnode.h>
#include <cpp/System/Audiosys.hpp>
#include <cpp/Device/Audio/SoundBlaster.hpp>
#include <cpp/System/Audiosys/AudioManager.hpp>

extern uni::AudioManager audio_manager;

struct DeviceNode;
struct DeviceEvent;

namespace uni {
	enum class IICTransferStatus : uint8;
	struct IICMessage;
	struct IICTransferResult;
}

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

enum class I2cCtrlCommand : uint32 {
	Base = uint32(DeviceCtrlCommand::GetBackingObject),
	Transfer,
};

struct DeviceI2cTransferRequest {
	uni::IICMessage* messages;
	stduint message_count;
	stduint timeout;
	uni::IICTransferResult* result;
	uni::IICTransferStatus status;
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
	I2cBus,
	I2cDevice,
	SpiBus,
	SpiDevice,
	UartBus,
	UartDevice,
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
	UART,
	Count,
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
	DeviceResourceFlag_IrqLevel = 1 << 3,
	DeviceResourceFlag_IrqActiveLow = 1 << 4,
	DeviceResourceFlag_IrqShareable = 1 << 5,
};

constexpr uint16 DeviceNodeInlineResourceCapacity = 8;
constexpr stduint DeviceAliasNameCapacity = 64;
constexpr stduint DeviceAcpiPathCapacity = 160;
constexpr stduint DeviceAcpiIdCapacity = 48;
constexpr stduint DeviceAcpiUidCapacity = 32;

struct DeviceResource {
	uint16 type;
	uint16 flags;
	uint32 index;
	uint64 start;
	uint64 length;
	uint64 extra;
};

enum DeviceAcpiInfoFlags : uint32 {
	DeviceAcpiInfoFlag_None = 0,
	DeviceAcpiInfoFlag_HasHardwareId = 1 << 0,
	DeviceAcpiInfoFlag_HasCompatibleId = 1 << 1,
	DeviceAcpiInfoFlag_HasUniqueId = 1 << 2,
	DeviceAcpiInfoFlag_HasStatus = 1 << 3,
	DeviceAcpiInfoFlag_HasAddress = 1 << 4,
	DeviceAcpiInfoFlag_Conditional = 1 << 5,
};

enum class DeviceAcpiSerialBusType : uint8 {
	I2C = 1,
	SPI = 2,
	UART = 3,
	CSI2 = 4,
};

enum class DeviceAcpiGpioType : uint8 {
	Interrupt,
	Io,
};

enum class DeviceAcpiRangeType : uint8 {
	Memory,
	IoPort,
};

struct DeviceAcpiSerialResource {
	char controller_path[DeviceAcpiPathCapacity];
	uint32 speed;
	uint16 address;
	uint16 receive_fifo;
	uint16 transmit_fifo;
	uint16 type_flags;
	DeviceAcpiSerialBusType bus_type;
	uint8 general_flags;
	uint8 data_bits;
	uint8 clock_phase;
	uint8 clock_polarity;
	uint8 parity;
	uint8 lines_enabled;
};

struct DeviceAcpiGpioResource {
	char controller_path[DeviceAcpiPathCapacity];
	uint32 pin;
	uint32 flags;
	DeviceAcpiGpioType type;
	uint8 pin_config;
};

struct DeviceAcpiIrqResource {
	uint32 interrupt;
	uint32 flags;
};

struct DeviceAcpiRangeResource {
	uint64 minimum;
	uint64 maximum;
	uint64 length;
	uint64 granularity;
	uint64 translation;
	uint32 flags;
	DeviceAcpiRangeType type;
	uint8 width;
};

enum class DeviceAcpiPropertyType : uint8 {
	Integer,
	String,
	Buffer,
	Array,
	DeviceReference,
};

enum DeviceAcpiPropertyFlags : uint8 {
	DeviceAcpiPropertyFlag_None = 0,
	DeviceAcpiPropertyFlag_NonStandard = 1 << 0,
};

enum class DeviceAcpiPropertyStatus : uint8 {
	NotEvaluated,
	Success,
	NotFound,
	DevicePropertiesNotFound,
	Malformed,
	ExecutionFailed,
	AllocationFailed,
};

struct DeviceAcpiPropertyValue {
	const uint8* data;
	const DeviceAcpiPropertyValue* elements;
	const char* reference_path;
	uint64 integer;
	uint32 length;
	uint16 element_count;
	DeviceAcpiPropertyType type;
	uint8 flags;
};

struct DeviceAcpiProperty {
	const char* name;
	DeviceAcpiPropertyValue value;
};

struct DeviceAcpiInfo {
	char path[DeviceAcpiPathCapacity];
	char hardware_id[DeviceAcpiIdCapacity];
	char compatible_ids[DeviceAcpiIdCapacity];
	char unique_id[DeviceAcpiUidCapacity];
	uint64 status;
	uint64 address;
	uint32 flags;
	uint16 serial_resource_count;
	uint16 gpio_resource_count;
	uint16 irq_resource_count;
	uint16 range_resource_count;
	uint16 property_count;
	uint16 property_value_count;
	uint16 nonstandard_property_count;
	uint32 property_data_length;
	DeviceAcpiPropertyStatus property_status;
	// These arrays are owned together with this heap-allocated record.
	DeviceAcpiSerialResource* serial_resources;
	DeviceAcpiGpioResource* gpio_resources;
	DeviceAcpiIrqResource* irq_resources;
	DeviceAcpiRangeResource* range_resources;
	DeviceAcpiProperty* properties;
	DeviceAcpiPropertyValue* property_values;
	uint8* property_data;
};

enum class DeviceControllerResourceSource : uint8 {
	None = 0,
	AcpiResource,
	AcpiDsd,
	AcpiDsm,
	PciConfiguration,
	PlatformDescription,
	HardwareCapability,
	FirmwareConfigured,
	DriverPolicy,
};

enum class DeviceControllerAccessType : uint8 {
	None = 0,
	MMIO,
	PortIo,
};

enum class DeviceControllerInterruptMode : uint8 {
	None = 0,
	IRQ,
	Polling,
};

enum class DeviceControllerResetType : uint8 {
	None = 0,
	Register,
	FirmwareConfigured,
};

enum class DeviceControllerPowerState : uint8 {
	Unknown = 0,
	On,
	FirmwareConfigured,
};

enum class DeviceControllerIdentityType : uint8 {
	None = 0,
	Pci,
	Platform,
};

enum class DeviceControllerDmaMode : uint8 {
	Unknown = 0,
	NotRequired,
	Supported,
	Required,
};

enum class DeviceControllerContractStatus : uint8 {
	NotEvaluated = 0,
	Ready,
	MissingAccess,
	MissingInterrupt,
	MissingClock,
	MissingReset,
	MissingPower,
	MissingNamespace,
	MissingIdentity,
	MissingDma,
};

enum DeviceControllerRequirementFlags : uint32 {
	DeviceControllerRequirement_None = 0,
	DeviceControllerRequirement_Access = 1 << 0,
	DeviceControllerRequirement_Interrupt = 1 << 1,
	DeviceControllerRequirement_Clock = 1 << 2,
	DeviceControllerRequirement_Reset = 1 << 3,
	DeviceControllerRequirement_Power = 1 << 4,
	DeviceControllerRequirement_Namespace = 1 << 5,
	DeviceControllerRequirement_Identity = 1 << 6,
	DeviceControllerRequirement_Dma = 1 << 7,
};

enum class DeviceControllerIpType : uint8 {
	Unknown = 0,
	DesignWareI2c,
};

enum class DeviceControllerClockModel : uint8 {
	Unknown = 0,
	Fixed,
};

enum DeviceControllerCompatibilityLimitationFlags : uint32 {
	DeviceControllerCompatibilityLimitation_None = 0,
	DeviceControllerCompatibilityLimitation_DriverInterruptUnsupported = 1 << 0,
	DeviceControllerCompatibilityLimitation_DriverDmaUnsupported = 1 << 1,
};

enum DeviceControllerCompatibilityEvidenceFlags : uint32 {
	DeviceControllerCompatibilityEvidence_None = 0,
	DeviceControllerCompatibilityEvidence_PublicReference = 1 << 0,
	DeviceControllerCompatibilityEvidence_RegisterModel = 1 << 1,
	DeviceControllerCompatibilityEvidence_FirmwareReplay = 1 << 2,
	DeviceControllerCompatibilityEvidence_PhysicalHardware = 1 << 3,
};

struct DeviceControllerCompatibilityDescription {
	const char* family_name;
	const char* product_name;
	const char* driver_name;
	DeviceControllerIpType ip_type;
	DeviceControllerClockModel clock_model;
	uint32 limitation_flags;
	uint32 evidence_flags;
	uint32 input_clock_hz;
	uint32 minimum_access_length;
	uint32 core_register_offset;
	uint32 private_register_offset;
	uint32 capabilities_register_offset;
	uint32 capabilities_type_mask;
	uint32 capabilities_type_value;
	uint32 reset_register_offset;
	uint32 reset_mask;
	uint32 reset_deassert_value;
	uint32 remap_address_low_offset;
	uint32 remap_address_high_offset;
	uint32 i2c_sda_hold_time_ns;
	uint32 i2c_sda_falling_time_ns;
	uint32 i2c_scl_falling_time_ns;
	uint8 access_index;
};

struct DeviceControllerResourceContract {
	uint32 requirements;
	DeviceControllerContractStatus status;
	DeviceControllerAccessType access_type;
	DeviceControllerResourceSource access_source;
	uint32 access_index;
	uint64 access_base;
	uint64 access_length;
	DeviceControllerInterruptMode interrupt_mode;
	DeviceControllerResourceSource interrupt_source;
	uint32 interrupt;
	DeviceControllerResourceSource clock_source;
	uint32 input_clock_hz;
	DeviceControllerResetType reset_type;
	DeviceControllerResourceSource reset_source;
	uint32 reset_register_offset;
	uint32 reset_mask;
	uint32 reset_deassert_value;
	DeviceControllerPowerState power_state;
	DeviceControllerResourceSource power_source;
	DeviceControllerIdentityType identity_type;
	DeviceControllerResourceSource identity_source;
	uint16 pci_segment;
	uint8 pci_bus;
	uint8 pci_device;
	uint8 pci_function;
	const char* platform_identity;
	const char* acpi_path;
	DeviceControllerDmaMode dma_mode;
	DeviceControllerResourceSource dma_source;
};

enum class DriverBindingState : uint32 {
	None = 0,
	Matched,
	Probed,
	Started,
	Failed,
};

enum class DriverMatchResult : uint16 {
	NotEvaluated = 0,
	Unsupported,
	Matched,
	MissingResource,
	Ambiguous,
	RejectedByCapability,
};

using DriverMatchCapabilityRoutine = bool (*)(const DeviceNode* node);

struct DeviceDriverMatchEntry {
	const char* driver_name;
	const char* node_name;
	const char* acpi_hardware_id;
	const char* acpi_compatible_id;
	const char* controller_driver_name;
	uint64 required_resource_mask;
	uint16 priority;
	uint16 node_type;
	uint16 bus_type;
	uint16 controller_node_type;
	uint16 controller_bus_type;
	uint16 pci_vendor_id;
	uint16 pci_vendor_mask;
	uint16 pci_device_id;
	uint16 pci_device_mask;
	uint16 pci_subsystem_vendor_id;
	uint16 pci_subsystem_vendor_mask;
	uint16 pci_subsystem_device_id;
	uint16 pci_subsystem_device_mask;
	uint8 pci_revision;
	uint8 pci_revision_mask;
	uint8 pci_class_base;
	uint8 pci_class_base_mask;
	uint8 pci_class_sub;
	uint8 pci_class_sub_mask;
	uint8 pci_class_if;
	uint8 pci_class_if_mask;
	DriverMatchCapabilityRoutine capability;
};

struct DeviceDriverMatchDecision {
	const char* driver_name;
	DriverMatchResult result;
	uint16 priority;
	uint16 candidate_count;
	uint16 missing_resource_type;
};

struct DriverBinding {
	const char* driver_name;
	uint32 state;
	int32 probe_result;
	void* driver_data;
	uint32 owner_pid;
	DriverMatchResult match_result;
	uint16 match_priority;
	uint16 match_candidate_count;
	uint16 missing_resource_type;
};

struct DevExt {
	uint16 node_type;
	uint16 flags;

	uint16 bus_type;
	uint16 dev_class;

	uint16 vendor_id;
	uint16 device_id;
	uint16 subsystem_vendor_id;
	uint16 subsystem_device_id;
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
	const char* dev_alias;

	void* acpi_handle;
	DeviceAcpiInfo* acpi_info;
	DeviceControllerResourceContract* controller_resources;
	// Bus nodes reference their physical controller without owning it.
	DeviceNode* controller;
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
		struct TCPConnectionContext;
		struct UDPDatagramContext;
	}
}

namespace Devs {
	class DeviceTree final : public uni::Nchain {
	public:
		DeviceTree() : uni::Nchain(true) {
			extn_field = sizeof(DeviceNode) - sizeof(Nnode);
		}

		DeviceNode* NewNode() {
			return reinterpret_cast<DeviceNode*>(New());
		}

		void SetRoot(DeviceNode* node) {
			root_node = node ? &node->link : nullptr;
		}
	};

	struct DriverOpsEntry {
		const char* driver_name;
		bool (*probe)(DeviceNode*);
	};

	#if defined(_MCCA) && _MCCA == 0x8664
	bool x64_ioapic_has_gsi(uint32 gsi);
	bool x64_ioapic_read_route(uint32 gsi, uint64& route);
	bool x64_ioapic_write_route(uint32 gsi, uint64 route);
	#endif
}


class Devsman {
public:
	using DriverStartRoutine = bool (*)(DeviceNode* node);
	static bool Initialize();
public:// debug
	// Read-only views of the driver registries, for the Ring0 debug dump
	static stduint GetDriverStartHookCount();
	static const char* GetDriverStartHookName(stduint index);
	static stduint GetDriverProcessCount();
	static const char* GetDriverProcessName(stduint index);
public:	
	static bool AttachPCIDevices(uni::PCI& pci);
	static void BindKnownDrivers();
	static void ProbeKnownDrivers();
	static void StartKnownDrivers();
	static void RegisterUSBDeviceTreeHooks();
	static bool RegisterDriverStarter(const char* driver_name, DriverStartRoutine starter);
	static DeviceDriverMatchDecision MatchDriver(const DeviceNode* node, const DeviceDriverMatchEntry* entries, stduint count);
	static const char* DriverMatchResultName(DriverMatchResult result);
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
	static bool RemoveUSBDevice(DeviceNode* node);
	#if defined(_MCCA) && _MCCA == 0x8664
	static void ProcessUSBMSCProbe(void* context);
	static void RegisterUSBMSCStorage(void* context);
	static void MountUSBMSCStorage(void* context);
	static void UnmountUSBMSCStorage(void* context);
	static void RemoveUSBMSCStorage(void* context);
	#endif
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
	static DeviceNode* FindBoundNode(const char* driver_name);
	static DeviceNode* FindOwnedNode(stduint pid);
	static bool RegisterDevAlias(DeviceNode* node, const char* alias);
	static const char* GetDevAlias(const DeviceNode* node);
	static DeviceNode* FindByDevAlias(const char* alias);
	static DeviceNode* FindPCIDeviceByClass(uint8 class_base, uint8 class_sub, uint8 class_if);
	static DeviceNode* FindPCIDeviceByVendorDevice(uint16 vendor_id, uint16 device_id);
	static const DeviceResource* FindResource(const DeviceNode* node, DeviceResourceType type, uint32 index = 0);
	static const DeviceAcpiProperty* FindAcpiProperty(const DeviceNode* node, const char* name);
	static bool ReadAcpiPropertyInteger(const DeviceNode* node, const char* name, uint64& value);
	static bool ReadAcpiPropertyString(const DeviceNode* node, const char* name, const char*& value, stduint& length);
	static bool ReadAcpiPropertyBuffer(const DeviceNode* node, const char* name, const uint8*& value, stduint& length);
	static bool ReadAcpiPropertyReference(const DeviceNode* node, const char* name, const char*& path);
	static bool ReadAcpiPropertyArray(const DeviceNode* node, const char* name, const DeviceAcpiPropertyValue*& values, stduint& count);
	static DeviceControllerResourceContract* EnsureControllerResourceContract(DeviceNode* node);
	static DeviceControllerContractStatus ValidateControllerResourceContract(DeviceControllerResourceContract& contract);
	#if defined(_MCCA) && (_MCCA & 0xFF00) == 0x8600
	static const DeviceControllerCompatibilityDescription* FindPciControllerCompatibility(uint16 vendor_id, uint16 device_id);
	static const DeviceControllerCompatibilityDescription* FindPciControllerCompatibility(uint16 vendor_id, uint16 device_id, uint16 subsystem_vendor_id, uint16 subsystem_device_id, uint8 revision);
	static const DeviceControllerCompatibilityDescription* FindControllerCompatibility(const DeviceNode* node);
	#endif
	static bool SetOps(DeviceNode* node, const DeviceNodeOps* ops);
	static const DeviceNodeOps* GetOps(const DeviceNode* node);
	static stdsint Read(DeviceNode* node, void* buf, stduint count, stduint idx = 0, stduint flags = 0);
	static stdsint Send(DeviceNode* node, const void* buf, stduint count, stduint idx = 0, stduint flags = 0);
	static stdsint Ctrl(DeviceNode* node, stduint cmd, void* args, stduint flags = 0);
	#if _MCCA == 0x8664 && defined(_UEFI)
	static bool BindXHCIEventOwner(stduint owner_pid, stduint owner_tid);
	static bool AcknowledgeXHCIInterrupt();
	static bool ProcessXHCIEvent(const DeviceEvent& event);
	#endif
	static bool AttachStorageOps(DeviceNode* node, uni::StorageTrait* storage);
	static DeviceNode* RegisterStoragePartition(DeviceNode* parent, const char* name,
		uni::StorageTrait& storage, stdsint part_dev, const char* driver_name = "storage-partition");
	#if (_MCCA & 0xFF00) == 0x8600
	static bool RegisterLinkDevice(uni::Network::LinkDevice* device);
	static stduint LinkDeviceCount();
	static uni::Network::LinkDevice* GetLinkDevice(stduint index);
	static bool OpenUdpPort(uint16 port);
	static bool BindUdpPort(uint16 port);
	static bool BindUdpPort(uint16 port, bool reuse_address, stduint& inbox_id);
	static bool BindUdpPort(const uni::Network::IPv4Address& local_address,
		uint16 port, bool reuse_address, stduint& inbox_id);
	static bool AllocateUdpPort(uint16& port);
	static bool AllocateUdpPort(uint16& port, stduint& inbox_id);
	static bool AllocateUdpPort(const uni::Network::IPv4Address& local_address,
		uint16& port, stduint& inbox_id);
	static bool CloseUdpPort(uint16 port);
	static bool CloseUdpPort(uint16 port, stduint inbox_id);
	static bool WaitUdp(uint16 port);
	static bool WaitUdp(uint16 port, stduint inbox_id);
	static void CancelSocketWait(::ThreadBlock* th);
	static bool HasUdp(uint16 port, stduint inbox_id);
	static bool HasUdpFrom(uint16 port, stduint inbox_id,
		const uni::Network::IPv4Address& remote_ip, uint16 remote_port);
	static int ConsumeUdpError(uint16 local_port,
		const uni::Network::IPv4Address& remote_ip, uint16 remote_port);
	static stdsint ReceiveUdp(uint16 port, uni::Network::UDPDatagramContext& context, void* payload, stduint capacity);
	static stdsint ReceiveUdp(uint16 port, stduint inbox_id,
		uni::Network::UDPDatagramContext& context, void* payload, stduint capacity);
	static bool ListenTcpPort(uint16 port, stduint backlog);
	static bool ListenTcpPort(const uni::Network::IPv4Address& local_address,
		uint16 port, stduint backlog);
	static bool CloseTcpPort(uint16 port);
	static bool CloseTcpPort(const uni::Network::IPv4Address& local_address, uint16 port);
	static bool AllocateTcpPort(const uni::Network::IPv4Address& local_address, uint16& port);
	static bool IsTcpPortListening(uint16 port);
	static bool IsTcpPortListening(const uni::Network::IPv4Address& local_address, uint16 port);
	static bool WaitTcpAccept(uint16 port);
	static bool WaitTcpAccept(const uni::Network::IPv4Address& local_address, uint16 port);
	static bool HasTcpAccept(uint16 port);
	static bool HasTcpAccept(const uni::Network::IPv4Address& local_address, uint16 port);
	static stdsint AcceptTcpConnection(uint16 port, uni::Network::TCPConnectionContext& context);
	static stdsint AcceptTcpConnection(const uni::Network::IPv4Address& local_address,
		uint16 port, uni::Network::TCPConnectionContext& context);
	static stdsint StartTcpConnect(const uni::Network::IPv4Address& target_ip,
		uint16 destination_port, uint16& source_port, uni::Network::TCPConnectionContext& context);
	static stdsint StartTcpConnect(const uni::Network::IPv4Address& local_ip,
		const uni::Network::IPv4Address& target_ip,
		uint16 destination_port, uint16& source_port, uni::Network::TCPConnectionContext& context);
	static stdsint CheckTcpConnect(const uni::Network::TCPConnectionContext& context);
	static bool CancelTcpConnect(const uni::Network::TCPConnectionContext& context, int error = 0);
	static stdsint ConnectTcp(const uni::Network::IPv4Address& target_ip,
		uint16 destination_port, uint16& source_port, uni::Network::TCPConnectionContext& context);
	static stdsint ConnectTcp(const uni::Network::IPv4Address& local_ip,
		const uni::Network::IPv4Address& target_ip,
		uint16 destination_port, uint16& source_port, uni::Network::TCPConnectionContext& context);
	static bool CloseTcpConnection(const uni::Network::TCPConnectionContext& context);
	static bool ShutdownTcpWrite(const uni::Network::TCPConnectionContext& context);
	static bool WaitTcpReceive(const uni::Network::TCPConnectionContext& context);
	static bool HasTcpReceive(const uni::Network::TCPConnectionContext& context);
	static bool IsTcpReceiveClosed(const uni::Network::TCPConnectionContext& context);
	static bool HasTcpError(const uni::Network::TCPConnectionContext& context);
	static int TcpErrorCode(const uni::Network::TCPConnectionContext& context);
	static bool IsTcpSendClosed(const uni::Network::TCPConnectionContext& context);
	static bool HasTcpSendSpace(const uni::Network::TCPConnectionContext& context);
	static bool ConfigureTcpKeepAlive(const uni::Network::TCPConnectionContext& context,
		bool enabled, stduint idle_ticks, stduint interval_ticks, stduint probe_limit);
	static bool ShutdownTcpReceive(const uni::Network::TCPConnectionContext& context);
	static stdsint ReceiveTcp(const uni::Network::TCPConnectionContext& context, void* payload, stduint capacity);
	static stdsint SendTcp(const uni::Network::TCPConnectionContext& context, const void* payload, stduint length);
	static stdsint SendUdp(const uni::Network::IPv4Address& source_ip,
		const uni::Network::IPv4Address& target_ip,
		uint16 source_port, uint16 destination_port, const void* payload, stduint length);
	static stdsint SendUdp(const uni::Network::IPv4Address& target_ip,
		uint16 source_port, uint16 destination_port, const void* payload, stduint length);
	static stdsint SendUdp(const uni::Network::MacAddress& target_mac, const uni::Network::IPv4Address& target_ip,
		uint16 source_port, uint16 destination_port, const void* payload, stduint length);
	static bool GetDefaultIPv4Route(void* route, stduint length);
	static bool GetIPv4Interface(stduint index, void* iface, stduint length);
	static bool ApplyIPv4Config(const void* config, stduint length);
	static stduint IPv4ArpCacheCount();
	static bool GetIPv4ArpCacheEntry(stduint index, void* entry, stduint length);
	static stduint UdpInboxCount();
	static bool GetUdpInboxEntry(stduint index, void* entry, stduint length);
	static stduint PendingUdpCount();
	static bool GetPendingUdpEntry(stduint index, void* entry, stduint length);
	static stduint DnsCacheCount();
	static bool GetDnsCacheEntry(stduint index, void* entry, stduint length);
	static bool StoreDnsCacheEntry(const void* entry, stduint length);
	static bool ClearDnsCache();
	static bool ClearDnsCache(const char* host);
	static bool SetDnsServer(const uni::Network::IPv4Address& address);
	static bool SetDnsServers(const uni::Network::IPv4Address* addresses, stduint count);
	static stduint GetDnsServers(uni::Network::IPv4Address* addresses, stduint capacity);
	static stduint TcpListenerCount();
	static bool GetTcpListenerEntry(stduint index, void* entry, stduint length);
	static stduint TcpConnectionCount();
	static bool GetTcpConnectionEntry(stduint index, void* entry, stduint length);
	static bool GetNetStats(void* stats, stduint length);
	static bool GetNetFault(void* state, stduint length);
	static bool SetNetFault(const void* config, stduint length);
	static bool ResetNetFault();
	enum class NetSocketErrorSource : uint8 {
		Icmp,
		Reset,
		Timeout,
		Shutdown,
	};
	static void RecordSocketError(NetSocketErrorSource source);
	static bool RenewDhcp();
	static bool ReleaseDhcp();
	static const char* LookupPciClassName(uint8 class_base, uint8 class_sub, uint8 class_if);
	static const char* LookupPciDeviceName(uint16 vendor_id, uint16 device_id, uint8 class_base = 0, uint8 class_sub = 0);
	static const char* LookupPciVendorName(uint16 vendor_id);
	#endif

};

// ---- AUDIO ----

#include "devsman.com.hpp"

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
