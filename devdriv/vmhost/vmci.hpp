#ifndef VMCI_HPP_
#define VMCI_HPP_

#include <c/stdinc.h>

static constexpr uint32 VmciProtocolVersion = 1;
static constexpr uint32 VmciInvalidId = 0xFFFFFFFFu;
static constexpr uint32 VmciHypervisorContextId = 0;
static constexpr uint32 VmciHostContextId = 2;
static constexpr uint32 VmciReservedResourceIdMax = 1023;
static constexpr uint32 VmciFirstDynamicResourceId =
	VmciReservedResourceIdMax + 1;
static constexpr uint32 VmciEndpointCapacity = 32;
static constexpr uint32 VmciIpcPayloadCapacity = 4096;

enum class VmciMsg : stduint {
	DriverAttach = 0x564D4300u,
	DriverLookup,
	GetInfo,
	EndpointCreate,
	EndpointDestroy,
	DatagramSend,
	DatagramReceive,
	StatsQuery,
};

enum class VmciStatus : int32 {
	Success = 0,
	InvalidArgument = -1,
	Unavailable = -2,
	NotFound = -3,
	AlreadyExists = -4,
	NoResources = -5,
	AccessDenied = -6,
	PayloadTooLarge = -7,
	TransportFailed = -8,
};

_PACKED(struct) VmciHandle {
	uint32 context;
	uint32 resource;
};

_PACKED(struct) VmciDriverInfo {
	uint32 version;
	int32 status;
	uint32 driver_tid;
	uint32 context_id;
	uint32 max_payload;
	uint32 endpoint_capacity;
};

_PACKED(struct) VmciEndpointRequest {
	uint32 version;
	uint32 resource_id;
};

_PACKED(struct) VmciEndpointResult {
	uint32 version;
	int32 status;
	VmciHandle handle;
};

_PACKED(struct) VmciDatagramPacket {
	uint32 version;
	int32 status;
	VmciHandle destination;
	VmciHandle source;
	uint32 payload_size;
	uint32 reserved;
	uint8 payload[VmciIpcPayloadCapacity];
};

static constexpr stduint VmciDatagramPacketHeaderSize =
	sizeof(VmciDatagramPacket) - VmciIpcPayloadCapacity;

_PACKED(struct) VmciDatagramResult {
	uint32 version;
	int32 status;
	int32 result;
	uint32 reserved;
};

_PACKED(struct) VmciStats {
	uint32 version;
	int32 status;
	uint32 endpoint_count;
	uint32 endpoint_capacity;
	uint32 transmit_requests;
	uint32 transmit_failures;
	uint32 receive_datagrams;
	uint32 receive_delivered;
	uint32 receive_unbound;
	uint32 receive_malformed;
	uint32 receive_ipc_drops;
};

static_assert(sizeof(VmciHandle) == 8, "VMCI handle size mismatch");
static_assert(sizeof(VmciDriverInfo) == 24, "VMCI driver info size mismatch");
static_assert(sizeof(VmciEndpointResult) == 16,
	"VMCI endpoint result size mismatch");
static_assert(VmciDatagramPacketHeaderSize == 32,
	"VMCI IPC datagram header size mismatch");
static_assert(sizeof(VmciDatagramResult) == 16,
	"VMCI datagram result size mismatch");

#endif /* VMCI_HPP_ */
