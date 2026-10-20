#ifndef VMCI_HPP_
#define VMCI_HPP_

#include <c/stdinc.h>

static constexpr uint32 VmciProtocolVersion = 6;
static constexpr uint32 VmciInvalidId = 0xFFFFFFFFu;
static constexpr uint32 VmciHypervisorContextId = 0;
static constexpr uint32 VmciHostContextId = 2;
static constexpr uint32 VmciReservedResourceIdMax = 1023;
static constexpr uint32 VmciFirstDynamicResourceId =
	VmciReservedResourceIdMax + 1;
static constexpr uint32 VmciEndpointCapacity = 32;
static constexpr uint32 VmciDoorbellCapacity = 32;
static constexpr uint32 VmciEventSubscriptionCapacity = 32;
static constexpr uint32 VmciQueuePairCapacity = 4;
static constexpr uint32 VmciQueuePairMaxQueueSize = 16u * 1024u;
static constexpr uint32 VmciIpcPayloadCapacity = 4096;
static constexpr uint32 VmciEventPayloadCapacity = 16;
static constexpr uint32 VmciFeatureNotifications = 1u << 0;
static constexpr uint32 VmciFeatureEvents = 1u << 1;
static constexpr uint32 VmciFeatureQueuePairs = 1u << 2;

enum class VmciQueuePairFlag : uint32 {
	None = 0,
	AttachOnly = 1u << 0,
	Local = 1u << 1,
	NonBlocking = 1u << 2,
	Pinned = 1u << 3,
};

enum class VmciEventType : uint32 {
	ContextIdUpdate = 0,
	ContextRemoved = 1,
	QueuePairResumed = 2,
	QueuePairPeerAttach = 3,
	QueuePairPeerDetach = 4,
	MemoryAccessOn = 5,
	MemoryAccessOff = 6,
	Max = 7,
};

enum class VmciMsg : stduint {
	DriverAttach = 0x564D4300u,
	DriverLookup,
	GetInfo,
	EndpointCreate,
	EndpointDestroy,
	DatagramSend,
	DatagramReceive,
	StatsQuery,
	DoorbellCreate,
	DoorbellDestroy,
	DoorbellNotify,
	DoorbellReceive,
	EventSubscribe,
	EventUnsubscribe,
	EventReceive,
	QueuePairCreate,
	QueuePairDestroy,
	QueuePairSend,
	QueuePairReceive,
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
	uint32 features;
	uint32 doorbell_capacity;
	uint32 event_subscription_capacity;
	uint32 queue_pair_capacity;
	uint32 queue_pair_max_queue_size;
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

_PACKED(struct) VmciDoorbellRequest {
	uint32 version;
	uint32 resource_id;
};

_PACKED(struct) VmciDoorbellNotifyRequest {
	uint32 version;
	uint32 reserved;
	VmciHandle handle;
};

_PACKED(struct) VmciDoorbellResult {
	uint32 version;
	int32 status;
	VmciHandle handle;
	uint32 notify_index;
	int32 transport_result;
};

_PACKED(struct) VmciDoorbellEvent {
	uint32 version;
	int32 status;
	VmciHandle handle;
};

_PACKED(struct) VmciEventSubscribeRequest {
	uint32 version;
	uint32 event;
};

_PACKED(struct) VmciEventUnsubscribeRequest {
	uint32 version;
	uint32 subscription_id;
};

_PACKED(struct) VmciEventSubscriptionResult {
	uint32 version;
	int32 status;
	uint32 subscription_id;
	uint32 event;
};

_PACKED(struct) VmciEventContextPayload {
	uint32 context_id;
	uint32 reserved;
};

_PACKED(struct) VmciEventQueuePairPayload {
	VmciHandle handle;
	uint32 peer_id;
	uint32 reserved;
};

_PACKED(struct) VmciEventPacket {
	uint32 version;
	int32 status;
	uint32 subscription_id;
	uint32 event;
	uint32 payload_size;
	uint32 reserved;
	uint8 payload[VmciEventPayloadCapacity];
};

_PACKED(struct) VmciQueuePairCreateRequest {
	uint32 version;
	uint32 resource_id;
	uint32 peer;
	uint32 flags;
	uint64 produce_size;
	uint64 consume_size;
	uint32 local_port;
	uint32 peer_port;
};

_PACKED(struct) VmciQueuePairDestroyRequest {
	uint32 version;
	uint32 resource_id;
};

_PACKED(struct) VmciQueuePairResult {
	uint32 version;
	int32 status;
	VmciHandle handle;
	uint32 peer;
	uint32 flags;
	uint64 produce_size;
	uint64 consume_size;
	uint32 page_count;
	uint32 dma_size;
	int32 hypervisor_result;
	uint32 local_port;
	uint32 peer_port;
	uint32 reserved;
};

_PACKED(struct) VmciQueuePairIoPacket {
	uint32 version;
	int32 status;
	uint32 resource_id;
	uint32 payload_size;
	uint8 payload[VmciIpcPayloadCapacity];
};

static constexpr stduint VmciQueuePairIoPacketHeaderSize =
	sizeof(VmciQueuePairIoPacket) - VmciIpcPayloadCapacity;

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
	uint32 doorbell_count;
	uint32 doorbell_capacity;
	uint32 doorbell_notify_requests;
	uint32 doorbell_notify_failures;
	uint32 doorbell_interrupts;
	uint32 doorbell_delivered;
	uint32 doorbell_ipc_drops;
	uint32 event_subscription_count;
	uint32 event_subscription_capacity;
	uint32 event_datagrams;
	uint32 event_delivered;
	uint32 event_unsubscribed;
	uint32 event_malformed;
	uint32 event_ipc_drops;
	uint32 queue_pair_count;
	uint32 queue_pair_capacity;
	uint32 queue_pair_create_requests;
	uint32 queue_pair_create_failures;
	uint32 queue_pair_detach_requests;
	uint32 queue_pair_detach_failures;
	uint64 queue_pair_dma_bytes;
};

static_assert(sizeof(VmciHandle) == 8, "VMCI handle size mismatch");
static_assert(sizeof(VmciDriverInfo) == 44, "VMCI driver info size mismatch");
static_assert(sizeof(VmciEndpointResult) == 16,
	"VMCI endpoint result size mismatch");
static_assert(VmciDatagramPacketHeaderSize == 32,
	"VMCI IPC datagram header size mismatch");
static_assert(sizeof(VmciDatagramResult) == 16,
	"VMCI datagram result size mismatch");
static_assert(sizeof(VmciDoorbellResult) == 24,
	"VMCI doorbell result size mismatch");
static_assert(sizeof(VmciDoorbellEvent) == 16,
	"VMCI doorbell event size mismatch");
static_assert(sizeof(VmciEventSubscribeRequest) == 8,
	"VMCI event subscribe request size mismatch");
static_assert(sizeof(VmciEventUnsubscribeRequest) == 8,
	"VMCI event unsubscribe request size mismatch");
static_assert(sizeof(VmciEventSubscriptionResult) == 16,
	"VMCI event subscription result size mismatch");
static_assert(sizeof(VmciEventContextPayload) == 8,
	"VMCI context event payload size mismatch");
static_assert(sizeof(VmciEventQueuePairPayload) == 16,
	"VMCI queue pair event payload size mismatch");
static_assert(sizeof(VmciEventPacket) == 40,
	"VMCI event packet size mismatch");
static_assert(sizeof(VmciQueuePairCreateRequest) == 40,
	"VMCI queue pair create request size mismatch");
static_assert(sizeof(VmciQueuePairDestroyRequest) == 8,
	"VMCI queue pair destroy request size mismatch");
static_assert(sizeof(VmciQueuePairResult) == 64,
	"VMCI queue pair result size mismatch");
static_assert(VmciQueuePairIoPacketHeaderSize == 16,
	"VMCI queue pair I/O header size mismatch");
static_assert(sizeof(VmciStats) == 132, "VMCI stats size mismatch");

#endif /* VMCI_HPP_ */
