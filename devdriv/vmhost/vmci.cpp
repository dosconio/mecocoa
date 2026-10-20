// ASCII g++ TAB4 LF
// ModuTitle: VMware VMCI Ring1 Driver
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// Reference: VMCI, GPL-2.0

// Test on: vmware 17.6.3 build-24583834

#include <c/stdinc.h>
#include "../../include/taskman.com.hpp"
#include "../../include/syscall-pow.hpp"
#include "vmci.hpp"

#if defined(_ACCM) && ((_ACCM & 0xFF00) == 0x8600)

namespace {
	constexpr uint16 VmciVendorId = 0x15ADu;
	constexpr uint16 VmciDeviceId = 0x0740u;
	constexpr uint32 VmciMmioBarIndex = 1;
	constexpr uint64 VmciMmioBarSize = 256u * 1024u;
	constexpr uint64 VmciMmioRegisterOffset = 128u * 1024u;
	constexpr uint64 VmciMmioRegisterSize = 64u * 1024u;
	constexpr stduint VmciPageSize = 4096;
	constexpr uint32 VmciPageShift = 12;
	constexpr stduint VmciMaxDatagramSize = 17u * VmciPageSize;
	constexpr stduint VmciDmaBufferSize = VmciMaxDatagramSize + VmciPageSize;
	constexpr stduint VmciDatagramDmaSize = 2u * VmciDmaBufferSize;
	constexpr stduint VmciDmaRegionSize = VmciDatagramDmaSize + VmciPageSize;
	constexpr stduint VmciDmaPollLimit = 1000000;
	constexpr stduint VmciInterruptDrainLimit = 8;
	constexpr uint32 VmciEventHandlerResourceId = 0;
	constexpr uint32 VmciResourcesQuery = 0;
	constexpr uint32 VmciGetContextId = 1;
	constexpr uint32 VmciSetNotifyBitmap = 2;
	constexpr uint32 VmciDoorbellLink = 3;
	constexpr uint32 VmciDoorbellUnlink = 4;
	constexpr uint32 VmciDoorbellNotify = 5;
	constexpr uint32 VmciQueuePairAllocate = 10;
	constexpr uint32 VmciQueuePairDetach = 11;
	constexpr uint32 VmciTransportResourceId = 1;
	constexpr uint8 VmciTransportPacketVersion = 1;
	constexpr uint16 VmciTransportProtocolPacketOnNotify = 1u << 0;
	constexpr uint32 VmciErrorResultFloor = uint32(0u - 1000u);
	constexpr stduint VmciQueuePairMaxDataPages =
		VmciQueuePairMaxQueueSize / VmciPageSize;
	constexpr stduint VmciQueuePairMaxPages =
		2u * (VmciQueuePairMaxDataPages + 1u);

	enum class VmciRegister : uint32 {
		Status = 0x00u,
		Control = 0x04u,
		InterruptCause = 0x08u,
		InterruptMask = 0x0Cu,
		DataOut = 0x10u,
		DataIn = 0x14u,
		Capabilities = 0x18u,
		ResultLow = 0x1Cu,
		ResultHigh = 0x20u,
		DataOutLow = 0x24u,
		DataOutHigh = 0x28u,
		DataInLow = 0x2Cu,
		DataInHigh = 0x30u,
		GuestPageShift = 0x34u,
	};

	constexpr uint32 VmciRequiredRegisterSize =
		_IMM(VmciRegister::GuestPageShift) + sizeof(uint32);
	constexpr uint32 VmciControlReset = 1u << 0;
	constexpr uint32 VmciControlInterruptEnable = 1u << 1;
	constexpr uint32 VmciControlInterruptDisable = 1u << 2;
	constexpr uint32 VmciCapabilityDatagram = 1u << 2;
	constexpr uint32 VmciCapabilityNotifications = 1u << 3;
	constexpr uint32 VmciCapabilityPpn64 = 1u << 4;
	constexpr uint32 VmciCapabilityDmaDatagram = 1u << 5;
	constexpr uint32 VmciInterruptDatagram = 1u << 0;
	constexpr uint32 VmciInterruptNotification = 1u << 1;
	constexpr uint32 VmciInterruptDmaDatagram = 1u << 2;
	constexpr uint32 VmciKnownInterrupts = VmciInterruptDatagram |
		VmciInterruptNotification | VmciInterruptDmaDatagram;

	struct VmciDmaRegion {
		stduint handle = 0;
		uint64 physical = 0;
		uint64 length = 0;
		uint8* address = nullptr;
	};

	struct VmciDmaHeader {
		volatile uint32 busy;
		uint32 opcode;
		uint32 size;
		uint32 reserved;
		uint64 result;
	};

	struct VmciScatterGatherElement {
		uint64 address;
		uint64 size;
	};

	struct VmciDatagram {
		VmciHandle destination;
		VmciHandle source;
		uint64 payload_size;
	};

	struct VmciDoorbellLinkPayload {
		VmciHandle handle;
		uint64 notify_index;
	};

	struct VmciEventDataHeader {
		uint32 event;
		uint32 reserved;
	};

	struct VmciQueueHeader {
		VmciHandle handle;
		volatile uint64 producer_tail;
		volatile uint64 consumer_head;
	};

	enum class VmciTransportPacketType : uint8 {
		Invalid = 0,
		Request = 1,
		Negotiate = 2,
		Offer = 3,
		Attach = 4,
		Wrote = 5,
		Read = 6,
		Reset = 7,
		Request2 = 11,
		Negotiate2 = 12,
	};

	struct VmciTransportWaitingInfo {
		uint64 generation;
		uint64 offset;
	};

	struct VmciTransportPacket {
		uint8 version;
		uint8 type;
		uint16 protocol;
		uint32 source_port;
		uint32 destination_port;
		uint32 reserved;
		union {
			uint64 size;
			VmciHandle handle;
			VmciTransportWaitingInfo waiting;
		};
	};

	_PACKED(struct) VmciQueuePairAllocPayload {
		VmciHandle handle;
		uint32 peer;
		uint32 flags;
		uint64 produce_size;
		uint64 consume_size;
		uint64 num_ppns;
	};

	struct VmciResourceQueryPayload {
		uint32 count;
		uint32 reserved;
		uint32 resources[2];
	};

	static_assert(sizeof(VmciDmaHeader) == 24, "VMCI DMA header size mismatch");
	static_assert(sizeof(VmciScatterGatherElement) == 16,
		"VMCI scatter/gather element size mismatch");
	static_assert(sizeof(VmciDatagram) == 24, "VMCI datagram header size mismatch");
	static_assert(sizeof(VmciDoorbellLinkPayload) == 16,
		"VMCI doorbell link payload size mismatch");
	static_assert(sizeof(VmciEventDataHeader) == 8,
		"VMCI event data header size mismatch");
	static_assert(sizeof(VmciQueueHeader) == 24,
		"VMCI queue header size mismatch");
	static_assert(sizeof(VmciTransportWaitingInfo) == 16,
		"VMCI transport waiting info size mismatch");
	static_assert(sizeof(VmciTransportPacket) == 32,
		"VMCI transport packet size mismatch");
	static_assert(offsetof(VmciTransportPacket, size) == 16,
		"VMCI transport packet payload offset mismatch");
	static_assert(sizeof(VmciQueuePairAllocPayload) == 40,
		"VMCI queue pair allocation payload size mismatch");
	static_assert(offsetof(VmciQueuePairAllocPayload, peer) == 8,
		"VMCI queue pair peer offset mismatch");
	static_assert(offsetof(VmciQueuePairAllocPayload, flags) == 12,
		"VMCI queue pair flags offset mismatch");
	static_assert(offsetof(VmciQueuePairAllocPayload, produce_size) == 16,
		"VMCI queue pair produce size offset mismatch");
	static_assert(offsetof(VmciQueuePairAllocPayload, consume_size) == 24,
		"VMCI queue pair consume size offset mismatch");
	static_assert(offsetof(VmciQueuePairAllocPayload, num_ppns) == 32,
		"VMCI queue pair PPN count offset mismatch");
	static_assert(sizeof(VmciResourceQueryPayload) == 16,
		"VMCI resource query payload size mismatch");
	static_assert(VmciDmaBufferSize == 73728, "VMCI DMA buffer size mismatch");
	static_assert(VmciDatagramDmaSize == 147456,
		"VMCI datagram DMA size mismatch");
	static_assert(VmciDmaRegionSize == 151552, "VMCI DMA region size mismatch");
	static_assert(sizeof(VmciDmaHeader) + sizeof(VmciScatterGatherElement) <=
		VmciPageSize, "VMCI receive header exceeds its page");
	static_assert(sizeof(VmciDmaHeader) + VmciMaxDatagramSize <=
		VmciDmaBufferSize, "VMCI transmit buffer is too small");

	struct VmciContext {
		stduint dev_handle = 0;
		PwcallDeviceIdentity identity = {};
		PwcallDeviceResourceInfo io = {};
		PwcallDeviceResourceInfo mmio = {};
		PwcallDeviceResourceInfo irq = {};
		volatile uint32* registers = nullptr;
		void* mapped_address = nullptr;
		VmciDmaRegion dma = {};
		uint8* receive_buffer = nullptr;
		uint8* transmit_buffer = nullptr;
		volatile uint8* notification_bitmap = nullptr;
		uint64 receive_physical = 0;
		uint64 transmit_physical = 0;
		uint64 notification_physical = 0;
		uint32 capabilities = 0;
		uint32 capabilities_in_use = 0;
		uint32 context_id = VmciInvalidId;
		uint32 malformed_datagrams = 0;
		uint32 driver_tid = 0;
		uint32 transmit_requests = 0;
		uint32 transmit_failures = 0;
		uint32 receive_datagrams = 0;
		uint32 receive_delivered = 0;
		uint32 receive_unbound = 0;
		uint32 receive_ipc_drops = 0;
		uint32 doorbell_notify_requests = 0;
		uint32 doorbell_notify_failures = 0;
		uint32 doorbell_interrupts = 0;
		uint32 doorbell_delivered = 0;
		uint32 doorbell_ipc_drops = 0;
		uint32 event_datagrams = 0;
		uint32 event_delivered = 0;
		uint32 event_unsubscribed = 0;
		uint32 event_malformed = 0;
		uint32 event_ipc_drops = 0;
		uint32 queue_pair_create_requests = 0;
		uint32 queue_pair_create_failures = 0;
		uint32 queue_pair_detach_requests = 0;
		uint32 queue_pair_detach_failures = 0;
		uint64 queue_pair_dma_bytes = 0;
		bool queue_pair_available = false;
		bool service_refresh_pending = false;
		bool use_mmio = false;
	};

	struct VmciEndpoint {
		stduint owner_tid = 0;
		uint32 resource_id = VmciInvalidId;
		bool active = false;
	};

	struct VmciDoorbell {
		stduint owner_tid = 0;
		uint32 resource_id = VmciInvalidId;
		uint32 notify_index = VmciInvalidId;
		bool active = false;
	};

	struct VmciEventSubscription {
		stduint owner_tid = 0;
		uint32 subscription_id = VmciInvalidId;
		uint32 event = _IMM(VmciEventType::Max);
		bool active = false;
	};

	struct VmciQueuePair {
		stduint owner_tid = 0;
		VmciHandle handle = { VmciInvalidId, VmciInvalidId };
		uint32 peer = VmciInvalidId;
		uint32 flags = 0;
		uint64 produce_size = 0;
		uint64 consume_size = 0;
		uint32 page_count = 0;
		uint32 local_port = VmciInvalidId;
		uint32 peer_port = VmciInvalidId;
		stduint pending_receive_tid = 0;
		uint32 pending_receive_size = 0;
		int32 hypervisor_result = 0;
		VmciDmaRegion dma = {};
		bool awaiting_negotiate = false;
		bool awaiting_attach = false;
		bool old_transport_protocol = false;
		bool active = false;
	};

	VmciContext g_vmci{};
	VmciEndpoint g_endpoints[VmciEndpointCapacity]{};
	VmciDoorbell g_doorbells[VmciDoorbellCapacity]{};
	VmciEventSubscription g_event_subscriptions[VmciEventSubscriptionCapacity]{};
	VmciQueuePair g_queue_pairs[VmciQueuePairCapacity]{};
	uint32 g_next_resource_id = VmciFirstDynamicResourceId;
	uint32 g_next_subscription_id = 1;
	alignas(stduint) uint8 g_message_buffer[sizeof(VmciDatagramPacket)]{};
	alignas(uint64) uint8 g_queue_pair_payload[
		sizeof(VmciQueuePairAllocPayload) +
		VmciQueuePairMaxPages * sizeof(uint64)]{};
	alignas(stduint) VmciQueuePairIoPacket g_queue_pair_io_packet{};

	bool FindResource(stduint dev_handle, PwcallDeviceResourceType type,
		uint32 resource_index, PwcallDeviceResourceInfo& resource) {
		const stdsint count = Powercall::DevGetResourceCount(dev_handle);
		if (count <= 0) return false;
		for (stdsint i = 0; i < count; ++i) {
			PwcallDeviceResourceQuery query = {};
			query.index = uint32(i);
			if (Powercall::DevGetResource(dev_handle, &query) != 0) continue;
			if (PwcallDeviceResourceType(query.resource.type) != type ||
				query.resource.index != resource_index) continue;
			resource = query.resource;
			return true;
		}
		return false;
	}

	bool ReadIoRegister(VmciRegister reg, uint32& value) {
		PwcallDeviceIoRequest request = {};
		request.resource_type = _IMM(PwcallDeviceResourceType::PciBarIo);
		request.resource_index = g_vmci.io.index;
		request.width = sizeof(uint32);
		request.offset = _IMM(reg);
		if (Powercall::DevIoRead(g_vmci.dev_handle, &request) != 0) return false;
		value = request.value;
		return true;
	}

	bool WriteIoRegister(VmciRegister reg, uint32 value) {
		PwcallDeviceIoRequest request = {};
		request.resource_type = _IMM(PwcallDeviceResourceType::PciBarIo);
		request.resource_index = g_vmci.io.index;
		request.width = sizeof(uint32);
		request.offset = _IMM(reg);
		request.value = value;
		return Powercall::DevIoWrite(g_vmci.dev_handle, &request) == 0;
	}

	bool ReadRegister(VmciRegister reg, uint32& value) {
		if (!g_vmci.use_mmio) return ReadIoRegister(reg, value);
		value = g_vmci.registers[_IMM(reg) / sizeof(uint32)];
		return true;
	}

	bool WriteRegister(VmciRegister reg, uint32 value) {
		if (!g_vmci.use_mmio) return WriteIoRegister(reg, value);
		g_vmci.registers[_IMM(reg) / sizeof(uint32)] = value;
		return true;
	}

	void DmaWriteBarrier() {
		_ASM volatile ("mfence":::"memory");
	}

	void DmaReadBarrier() {
		_ASM volatile ("mfence":::"memory");
	}

	bool WaitForDmaBusy(volatile uint32& busy, uint32 expected) {
		for0(i, VmciDmaPollLimit) {
			if (busy == expected) {
				DmaReadBarrier();
				return true;
			}
			_ASM volatile ("pause":::"memory");
		}
		return false;
	}

	bool AllocateDmaRegion(stduint dev_handle, stduint size,
		VmciDmaRegion& region, stduint flags = 0) {
		const stdsint dma_handle = Powercall::DevDmaAlloc(dev_handle, size, flags);
		if (dma_handle <= 0) return false;
		region.handle = stduint(dma_handle);

		PwcallDeviceDmaMapRequest request = {};
		request.map_flags = _IMM(PwcallDeviceDmaMapFlag::Writable);
		if (Powercall::DevDmaMap(region.handle, &request) != 0 ||
			!request.address || !request.physical || request.length < size ||
			uint64(size - 1) > ~request.physical) {
			(void)Powercall::DevDmaFree(region.handle);
			region = {};
			return false;
		}

		region.physical = request.physical;
		region.length = request.length;
		region.address = reinterpret_cast<uint8*>(stduint(request.address));
		MemSet(region.address, 0, stduint(region.length));
		return true;
	}

	void FreeDmaRegion(VmciDmaRegion& region) {
		if (region.handle) (void)Powercall::DevDmaFree(region.handle);
		region = {};
	}

	bool MapMmioRegisters() {
		if (g_vmci.mmio.length != VmciMmioBarSize) return false;
		PwcallDeviceMapRequest request = {};
		request.resource_type = _IMM(PwcallDeviceResourceType::PciBarMmio);
		request.resource_index = g_vmci.mmio.index;
		request.map_flags = _IMM(PwcallDeviceMapFlag::Writable);
		request.offset = VmciMmioRegisterOffset;
		request.length = VmciMmioRegisterSize;
		const stdsint address = Powercall::DevMmap(g_vmci.dev_handle, &request);
		if (address <= 0) return false;
		g_vmci.mapped_address = reinterpret_cast<void*>(stduint(address));
		g_vmci.registers = reinterpret_cast<volatile uint32*>(g_vmci.mapped_address);
		g_vmci.use_mmio = true;
		return true;
	}

	bool SelectRegisterAccess() {
		if (FindResource(g_vmci.dev_handle, PwcallDeviceResourceType::PciBarMmio,
			VmciMmioBarIndex, g_vmci.mmio) && MapMmioRegisters()) return true;
		if (!FindResource(g_vmci.dev_handle, PwcallDeviceResourceType::PciBarIo,
			0, g_vmci.io)) return false;
		if (g_vmci.io.length < VmciRequiredRegisterSize ||
			g_vmci.io.start + VmciRequiredRegisterSize > 0x10000u) return false;
		g_vmci.use_mmio = false;
		return true;
	}

	bool InitializeRegisters() {
		if (!WriteRegister(VmciRegister::Control, VmciControlReset) ||
			!WriteRegister(VmciRegister::Control, VmciControlInterruptDisable) ||
			!WriteRegister(VmciRegister::InterruptMask, 0)) return false;
		uint32 stale_cause = 0;
		if (!ReadRegister(VmciRegister::Capabilities, g_vmci.capabilities) ||
			!ReadRegister(VmciRegister::InterruptCause, stale_cause)) return false;
		return true;
	}

	bool ValidateCapabilities() {
		if ((g_vmci.capabilities & VmciCapabilityDatagram) == 0) {
			plogerro("[VMCI] Datagram capability is missing");
			return false;
		}
		if (g_vmci.use_mmio &&
			(g_vmci.capabilities & VmciCapabilityDmaDatagram) == 0) {
			plogerro("[VMCI] MMIO access requires DMA datagram capability");
			return false;
		}
		return true;
	}

	bool SendVmciDatagram(const VmciHandle& destination,
		const VmciHandle& source, const void* payload, stduint payload_size,
		uint64& result) {
		if ((!payload && payload_size) ||
			payload_size > VmciMaxDatagramSize - sizeof(VmciDatagram) ||
			!g_vmci.transmit_buffer) return false;

		auto* header = reinterpret_cast<VmciDmaHeader*>(g_vmci.transmit_buffer);
		uint8* output = g_vmci.transmit_buffer + sizeof(VmciDmaHeader);
		auto* datagram = reinterpret_cast<VmciDatagram*>(output);
		const stduint datagram_size = sizeof(VmciDatagram) + payload_size;
		MemSet(header, 0, sizeof(VmciDmaHeader));
		datagram->destination = destination;
		datagram->source = source;
		datagram->payload_size = payload_size;
		if (payload_size) {
			MemCopyN(output + sizeof(VmciDatagram), payload, payload_size);
		}
		header->opcode = 0;
		header->size = uint32(datagram_size);
		header->busy = 1;
		DmaWriteBarrier();
		if (!WriteRegister(VmciRegister::DataOutLow,
			uint32(g_vmci.transmit_physical)) ||
			!WaitForDmaBusy(header->busy, 0)) return false;

		uint32 register_result = 0;
		if (!ReadRegister(VmciRegister::ResultLow, register_result)) return false;
		result = register_result == 0 ? header->result : register_result;
		return true;
	}

	bool SendHypervisorRequest(uint32 resource_id, const void* payload,
		stduint payload_size, int32& result) {
		const VmciHandle destination = {
			VmciHypervisorContextId, resource_id
		};
		const VmciHandle source = { VmciInvalidId, VmciInvalidId };
		uint64 raw_result = 0;
		if (!SendVmciDatagram(destination, source, payload,
			payload_size, raw_result)) return false;
		result = int32(uint32(raw_result));
		return true;
	}

	bool RegisterNotificationBitmap() {
		if (!g_vmci.notification_bitmap ||
			(g_vmci.capabilities_in_use & VmciCapabilityNotifications) == 0) {
			return false;
		}
		const uint64 bitmap_ppn = g_vmci.notification_physical >> VmciPageShift;
		if ((g_vmci.capabilities_in_use & VmciCapabilityPpn64) == 0 &&
			(bitmap_ppn >> 32) != 0) {
			plogerro("[VMCI] Notification bitmap PPN exceeds 32 bits");
			return false;
		}
		const uint64 bitmap_payload =
			(g_vmci.capabilities_in_use & VmciCapabilityPpn64) ?
			bitmap_ppn : uint64(uint32(bitmap_ppn));
		int32 result = 0;
		const bool sent = SendHypervisorRequest(VmciSetNotifyBitmap,
			&bitmap_payload, sizeof(bitmap_payload), result);
		if (!sent || result != 0) {
			plogerro("[VMCI] Notification bitmap registration failed result=%d",
				result);
			return false;
		}
		return true;
	}

	bool QueryContextId() {
		const VmciHandle destination = {
			VmciHypervisorContextId, VmciGetContextId
		};
		const VmciHandle source = { VmciInvalidId, VmciInvalidId };

		uint64 result = 0;
		if (!SendVmciDatagram(destination, source, nullptr, 0, result)) {
			plogerro("[VMCI] GET_CONTEXT_ID transport failed");
			return false;
		}
		if (result > 0xFFFFFFFFull) {
			plogerro("[VMCI] GET_CONTEXT_ID result exceeds 32 bits");
			return false;
		}
		g_vmci.context_id = uint32(result);
		if (g_vmci.context_id <= VmciHostContextId ||
			g_vmci.context_id >= VmciErrorResultFloor) {
			plogerro("[VMCI] Invalid context id=%08x",
				unsigned(g_vmci.context_id));
			return false;
		}
		return true;
	}

	bool QueryQueuePairResources() {
		VmciResourceQueryPayload payload = {};
		payload.count = 2;
		payload.resources[0] = VmciQueuePairAllocate;
		payload.resources[1] = VmciQueuePairDetach;
		int32 result = 0;
		if (!SendHypervisorRequest(VmciResourcesQuery, &payload,
			sizeof(payload), result)) {
			plogwarn("[VMCI] Queue pair resource query transport failed");
			return false;
		}
		if (result < 0 || (uint32(result) & 0x3u) != 0x3u) {
			plogwarn("[VMCI] Queue pair resources unavailable result=%d", result);
			return false;
		}
		return true;
	}

	bool ConfigureDmaDatagrams() {
		if (!g_vmci.use_mmio ||
			(g_vmci.capabilities & VmciCapabilityDmaDatagram) == 0) return false;
		if (!AllocateDmaRegion(g_vmci.dev_handle, VmciDmaRegionSize,
			g_vmci.dma)) {
			plogerro("[VMCI] Failed to allocate %u-byte DMA region",
				unsigned(VmciDmaRegionSize));
			return false;
		}

		g_vmci.receive_buffer = g_vmci.dma.address;
		g_vmci.transmit_buffer = g_vmci.dma.address + VmciDmaBufferSize;
		g_vmci.receive_physical = g_vmci.dma.physical;
		g_vmci.transmit_physical = g_vmci.dma.physical + VmciDmaBufferSize;
		g_vmci.notification_bitmap = g_vmci.dma.address + VmciDatagramDmaSize;
		g_vmci.notification_physical =
			g_vmci.dma.physical + VmciDatagramDmaSize;
		if (g_vmci.notification_physical & (VmciPageSize - 1u)) {
			plogerro("[VMCI] Notification bitmap is not page aligned");
			return false;
		}
		g_vmci.capabilities_in_use = VmciCapabilityDatagram |
			VmciCapabilityDmaDatagram;
		if (g_vmci.capabilities & VmciCapabilityNotifications) {
			g_vmci.capabilities_in_use |= VmciCapabilityNotifications;
		}
		if (g_vmci.capabilities & VmciCapabilityPpn64) {
			g_vmci.capabilities_in_use |= VmciCapabilityPpn64;
		}

		if (!WriteRegister(VmciRegister::Capabilities,
			g_vmci.capabilities_in_use) ||
			!WriteRegister(VmciRegister::GuestPageShift, VmciPageShift) ||
			!WriteRegister(VmciRegister::DataInHigh,
				uint32(g_vmci.receive_physical >> 32)) ||
			!WriteRegister(VmciRegister::DataOutHigh,
				uint32(g_vmci.transmit_physical >> 32))) return false;

		if ((g_vmci.capabilities_in_use & VmciCapabilityNotifications) &&
			!RegisterNotificationBitmap()) return false;
		uint32 stale_cause = 0;
		if (!ReadRegister(VmciRegister::InterruptCause, stale_cause)) return false;
		(void)stale_cause;
		return true;
	}

	bool SendIpcMessage(stduint target, VmciMsg type, const void* data,
		stduint length) {
		CommMsg message = {};
		message.data.address = _IMM(data);
		message.data.length = length;
		message.type = _IMM(type);
		return Powercall::SysComm(COMM_SEND_ASYNC, target, &message) == 0;
	}

	VmciEndpoint* FindEndpoint(uint32 resource_id) {
		for0(i, VmciEndpointCapacity) {
			if (g_endpoints[i].active &&
				g_endpoints[i].resource_id == resource_id) return &g_endpoints[i];
		}
		return nullptr;
	}

	VmciEndpoint* FindFreeEndpoint() {
		for0(i, VmciEndpointCapacity) {
			if (!g_endpoints[i].active) return &g_endpoints[i];
		}
		return nullptr;
	}

	uint32 CountEndpoints() {
		uint32 count = 0;
		for0(i, VmciEndpointCapacity) {
			if (g_endpoints[i].active) count++;
		}
		return count;
	}

	VmciDoorbell* FindDoorbell(uint32 resource_id) {
		for0(i, VmciDoorbellCapacity) {
			if (g_doorbells[i].active &&
				g_doorbells[i].resource_id == resource_id) return &g_doorbells[i];
		}
		return nullptr;
	}

	VmciDoorbell* FindFreeDoorbell() {
		for0(i, VmciDoorbellCapacity) {
			if (!g_doorbells[i].active) return &g_doorbells[i];
		}
		return nullptr;
	}

	uint32 CountDoorbells() {
		uint32 count = 0;
		for0(i, VmciDoorbellCapacity) {
			if (g_doorbells[i].active) count++;
		}
		return count;
	}

	bool IsClientEvent(uint32 event) {
		return event <= _IMM(VmciEventType::QueuePairPeerDetach);
	}

	VmciEventSubscription* FindEventSubscription(uint32 subscription_id) {
		for0(i, VmciEventSubscriptionCapacity) {
			if (g_event_subscriptions[i].active &&
				g_event_subscriptions[i].subscription_id == subscription_id) {
				return &g_event_subscriptions[i];
			}
		}
		return nullptr;
	}

	VmciEventSubscription* FindFreeEventSubscription() {
		for0(i, VmciEventSubscriptionCapacity) {
			if (!g_event_subscriptions[i].active) return &g_event_subscriptions[i];
		}
		return nullptr;
	}

	uint32 CountEventSubscriptions() {
		uint32 count = 0;
		for0(i, VmciEventSubscriptionCapacity) {
			if (g_event_subscriptions[i].active) count++;
		}
		return count;
	}

	VmciQueuePair* FindQueuePair(uint32 resource_id) {
		for0(i, VmciQueuePairCapacity) {
			if (g_queue_pairs[i].active &&
				g_queue_pairs[i].handle.resource == resource_id) {
				return &g_queue_pairs[i];
			}
		}
		return nullptr;
	}

	VmciQueuePair* FindFreeQueuePair() {
		for0(i, VmciQueuePairCapacity) {
			if (!g_queue_pairs[i].active) return &g_queue_pairs[i];
		}
		return nullptr;
	}

	VmciQueuePair* FindTransportQueuePair(uint32 peer, uint32 local_port,
		uint32 peer_port) {
		for0(i, VmciQueuePairCapacity) {
			VmciQueuePair& queue_pair = g_queue_pairs[i];
			if (queue_pair.active && queue_pair.peer == peer &&
				queue_pair.local_port == local_port &&
				queue_pair.peer_port == peer_port) return &queue_pair;
		}
		return nullptr;
	}

	uint32 CountQueuePairs() {
		uint32 count = 0;
		for0(i, VmciQueuePairCapacity) {
			if (g_queue_pairs[i].active) count++;
		}
		return count;
	}

	uint32 AllocateSubscriptionId() {
		for0(i, VmciEventSubscriptionCapacity + 1) {
			const uint32 candidate = g_next_subscription_id++;
			if (g_next_subscription_id == 0 ||
				g_next_subscription_id == VmciInvalidId) {
				g_next_subscription_id = 1;
			}
			if (candidate != 0 && candidate != VmciInvalidId &&
				!FindEventSubscription(candidate)) return candidate;
		}
		return VmciInvalidId;
	}

	bool ResourceIdInUse(uint32 resource_id) {
		return FindEndpoint(resource_id) || FindDoorbell(resource_id) ||
			FindQueuePair(resource_id);
	}

	uint32 AllocateResourceId() {
		for0(i, VmciEndpointCapacity + VmciDoorbellCapacity +
			VmciQueuePairCapacity + 1) {
			const uint32 candidate = g_next_resource_id++;
			if (g_next_resource_id == VmciInvalidId ||
				g_next_resource_id < VmciFirstDynamicResourceId) {
				g_next_resource_id = VmciFirstDynamicResourceId;
			}
			if (candidate >= VmciFirstDynamicResourceId &&
				candidate != VmciInvalidId && !ResourceIdInUse(candidate)) {
				return candidate;
			}
		}
		return VmciInvalidId;
	}

	VmciDriverInfo MakeDriverInfo(VmciStatus status) {
		VmciDriverInfo info = {};
		info.version = VmciProtocolVersion;
		info.status = int32(status);
		info.driver_tid = uint32(g_vmci.driver_tid);
		info.context_id = g_vmci.context_id;
		info.max_payload = VmciIpcPayloadCapacity;
		info.endpoint_capacity = VmciEndpointCapacity;
		info.features = (g_vmci.capabilities_in_use &
			VmciCapabilityNotifications) ? VmciFeatureNotifications : 0;
		info.features |= VmciFeatureEvents;
		if (g_vmci.queue_pair_available) {
			info.features |= VmciFeatureQueuePairs;
		}
		info.doorbell_capacity = VmciDoorbellCapacity;
		info.event_subscription_capacity = VmciEventSubscriptionCapacity;
		info.queue_pair_capacity = VmciQueuePairCapacity;
		info.queue_pair_max_queue_size = VmciQueuePairMaxQueueSize;
		return info;
	}

	void ReplyEndpoint(stduint target, VmciMsg type, VmciStatus status,
		uint32 resource_id = VmciInvalidId) {
		VmciEndpointResult result = {};
		result.version = VmciProtocolVersion;
		result.status = int32(status);
		result.handle.context = g_vmci.context_id;
		result.handle.resource = resource_id;
		(void)SendIpcMessage(target, type, &result, sizeof(result));
	}

	void CreateEndpoint(stduint source, const VmciEndpointRequest& request) {
		if (request.version != VmciProtocolVersion) {
			ReplyEndpoint(source, VmciMsg::EndpointCreate,
				VmciStatus::InvalidArgument);
			return;
		}
		VmciEndpoint* endpoint = FindFreeEndpoint();
		if (!endpoint) {
			ReplyEndpoint(source, VmciMsg::EndpointCreate,
				VmciStatus::NoResources);
			return;
		}

		uint32 resource_id = request.resource_id;
		if (resource_id == VmciInvalidId) resource_id = AllocateResourceId();
		else if (resource_id < VmciFirstDynamicResourceId) {
			ReplyEndpoint(source, VmciMsg::EndpointCreate,
				VmciStatus::AccessDenied);
			return;
		}
		if (resource_id == VmciInvalidId) {
			ReplyEndpoint(source, VmciMsg::EndpointCreate,
				VmciStatus::NoResources);
			return;
		}
		if (ResourceIdInUse(resource_id)) {
			ReplyEndpoint(source, VmciMsg::EndpointCreate,
				VmciStatus::AlreadyExists, resource_id);
			return;
		}

		endpoint->owner_tid = source;
		endpoint->resource_id = resource_id;
		endpoint->active = true;
		ReplyEndpoint(source, VmciMsg::EndpointCreate,
			VmciStatus::Success, resource_id);
	}

	void DestroyEndpoint(stduint source, const VmciEndpointRequest& request) {
		if (request.version != VmciProtocolVersion ||
			request.resource_id == VmciInvalidId) {
			ReplyEndpoint(source, VmciMsg::EndpointDestroy,
				VmciStatus::InvalidArgument);
			return;
		}
		auto* endpoint = FindEndpoint(request.resource_id);
		if (!endpoint) {
			ReplyEndpoint(source, VmciMsg::EndpointDestroy,
				VmciStatus::NotFound, request.resource_id);
			return;
		}
		if (endpoint->owner_tid != source) {
			ReplyEndpoint(source, VmciMsg::EndpointDestroy,
				VmciStatus::AccessDenied, request.resource_id);
			return;
		}
		*endpoint = {};
		ReplyEndpoint(source, VmciMsg::EndpointDestroy,
			VmciStatus::Success, request.resource_id);
	}

	void ReplyDoorbell(stduint target, VmciMsg type, VmciStatus status,
		VmciHandle handle = { VmciInvalidId, VmciInvalidId },
		uint32 notify_index = VmciInvalidId, int32 transport_result = 0) {
		VmciDoorbellResult result = {};
		result.version = VmciProtocolVersion;
		result.status = int32(status);
		result.handle = handle;
		result.notify_index = notify_index;
		result.transport_result = transport_result;
		(void)SendIpcMessage(target, type, &result, sizeof(result));
	}

	void CreateDoorbell(stduint source, const VmciDoorbellRequest& request) {
		if (request.version != VmciProtocolVersion) {
			ReplyDoorbell(source, VmciMsg::DoorbellCreate,
				VmciStatus::InvalidArgument);
			return;
		}
		if ((g_vmci.capabilities_in_use & VmciCapabilityNotifications) == 0) {
			ReplyDoorbell(source, VmciMsg::DoorbellCreate,
				VmciStatus::Unavailable);
			return;
		}
		VmciDoorbell* doorbell = FindFreeDoorbell();
		if (!doorbell) {
			ReplyDoorbell(source, VmciMsg::DoorbellCreate,
				VmciStatus::NoResources);
			return;
		}

		uint32 resource_id = request.resource_id;
		if (resource_id == VmciInvalidId) resource_id = AllocateResourceId();
		else if (resource_id < VmciFirstDynamicResourceId) {
			ReplyDoorbell(source, VmciMsg::DoorbellCreate,
				VmciStatus::AccessDenied);
			return;
		}
		if (resource_id == VmciInvalidId) {
			ReplyDoorbell(source, VmciMsg::DoorbellCreate,
				VmciStatus::NoResources);
			return;
		}
		if (ResourceIdInUse(resource_id)) {
			ReplyDoorbell(source, VmciMsg::DoorbellCreate,
				VmciStatus::AlreadyExists,
				{ g_vmci.context_id, resource_id });
			return;
		}

		const uint32 notify_index = uint32(doorbell - g_doorbells);
		const VmciHandle handle = { g_vmci.context_id, resource_id };
		const VmciDoorbellLinkPayload payload = { handle, notify_index };
		int32 transport_result = 0;
		if (!SendHypervisorRequest(VmciDoorbellLink, &payload,
			sizeof(payload), transport_result) || transport_result < 0) {
			ReplyDoorbell(source, VmciMsg::DoorbellCreate,
				VmciStatus::TransportFailed, handle, notify_index,
				transport_result);
			return;
		}

		doorbell->owner_tid = source;
		doorbell->resource_id = resource_id;
		doorbell->notify_index = notify_index;
		doorbell->active = true;
		ReplyDoorbell(source, VmciMsg::DoorbellCreate,
			VmciStatus::Success, handle, notify_index, transport_result);
	}

	void DestroyDoorbell(stduint source, const VmciDoorbellRequest& request) {
		if (request.version != VmciProtocolVersion ||
			request.resource_id == VmciInvalidId) {
			ReplyDoorbell(source, VmciMsg::DoorbellDestroy,
				VmciStatus::InvalidArgument);
			return;
		}
		VmciDoorbell* doorbell = FindDoorbell(request.resource_id);
		if (!doorbell) {
			ReplyDoorbell(source, VmciMsg::DoorbellDestroy,
				VmciStatus::NotFound,
				{ g_vmci.context_id, request.resource_id });
			return;
		}
		const VmciHandle handle = {
			g_vmci.context_id, doorbell->resource_id
		};
		if (doorbell->owner_tid != source) {
			ReplyDoorbell(source, VmciMsg::DoorbellDestroy,
				VmciStatus::AccessDenied, handle, doorbell->notify_index);
			return;
		}

		int32 transport_result = 0;
		if (!SendHypervisorRequest(VmciDoorbellUnlink, &handle,
			sizeof(handle), transport_result) || transport_result < 0) {
			ReplyDoorbell(source, VmciMsg::DoorbellDestroy,
				VmciStatus::TransportFailed, handle, doorbell->notify_index,
				transport_result);
			return;
		}
		const uint32 notify_index = doorbell->notify_index;
		g_vmci.notification_bitmap[notify_index] = 0;
		*doorbell = {};
		ReplyDoorbell(source, VmciMsg::DoorbellDestroy,
			VmciStatus::Success, handle, notify_index, transport_result);
	}

	void NotifyDoorbell(stduint source,
		const VmciDoorbellNotifyRequest& request) {
		g_vmci.doorbell_notify_requests++;
		if (request.version != VmciProtocolVersion || request.reserved != 0 ||
			request.handle.context == VmciInvalidId ||
			request.handle.resource == VmciInvalidId) {
			g_vmci.doorbell_notify_failures++;
			ReplyDoorbell(source, VmciMsg::DoorbellNotify,
				VmciStatus::InvalidArgument, request.handle);
			return;
		}

		int32 transport_result = 0;
		if (!SendHypervisorRequest(VmciDoorbellNotify, &request.handle,
			sizeof(request.handle), transport_result) || transport_result < 0) {
			g_vmci.doorbell_notify_failures++;
			ReplyDoorbell(source, VmciMsg::DoorbellNotify,
				VmciStatus::TransportFailed, request.handle,
				VmciInvalidId, transport_result);
			return;
		}
		VmciDoorbell* doorbell = request.handle.context == g_vmci.context_id ?
			FindDoorbell(request.handle.resource) : nullptr;
		ReplyDoorbell(source, VmciMsg::DoorbellNotify,
			VmciStatus::Success, request.handle,
			doorbell ? doorbell->notify_index : VmciInvalidId,
			transport_result);
	}

	void ScanNotificationBitmap() {
		if (!g_vmci.notification_bitmap) return;
		g_vmci.doorbell_interrupts++;
		DmaReadBarrier();
		for0(i, VmciDoorbellCapacity) {
			VmciDoorbell& doorbell = g_doorbells[i];
			if (!doorbell.active ||
				(g_vmci.notification_bitmap[doorbell.notify_index] & 1u) == 0) {
				continue;
			}
			g_vmci.notification_bitmap[doorbell.notify_index] &= uint8(~1u);
			VmciDoorbellEvent event = {};
			event.version = VmciProtocolVersion;
			event.status = int32(VmciStatus::Success);
			event.handle.context = g_vmci.context_id;
			event.handle.resource = doorbell.resource_id;
			if (SendIpcMessage(doorbell.owner_tid, VmciMsg::DoorbellReceive,
				&event, sizeof(event))) g_vmci.doorbell_delivered++;
			else g_vmci.doorbell_ipc_drops++;
		}
		DmaWriteBarrier();
	}

	void ReplyEventSubscription(stduint target, VmciMsg type,
		VmciStatus status, uint32 subscription_id = VmciInvalidId,
		uint32 event = _IMM(VmciEventType::Max)) {
		VmciEventSubscriptionResult result = {};
		result.version = VmciProtocolVersion;
		result.status = int32(status);
		result.subscription_id = subscription_id;
		result.event = event;
		(void)SendIpcMessage(target, type, &result, sizeof(result));
	}

	void SubscribeEvent(stduint source,
		const VmciEventSubscribeRequest& request) {
		if (request.version != VmciProtocolVersion ||
			!IsClientEvent(request.event)) {
			ReplyEventSubscription(source, VmciMsg::EventSubscribe,
				VmciStatus::InvalidArgument, VmciInvalidId, request.event);
			return;
		}
		VmciEventSubscription* subscription = FindFreeEventSubscription();
		if (!subscription) {
			ReplyEventSubscription(source, VmciMsg::EventSubscribe,
				VmciStatus::NoResources, VmciInvalidId, request.event);
			return;
		}
		const uint32 subscription_id = AllocateSubscriptionId();
		if (subscription_id == VmciInvalidId) {
			ReplyEventSubscription(source, VmciMsg::EventSubscribe,
				VmciStatus::NoResources, VmciInvalidId, request.event);
			return;
		}
		subscription->owner_tid = source;
		subscription->subscription_id = subscription_id;
		subscription->event = request.event;
		subscription->active = true;
		ReplyEventSubscription(source, VmciMsg::EventSubscribe,
			VmciStatus::Success, subscription_id, request.event);
	}

	void UnsubscribeEvent(stduint source,
		const VmciEventUnsubscribeRequest& request) {
		if (request.version != VmciProtocolVersion ||
			request.subscription_id == 0 ||
			request.subscription_id == VmciInvalidId) {
			ReplyEventSubscription(source, VmciMsg::EventUnsubscribe,
				VmciStatus::InvalidArgument, request.subscription_id);
			return;
		}
		VmciEventSubscription* subscription =
			FindEventSubscription(request.subscription_id);
		if (!subscription) {
			ReplyEventSubscription(source, VmciMsg::EventUnsubscribe,
				VmciStatus::NotFound, request.subscription_id);
			return;
		}
		if (subscription->owner_tid != source) {
			ReplyEventSubscription(source, VmciMsg::EventUnsubscribe,
				VmciStatus::AccessDenied, request.subscription_id,
				subscription->event);
			return;
		}
		const uint32 event = subscription->event;
		*subscription = {};
		ReplyEventSubscription(source, VmciMsg::EventUnsubscribe,
			VmciStatus::Success, request.subscription_id, event);
	}

	void SendQueuePairResult(stduint target, VmciMsg type,
		const VmciQueuePairResult& result) {
		(void)SendIpcMessage(target, type, &result, sizeof(result));
	}

	VmciQueuePairResult MakeQueuePairResult(const VmciQueuePair& queue_pair,
		VmciStatus status) {
		VmciQueuePairResult response = {};
		response.version = VmciProtocolVersion;
		response.status = int32(status);
		response.handle = queue_pair.handle;
		response.peer = queue_pair.peer;
		response.flags = queue_pair.flags;
		response.produce_size = queue_pair.produce_size;
		response.consume_size = queue_pair.consume_size;
		response.page_count = queue_pair.page_count;
		response.dma_size = queue_pair.page_count * VmciPageSize;
		response.hypervisor_result = queue_pair.hypervisor_result;
		response.local_port = queue_pair.local_port;
		response.peer_port = queue_pair.peer_port;
		return response;
	}

	void SendQueuePairIoResult(stduint target, VmciMsg type,
		VmciStatus status, uint32 resource_id, const void* payload = nullptr,
		uint32 payload_size = 0) {
		MemSet(&g_queue_pair_io_packet, 0, sizeof(g_queue_pair_io_packet));
		g_queue_pair_io_packet.version = VmciProtocolVersion;
		g_queue_pair_io_packet.status = int32(status);
		g_queue_pair_io_packet.resource_id = resource_id;
		g_queue_pair_io_packet.payload_size = payload_size;
		if (payload && payload_size) {
			MemCopyN(g_queue_pair_io_packet.payload, payload, payload_size);
		}
		(void)SendIpcMessage(target, type, &g_queue_pair_io_packet,
			VmciQueuePairIoPacketHeaderSize + (payload ? payload_size : 0));
	}

	void CancelQueuePairReceive(VmciQueuePair& queue_pair,
		VmciStatus status) {
		if (!queue_pair.pending_receive_tid) return;
		const stduint target = queue_pair.pending_receive_tid;
		queue_pair.pending_receive_tid = 0;
		queue_pair.pending_receive_size = 0;
		SendQueuePairIoResult(target, VmciMsg::QueuePairReceive, status,
			queue_pair.handle.resource);
	}

	void ReleaseQueuePair(VmciQueuePair& queue_pair) {
		CancelQueuePairReceive(queue_pair, VmciStatus::Unavailable);
		if (queue_pair.dma.length <= g_vmci.queue_pair_dma_bytes) {
			g_vmci.queue_pair_dma_bytes -= queue_pair.dma.length;
		}
		else g_vmci.queue_pair_dma_bytes = 0;
		FreeDmaRegion(queue_pair.dma);
		queue_pair = {};
	}

	void FailQueuePair(VmciQueuePair& queue_pair, VmciStatus status) {
		const stduint owner_tid = queue_pair.owner_tid;
		const VmciQueuePairResult response =
			MakeQueuePairResult(queue_pair, status);
		if (queue_pair.dma.handle) {
			int32 detach_result = 0;
			(void)SendHypervisorRequest(VmciQueuePairDetach,
				&queue_pair.handle, sizeof(queue_pair.handle), detach_result);
		}
		g_vmci.queue_pair_create_failures++;
		ReleaseQueuePair(queue_pair);
		SendQueuePairResult(owner_tid, VmciMsg::QueuePairCreate, response);
	}

	bool SendTransportControl(VmciQueuePair& queue_pair,
		VmciTransportPacketType type, uint64 value = 0) {
		VmciTransportPacket packet = {};
		packet.version = VmciTransportPacketVersion;
		packet.type = _IMM(type);
		if (type == VmciTransportPacketType::Request2) {
			packet.protocol = VmciTransportProtocolPacketOnNotify;
		}
		packet.source_port = queue_pair.local_port;
		packet.destination_port = queue_pair.peer_port;
		packet.size = value;
		if (type == VmciTransportPacketType::Offer ||
			type == VmciTransportPacketType::Attach) {
			packet.handle = queue_pair.handle;
		}
		const VmciHandle destination = {
			queue_pair.peer, VmciTransportResourceId
		};
		// The common VMCI route layer resolves ANY_CID before hardware submission.
		const VmciHandle source = {
			g_vmci.context_id, VmciTransportResourceId
		};
		uint64 result = 0;
		if (!SendVmciDatagram(destination, source, &packet,
			sizeof(packet), result)) {
			if (type != VmciTransportPacketType::Reset) {
				plogwarn("[VMCI] Stream control transport failed type=%u local=%u peer=%08x:%u",
					unsigned(packet.type), unsigned(packet.source_port),
					unsigned(queue_pair.peer),
					unsigned(packet.destination_port));
			}
			return false;
		}
		const int32 vmci_result = int32(uint32(result));
		if (vmci_result < 0) {
			queue_pair.hypervisor_result = vmci_result;
			if (type != VmciTransportPacketType::Reset) {
				plogwarn("[VMCI] Stream control rejected type=%u result=%d local=%u peer=%08x:%u",
					unsigned(packet.type), vmci_result,
					unsigned(packet.source_port), unsigned(queue_pair.peer),
					unsigned(packet.destination_port));
			}
			return false;
		}
		return true;
	}

	void RejectTransportQueuePair(VmciQueuePair& queue_pair,
		VmciStatus status) {
		(void)SendTransportControl(queue_pair, VmciTransportPacketType::Reset);
		FailQueuePair(queue_pair, status);
	}

	uint32 ReadQueuePointer(const volatile uint64& pointer) {
		return *reinterpret_cast<const volatile uint32*>(&pointer);
	}

	void WriteQueuePointer(volatile uint64& pointer, uint32 value) {
		*reinterpret_cast<volatile uint32*>(&pointer) = value;
	}

	void CopyToQueue(uint8* queue, uint32 queue_size, uint32 offset,
		const uint8* source, uint32 length) {
		const uint32 first = minof(length, queue_size - offset);
		MemCopyN(queue + offset, source, first);
		if (first < length) MemCopyN(queue, source + first, length - first);
	}

	void CopyFromQueue(uint8* destination, const uint8* queue,
		uint32 queue_size, uint32 offset, uint32 length) {
		const uint32 first = minof(length, queue_size - offset);
		MemCopyN(destination, queue + offset, first);
		if (first < length) MemCopyN(destination + first, queue, length - first);
	}

	stdsint WriteQueuePair(VmciQueuePair& queue_pair, const uint8* payload,
		uint32 payload_size) {
		const uint32 queue_size = uint32(queue_pair.produce_size);
		const stduint produce_pages = stduint((queue_pair.produce_size +
			VmciPageSize - 1u) / VmciPageSize) + 1u;
		auto* produce_header = reinterpret_cast<VmciQueueHeader*>(
			queue_pair.dma.address);
		auto* consume_header = reinterpret_cast<VmciQueueHeader*>(
			queue_pair.dma.address + produce_pages * VmciPageSize);
		DmaReadBarrier();
		const uint32 tail = ReadQueuePointer(produce_header->producer_tail);
		const uint32 head = ReadQueuePointer(consume_header->consumer_head);
		if (tail >= queue_size || head >= queue_size) return -1;
		const uint32 free_size = tail >= head ?
			queue_size - (tail - head) - 1u : head - tail - 1u;
		if (payload_size > free_size) return 0;
		CopyToQueue(queue_pair.dma.address + VmciPageSize, queue_size, tail,
			payload, payload_size);
		DmaWriteBarrier();
		WriteQueuePointer(produce_header->producer_tail,
			(tail + payload_size) % queue_size);
		DmaWriteBarrier();
		return payload_size;
	}

	stdsint ReadQueuePair(VmciQueuePair& queue_pair, uint8* payload,
		uint32 capacity) {
		const uint32 queue_size = uint32(queue_pair.consume_size);
		const stduint produce_pages = stduint((queue_pair.produce_size +
			VmciPageSize - 1u) / VmciPageSize) + 1u;
		auto* produce_header = reinterpret_cast<VmciQueueHeader*>(
			queue_pair.dma.address);
		auto* consume_header = reinterpret_cast<VmciQueueHeader*>(
			queue_pair.dma.address + produce_pages * VmciPageSize);
		DmaReadBarrier();
		const uint32 tail = ReadQueuePointer(consume_header->producer_tail);
		const uint32 head = ReadQueuePointer(produce_header->consumer_head);
		if (tail >= queue_size || head >= queue_size) return -1;
		const uint32 ready = tail >= head ?
			tail - head : queue_size - head + tail;
		if (!ready) return 0;
		const uint32 length = minof(ready, capacity);
		const uint8* data = queue_pair.dma.address +
			(produce_pages + 1u) * VmciPageSize;
		CopyFromQueue(payload, data, queue_size, head, length);
		DmaWriteBarrier();
		WriteQueuePointer(produce_header->consumer_head,
			(head + length) % queue_size);
		DmaWriteBarrier();
		return length;
	}

	bool CompleteQueuePairReceive(VmciQueuePair& queue_pair) {
		if (!queue_pair.pending_receive_tid) return false;
		const stdsint length = ReadQueuePair(queue_pair,
			g_message_buffer, queue_pair.pending_receive_size);
		if (!length) return false;
		const stduint target = queue_pair.pending_receive_tid;
		queue_pair.pending_receive_tid = 0;
		queue_pair.pending_receive_size = 0;
		if (length < 0 || !SendTransportControl(queue_pair,
			VmciTransportPacketType::Read)) {
			SendQueuePairIoResult(target, VmciMsg::QueuePairReceive,
				VmciStatus::TransportFailed, queue_pair.handle.resource);
			return true;
		}
		SendQueuePairIoResult(target, VmciMsg::QueuePairReceive,
			VmciStatus::Success, queue_pair.handle.resource,
			g_message_buffer, uint32(length));
		return true;
	}

	void SendQueuePairData(stduint source, const VmciQueuePairIoPacket& packet,
		stduint packet_length) {
		VmciQueuePair* queue_pair = FindQueuePair(packet.resource_id);
		if (packet.version != VmciProtocolVersion || !packet.payload_size ||
			packet.payload_size > VmciIpcPayloadCapacity ||
			packet_length < VmciQueuePairIoPacketHeaderSize +
				packet.payload_size) {
			SendQueuePairIoResult(source, VmciMsg::QueuePairSend,
				VmciStatus::InvalidArgument, packet.resource_id);
			return;
		}
		if (!queue_pair) {
			SendQueuePairIoResult(source, VmciMsg::QueuePairSend,
				VmciStatus::NotFound, packet.resource_id);
			return;
		}
		if (queue_pair->owner_tid != source) {
			SendQueuePairIoResult(source, VmciMsg::QueuePairSend,
				VmciStatus::AccessDenied, packet.resource_id);
			return;
		}
		if (queue_pair->awaiting_negotiate || queue_pair->awaiting_attach) {
			SendQueuePairIoResult(source, VmciMsg::QueuePairSend,
				VmciStatus::Unavailable, packet.resource_id);
			return;
		}
		const stdsint length = WriteQueuePair(*queue_pair, packet.payload,
			packet.payload_size);
		if (length <= 0) {
			SendQueuePairIoResult(source, VmciMsg::QueuePairSend,
				length < 0 ? VmciStatus::TransportFailed : VmciStatus::NoResources,
				packet.resource_id);
			return;
		}
		if (!SendTransportControl(*queue_pair, VmciTransportPacketType::Wrote)) {
			SendQueuePairIoResult(source, VmciMsg::QueuePairSend,
				VmciStatus::TransportFailed, packet.resource_id);
			return;
		}
		SendQueuePairIoResult(source, VmciMsg::QueuePairSend,
			VmciStatus::Success, packet.resource_id, nullptr, uint32(length));
	}

	void ReceiveQueuePairData(stduint source,
		const VmciQueuePairIoPacket& packet) {
		VmciQueuePair* queue_pair = FindQueuePair(packet.resource_id);
		if (packet.version != VmciProtocolVersion || !packet.payload_size ||
			packet.payload_size > VmciIpcPayloadCapacity) {
			SendQueuePairIoResult(source, VmciMsg::QueuePairReceive,
				VmciStatus::InvalidArgument, packet.resource_id);
			return;
		}
		if (!queue_pair) {
			SendQueuePairIoResult(source, VmciMsg::QueuePairReceive,
				VmciStatus::NotFound, packet.resource_id);
			return;
		}
		if (queue_pair->owner_tid != source) {
			SendQueuePairIoResult(source, VmciMsg::QueuePairReceive,
				VmciStatus::AccessDenied, packet.resource_id);
			return;
		}
		if (queue_pair->awaiting_negotiate || queue_pair->awaiting_attach ||
			queue_pair->pending_receive_tid) {
			SendQueuePairIoResult(source, VmciMsg::QueuePairReceive,
				VmciStatus::Unavailable, packet.resource_id);
			return;
		}
		queue_pair->pending_receive_tid = source;
		queue_pair->pending_receive_size = packet.payload_size;
		(void)CompleteQueuePairReceive(*queue_pair);
	}

	bool AllocateQueuePairDma(VmciQueuePair& queue_pair) {
		const stduint produce_pages = stduint((queue_pair.produce_size +
			VmciPageSize - 1u) / VmciPageSize) + 1u;
		const stduint consume_pages = stduint((queue_pair.consume_size +
			VmciPageSize - 1u) / VmciPageSize) + 1u;
		const stduint total_pages = produce_pages + consume_pages;
		const stduint dma_size = total_pages * VmciPageSize;
		queue_pair.page_count = uint32(total_pages);
		if (total_pages > VmciQueuePairMaxPages ||
			!AllocateDmaRegion(g_vmci.dev_handle, dma_size, queue_pair.dma,
				_IMM(PwcallDeviceDmaAllocFlag::NoLowMemory)) ||
			(queue_pair.dma.physical & (VmciPageSize - 1u)) ||
			(stduint(queue_pair.dma.address) & (VmciPageSize - 1u))) {
			FreeDmaRegion(queue_pair.dma);
			return false;
		}
		MemSet(g_queue_pair_payload, 0, sizeof(g_queue_pair_payload));
		auto* payload = reinterpret_cast<VmciQueuePairAllocPayload*>(
			g_queue_pair_payload);
		payload->handle = queue_pair.handle;
		payload->peer = queue_pair.peer;
		payload->flags = queue_pair.flags;
		payload->produce_size = queue_pair.produce_size;
		payload->consume_size = queue_pair.consume_size;
		payload->num_ppns = total_pages;
		const bool use_ppn64 = (g_vmci.capabilities_in_use &
			VmciCapabilityPpn64) != 0;
		uint8* ppn_output = g_queue_pair_payload + sizeof(*payload);
		for0(i, total_pages) {
			const uint64 ppn = (queue_pair.dma.physical +
				uint64(i) * VmciPageSize) >> VmciPageShift;
			if (!use_ppn64 && (ppn >> 32) != 0) {
				FreeDmaRegion(queue_pair.dma);
				return false;
			}
			if (use_ppn64) reinterpret_cast<uint64*>(ppn_output)[i] = ppn;
			else reinterpret_cast<uint32*>(ppn_output)[i] = uint32(ppn);
		}
		DmaWriteBarrier();
		const stduint payload_size = sizeof(*payload) + total_pages *
			(use_ppn64 ? sizeof(uint64) : sizeof(uint32));
		if (!SendHypervisorRequest(VmciQueuePairAllocate, payload,
			payload_size, queue_pair.hypervisor_result)) {
			FreeDmaRegion(queue_pair.dma);
			return false;
		}
		if (queue_pair.hypervisor_result < 0) {
			plogerro("[VMCI] Queue pair allocate failed result=%d handle=%08x:%u peer=%08x flags=%08x pages=%u",
				queue_pair.hypervisor_result,
				unsigned(payload->handle.context),
				unsigned(payload->handle.resource), unsigned(payload->peer),
				unsigned(payload->flags), unsigned(payload->num_ppns));
			FreeDmaRegion(queue_pair.dma);
			return false;
		}

		DmaReadBarrier();
		const auto* produce_header = reinterpret_cast<const VmciQueueHeader*>(
			queue_pair.dma.address);
		const auto* consume_header = reinterpret_cast<const VmciQueueHeader*>(
			queue_pair.dma.address + produce_pages * VmciPageSize);
		if (produce_header->handle.context != queue_pair.handle.context ||
			produce_header->handle.resource != queue_pair.handle.resource ||
			consume_header->handle.context != queue_pair.handle.context ||
			consume_header->handle.resource != queue_pair.handle.resource) {
			plogerro("[VMCI] Queue pair headers were not initialized handle=%08x:%u produce=%08x:%u consume=%08x:%u",
				unsigned(queue_pair.handle.context),
				unsigned(queue_pair.handle.resource),
				unsigned(produce_header->handle.context),
				unsigned(produce_header->handle.resource),
				unsigned(consume_header->handle.context),
				unsigned(consume_header->handle.resource));
			int32 detach_result = 0;
			(void)SendHypervisorRequest(VmciQueuePairDetach,
				&queue_pair.handle, sizeof(queue_pair.handle), detach_result);
			queue_pair.hypervisor_result = detach_result < 0 ? detach_result : -1;
			FreeDmaRegion(queue_pair.dma);
			return false;
		}
		g_vmci.queue_pair_dma_bytes += queue_pair.dma.length;
		return true;
	}

	void CreateQueuePair(stduint source,
		const VmciQueuePairCreateRequest& request) {
		g_vmci.queue_pair_create_requests++;
		VmciQueuePairResult response = {};
		response.version = VmciProtocolVersion;
		response.status = int32(VmciStatus::InvalidArgument);
		response.handle.context = g_vmci.context_id;
		response.handle.resource = request.resource_id;
		response.peer = request.peer;
		response.flags = request.flags;
		response.produce_size = request.produce_size;
		response.consume_size = request.consume_size;
		response.local_port = request.local_port;
		response.peer_port = request.peer_port;
		if (!g_vmci.queue_pair_available) {
			response.status = int32(VmciStatus::Unavailable);
			g_vmci.queue_pair_create_failures++;
			SendQueuePairResult(source, VmciMsg::QueuePairCreate, response);
			return;
		}

		if (request.version != VmciProtocolVersion ||
			request.peer != VmciHostContextId ||
			request.flags != _IMM(VmciQueuePairFlag::None) ||
			!request.produce_size || !request.consume_size ||
			request.produce_size != request.consume_size ||
			request.produce_size > VmciQueuePairMaxQueueSize ||
			request.consume_size > VmciQueuePairMaxQueueSize ||
			(request.local_port != VmciInvalidId &&
				request.local_port < VmciFirstDynamicResourceId) ||
			request.peer_port < VmciFirstDynamicResourceId ||
			request.peer_port == VmciInvalidId ||
			(request.resource_id != VmciInvalidId &&
				request.resource_id < VmciFirstDynamicResourceId)) {
			g_vmci.queue_pair_create_failures++;
			SendQueuePairResult(source, VmciMsg::QueuePairCreate, response);
			return;
		}

		VmciQueuePair* queue_pair = FindFreeQueuePair();
		if (!queue_pair) {
			response.status = int32(VmciStatus::NoResources);
			g_vmci.queue_pair_create_failures++;
			SendQueuePairResult(source, VmciMsg::QueuePairCreate, response);
			return;
		}

		uint32 resource_id = request.resource_id;
		if (resource_id == VmciInvalidId) resource_id = AllocateResourceId();
		response.handle.resource = resource_id;
		if (resource_id == VmciInvalidId) {
			response.status = int32(VmciStatus::NoResources);
			g_vmci.queue_pair_create_failures++;
			SendQueuePairResult(source, VmciMsg::QueuePairCreate, response);
			return;
		}
		if (ResourceIdInUse(resource_id)) {
			response.status = int32(VmciStatus::AlreadyExists);
			g_vmci.queue_pair_create_failures++;
			SendQueuePairResult(source, VmciMsg::QueuePairCreate, response);
			return;
		}

		queue_pair->owner_tid = source;
		queue_pair->handle = response.handle;
		queue_pair->peer = request.peer;
		queue_pair->flags = request.flags;
		queue_pair->produce_size = request.produce_size;
		queue_pair->consume_size = request.consume_size;
		queue_pair->local_port = request.local_port == VmciInvalidId ?
			resource_id : request.local_port;
		queue_pair->peer_port = request.peer_port;
		queue_pair->awaiting_negotiate = true;
		queue_pair->active = true;
		if (!SendTransportControl(*queue_pair,
			VmciTransportPacketType::Request2, request.produce_size)) {
			if (queue_pair->hypervisor_result == -2) {
				queue_pair->old_transport_protocol = true;
				queue_pair->hypervisor_result = 0;
				if (SendTransportControl(*queue_pair,
					VmciTransportPacketType::Request, request.produce_size)) return;
			}
			FailQueuePair(*queue_pair, VmciStatus::TransportFailed);
		}
	}

	void DestroyQueuePair(stduint source,
		const VmciQueuePairDestroyRequest& request) {
		g_vmci.queue_pair_detach_requests++;
		VmciQueuePairResult response = {};
		response.version = VmciProtocolVersion;
		response.status = int32(VmciStatus::InvalidArgument);
		response.handle.context = g_vmci.context_id;
		response.handle.resource = request.resource_id;
		if (request.version != VmciProtocolVersion ||
			request.resource_id < VmciFirstDynamicResourceId) {
			g_vmci.queue_pair_detach_failures++;
			SendQueuePairResult(source, VmciMsg::QueuePairDestroy, response);
			return;
		}

		VmciQueuePair* queue_pair = FindQueuePair(request.resource_id);
		if (!queue_pair) response.status = int32(VmciStatus::NotFound);
		else if (queue_pair->owner_tid != source) {
			response.status = int32(VmciStatus::AccessDenied);
		}
		if (!queue_pair || queue_pair->owner_tid != source) {
			g_vmci.queue_pair_detach_failures++;
			SendQueuePairResult(source, VmciMsg::QueuePairDestroy, response);
			return;
		}

		response = MakeQueuePairResult(*queue_pair,
			VmciStatus::InvalidArgument);
		(void)SendTransportControl(*queue_pair, VmciTransportPacketType::Reset);
		int32 hypervisor_result = 0;
		if (!SendHypervisorRequest(VmciQueuePairDetach, &queue_pair->handle,
			sizeof(queue_pair->handle), hypervisor_result)) {
			response.status = int32(VmciStatus::TransportFailed);
			g_vmci.queue_pair_detach_failures++;
			SendQueuePairResult(source, VmciMsg::QueuePairDestroy, response);
			return;
		}
		response.hypervisor_result = hypervisor_result;
		if (hypervisor_result < 0) {
			response.status = int32(VmciStatus::TransportFailed);
			g_vmci.queue_pair_detach_failures++;
			SendQueuePairResult(source, VmciMsg::QueuePairDestroy, response);
			return;
		}

		ReleaseQueuePair(*queue_pair);
		response.status = int32(VmciStatus::Success);
		SendQueuePairResult(source, VmciMsg::QueuePairDestroy, response);
	}

	void DeliverEventDatagram(const VmciDatagram& datagram,
		const uint8* payload) {
		g_vmci.receive_datagrams++;
		g_vmci.event_datagrams++;
		if (datagram.destination.resource != VmciEventHandlerResourceId ||
			datagram.source.context != VmciHypervisorContextId ||
			datagram.payload_size < sizeof(uint32) ||
			datagram.payload_size >
				sizeof(VmciEventDataHeader) + VmciEventPayloadCapacity) {
			g_vmci.event_malformed++;
			g_vmci.malformed_datagrams++;
			return;
		}

		const uint32 event = *reinterpret_cast<const uint32*>(payload);
		if (!IsClientEvent(event)) {
			g_vmci.event_malformed++;
			g_vmci.malformed_datagrams++;
			return;
		}
		VmciEventPacket packet = {};
		packet.version = VmciProtocolVersion;
		packet.status = int32(VmciStatus::Success);
		packet.event = event;
		if (datagram.payload_size > sizeof(VmciEventDataHeader)) {
			packet.payload_size = uint32(datagram.payload_size -
				sizeof(VmciEventDataHeader));
			MemCopyN(packet.payload, payload + sizeof(VmciEventDataHeader),
				packet.payload_size);
		}
		if (event == _IMM(VmciEventType::ContextIdUpdate) &&
			packet.payload_size >= sizeof(VmciEventContextPayload)) {
			VmciEventContextPayload context = {};
			MemCopyN(&context, packet.payload, sizeof(context));
			if (context.context_id > VmciHostContextId &&
				context.context_id < VmciErrorResultFloor &&
				context.context_id != g_vmci.context_id) {
				g_vmci.context_id = context.context_id;
				g_vmci.service_refresh_pending = true;
			}
		}

		uint32 subscribers = 0;
		for0(i, VmciEventSubscriptionCapacity) {
			const VmciEventSubscription& subscription = g_event_subscriptions[i];
			if (!subscription.active || subscription.event != event) continue;
			subscribers++;
			packet.subscription_id = subscription.subscription_id;
			if (SendIpcMessage(subscription.owner_tid, VmciMsg::EventReceive,
				&packet, sizeof(packet))) g_vmci.event_delivered++;
			else g_vmci.event_ipc_drops++;
		}
		if (!subscribers) g_vmci.event_unsubscribed++;
	}

	void SendClientDatagram(stduint source, const VmciDatagramPacket& packet,
		stduint packet_length) {
		VmciDatagramResult response = {};
		response.version = VmciProtocolVersion;
		response.status = int32(VmciStatus::InvalidArgument);
		g_vmci.transmit_requests++;
		if (packet.payload_size > VmciIpcPayloadCapacity) {
			response.status = int32(VmciStatus::PayloadTooLarge);
			g_vmci.transmit_failures++;
			(void)SendIpcMessage(source, VmciMsg::DatagramSend,
				&response, sizeof(response));
			return;
		}
		if (packet.version != VmciProtocolVersion || packet.reserved != 0 ||
			packet_length < VmciDatagramPacketHeaderSize + packet.payload_size ||
			packet.destination.context == VmciInvalidId ||
			packet.destination.resource == VmciInvalidId ||
			packet.source.context != g_vmci.context_id) {
			g_vmci.transmit_failures++;
			(void)SendIpcMessage(source, VmciMsg::DatagramSend,
				&response, sizeof(response));
			return;
		}

		auto* endpoint = FindEndpoint(packet.source.resource);
		if (!endpoint) response.status = int32(VmciStatus::NotFound);
		else if (endpoint->owner_tid != source)
			response.status = int32(VmciStatus::AccessDenied);
		else {
			const VmciHandle destination = packet.destination;
			const VmciHandle source_handle = packet.source;
			uint64 result = 0;
			if (SendVmciDatagram(destination, source_handle,
				packet.payload, packet.payload_size, result)) {
				response.status = int32(VmciStatus::Success);
				response.result = int32(uint32(result));
			}
			else response.status = int32(VmciStatus::TransportFailed);
		}
		if (response.status != int32(VmciStatus::Success)) {
			g_vmci.transmit_failures++;
		}
		(void)SendIpcMessage(source, VmciMsg::DatagramSend,
			&response, sizeof(response));
	}

	bool HandleTransportDatagram(const VmciDatagram& datagram,
		const uint8* payload) {
		if (datagram.destination.resource != VmciTransportResourceId ||
			datagram.source.resource != VmciTransportResourceId) return false;
		if (datagram.destination.context != g_vmci.context_id ||
			datagram.source.context != VmciHostContextId ||
			datagram.payload_size != sizeof(VmciTransportPacket)) return true;

		VmciTransportPacket packet = {};
		MemCopyN(&packet, payload, sizeof(packet));
		VmciQueuePair* queue_pair = FindTransportQueuePair(
			datagram.source.context, packet.destination_port,
			packet.source_port);
		if (!queue_pair || packet.version != VmciTransportPacketVersion ||
			packet.reserved != 0) return true;

		const auto type = VmciTransportPacketType(packet.type);
		const bool old_negotiate = type == VmciTransportPacketType::Negotiate &&
			packet.protocol == 0;
		const bool new_negotiate = type == VmciTransportPacketType::Negotiate2 &&
			packet.protocol == VmciTransportProtocolPacketOnNotify;
		if ((old_negotiate || new_negotiate) &&
			queue_pair->awaiting_negotiate) {
			queue_pair->old_transport_protocol = old_negotiate;
			if (!packet.size || packet.size > VmciQueuePairMaxQueueSize) {
				RejectTransportQueuePair(*queue_pair,
					VmciStatus::InvalidArgument);
				return true;
			}
			queue_pair->produce_size = packet.size;
			queue_pair->consume_size = packet.size;
			queue_pair->awaiting_negotiate = false;
			if (!AllocateQueuePairDma(*queue_pair)) {
				const VmciStatus status = queue_pair->hypervisor_result < 0 ?
					VmciStatus::TransportFailed : VmciStatus::NoResources;
				RejectTransportQueuePair(*queue_pair, status);
				return true;
			}
			if (!SendTransportControl(*queue_pair,
				VmciTransportPacketType::Offer)) {
				RejectTransportQueuePair(*queue_pair,
					VmciStatus::TransportFailed);
				return true;
			}
			queue_pair->awaiting_attach = true;
			return true;
		}

		if (type == VmciTransportPacketType::Attach &&
			queue_pair->awaiting_attach) {
			if (packet.handle.context != queue_pair->handle.context ||
				packet.handle.resource != queue_pair->handle.resource) {
				RejectTransportQueuePair(*queue_pair,
					VmciStatus::InvalidArgument);
				return true;
			}
			queue_pair->awaiting_attach = false;
			const VmciQueuePairResult response =
				MakeQueuePairResult(*queue_pair, VmciStatus::Success);
			SendQueuePairResult(queue_pair->owner_tid,
				VmciMsg::QueuePairCreate, response);
			return true;
		}

		if (type == VmciTransportPacketType::Wrote &&
			!queue_pair->awaiting_negotiate && !queue_pair->awaiting_attach) {
			(void)CompleteQueuePairReceive(*queue_pair);
			return true;
		}
		if (type == VmciTransportPacketType::Read &&
			!queue_pair->awaiting_negotiate && !queue_pair->awaiting_attach) {
			return true;
		}

		if (type == VmciTransportPacketType::Invalid &&
			queue_pair->awaiting_negotiate &&
			!queue_pair->old_transport_protocol) {
			queue_pair->old_transport_protocol = true;
			queue_pair->hypervisor_result = 0;
			if (!SendTransportControl(*queue_pair,
				VmciTransportPacketType::Request, queue_pair->produce_size)) {
				FailQueuePair(*queue_pair, VmciStatus::TransportFailed);
			}
			return true;
		}

		if (type == VmciTransportPacketType::Reset ||
			type == VmciTransportPacketType::Invalid) {
			if (queue_pair->awaiting_negotiate ||
				queue_pair->awaiting_attach) {
				FailQueuePair(*queue_pair, VmciStatus::TransportFailed);
			}
		}
		return true;
	}

	void DeliverDatagram(const VmciDatagram& datagram, const uint8* payload) {
		if (datagram.source.context == VmciHypervisorContextId &&
			datagram.destination.resource == VmciEventHandlerResourceId) {
			DeliverEventDatagram(datagram, payload);
			return;
		}
		g_vmci.receive_datagrams++;
		if (HandleTransportDatagram(datagram, payload)) return;
		if (datagram.destination.context != g_vmci.context_id ||
			datagram.payload_size > VmciIpcPayloadCapacity) {
			g_vmci.receive_unbound++;
			return;
		}
		auto* endpoint = FindEndpoint(datagram.destination.resource);
		if (!endpoint) {
			g_vmci.receive_unbound++;
			return;
		}

		auto* packet = reinterpret_cast<VmciDatagramPacket*>(g_message_buffer);
		packet->version = VmciProtocolVersion;
		packet->status = int32(VmciStatus::Success);
		packet->destination = datagram.destination;
		packet->source = datagram.source;
		packet->payload_size = uint32(datagram.payload_size);
		packet->reserved = 0;
		if (packet->payload_size) {
			MemCopyN(packet->payload, payload, packet->payload_size);
		}
		if (SendIpcMessage(endpoint->owner_tid, VmciMsg::DatagramReceive,
			packet, VmciDatagramPacketHeaderSize + packet->payload_size)) {
			g_vmci.receive_delivered++;
		}
		else g_vmci.receive_ipc_drops++;
	}

	bool ParseReceivedDatagrams() {
		const uint8* input = g_vmci.receive_buffer + VmciPageSize;
		stduint remaining = VmciMaxDatagramSize;
		while (remaining >= sizeof(VmciDatagram)) {
			const auto* datagram = reinterpret_cast<const VmciDatagram*>(input);
			if (datagram->destination.resource == VmciInvalidId) break;
			if (datagram->payload_size >
				uint64(VmciMaxDatagramSize - sizeof(VmciDatagram))) {
				g_vmci.malformed_datagrams++;
				plogwarn("[VMCI] Drop malformed datagram payload=%u",
					unsigned(uint32(datagram->payload_size)));
				return true;
			}
			const stduint datagram_size = sizeof(VmciDatagram) +
				stduint(datagram->payload_size);
			const stduint aligned_size = (datagram_size + 7u) & ~stduint(7u);
			if (aligned_size > remaining) {
				g_vmci.malformed_datagrams++;
				plogwarn("[VMCI] Drop truncated datagram size=%u remaining=%u",
					unsigned(aligned_size), unsigned(remaining));
				return true;
			}
			DeliverDatagram(*datagram, input + sizeof(VmciDatagram));

			input += aligned_size;
			remaining -= aligned_size;
		}
		return true;
	}

	bool ReceiveDmaDatagrams() {
		if (!g_vmci.receive_buffer) return false;
		auto* header = reinterpret_cast<VmciDmaHeader*>(g_vmci.receive_buffer);
		auto* scatter_gather = reinterpret_cast<VmciScatterGatherElement*>(
			g_vmci.receive_buffer + sizeof(VmciDmaHeader));
		MemSet(header, 0, sizeof(VmciDmaHeader));
		header->opcode = 1;
		header->size = 1;
		scatter_gather[0].address = g_vmci.receive_physical + VmciPageSize;
		scatter_gather[0].size = VmciMaxDatagramSize;
		header->busy = 0;
		DmaWriteBarrier();
		if (!WriteRegister(VmciRegister::DataInLow,
			uint32(g_vmci.receive_physical)) ||
			!WaitForDmaBusy(header->busy, 1)) return false;
		return ParseReceivedDatagrams();
	}

	bool EnableInterrupts() {
		uint32 interrupt_mask = VmciInterruptDatagram |
			VmciInterruptDmaDatagram;
		if (g_vmci.capabilities_in_use & VmciCapabilityNotifications) {
			interrupt_mask |= VmciInterruptNotification;
		}
		if (!WriteRegister(VmciRegister::InterruptMask,
			interrupt_mask) ||
			!WriteRegister(VmciRegister::Control,
				VmciControlInterruptEnable)) return false;
		return true;
	}

	bool ServiceInterrupt() {
		for0(i, VmciInterruptDrainLimit) {
			uint32 cause = 0;
			if (!ReadRegister(VmciRegister::InterruptCause, cause)) return false;
			if (cause == 0) return true;
			if (cause == 0xFFFFFFFFu) {
				plogerro("[VMCI] Invalid interrupt cause");
				return false;
			}

			const uint32 unknown = cause & ~VmciKnownInterrupts;
			if (unknown) {
				plogwarn("[VMCI] Unknown interrupt cause=%08x", unsigned(unknown));
			}
			if (cause & VmciInterruptDatagram) {
				if (!ReceiveDmaDatagrams()) {
					plogerro("[VMCI] Receive DMA failed malformed=%u",
						unsigned(g_vmci.malformed_datagrams));
					return false;
				}
			}
			if (cause & VmciInterruptNotification) {
				ScanNotificationBitmap();
			}
		}
		return true;
	}

	void ShutdownVmci() {
		if (g_vmci.dev_handle && (g_vmci.use_mmio || g_vmci.io.length)) {
			(void)WriteRegister(VmciRegister::InterruptMask, 0);
			(void)WriteRegister(VmciRegister::Control,
				VmciControlInterruptDisable);
			(void)WriteRegister(VmciRegister::Control, VmciControlReset);
		}
		g_vmci.receive_buffer = nullptr;
		g_vmci.transmit_buffer = nullptr;
		g_vmci.notification_bitmap = nullptr;
		g_vmci.receive_physical = 0;
		g_vmci.transmit_physical = 0;
		g_vmci.notification_physical = 0;
		for0(i, VmciDoorbellCapacity) g_doorbells[i] = {};
		for0(i, VmciEventSubscriptionCapacity) g_event_subscriptions[i] = {};
		for0(i, VmciQueuePairCapacity) {
			if (g_queue_pairs[i].active || g_queue_pairs[i].dma.handle) {
				ReleaseQueuePair(g_queue_pairs[i]);
			}
		}
		FreeDmaRegion(g_vmci.dma);
		if (g_vmci.mapped_address) {
			(void)Powercall::DevUmap(g_vmci.mapped_address, stduint(VmciMmioRegisterSize));
		}
		g_vmci.mapped_address = nullptr;
		g_vmci.registers = nullptr;
		g_vmci.use_mmio = false;
		if (g_vmci.dev_handle) (void)Powercall::DevClose(g_vmci.dev_handle);
		g_vmci.dev_handle = 0;
	}

	bool InitializeVmci() {
		const stduint cls = (stduint(VmciVendorId) << 16) | VmciDeviceId;
		const stdsint opened = Powercall::DevOpen(0, cls,
			_IMM(PwcallDeviceOpenFlag::Interrupt));
		if (opened <= 0) return false;
		g_vmci.dev_handle = stduint(opened);
		if (Powercall::DevGetIdentity(g_vmci.dev_handle, &g_vmci.identity) != 0 ||
			g_vmci.identity.vendor_id != VmciVendorId ||
			g_vmci.identity.device_id != VmciDeviceId) return false;
		if (!FindResource(g_vmci.dev_handle, PwcallDeviceResourceType::IrqLine,
			0, g_vmci.irq) || !SelectRegisterAccess() || !InitializeRegisters()) return false;

		return ValidateCapabilities() && ConfigureDmaDatagrams();
	}

	bool StartVmci() {
		if (!EnableInterrupts() || !QueryContextId()) return false;
		g_vmci.queue_pair_available = QueryQueuePairResources();
		return true;
	}

	bool RegisterVmciService() {
		VmciDriverInfo request = MakeDriverInfo(VmciStatus::Success);
		CommMsg message = {};
		message.data.address = _IMM(&request);
		message.data.length = sizeof(request);
		message.type = _IMM(VmciMsg::DriverAttach);
		if (Powercall::SysComm(COMM_SEND, Task_Devsman, &message) != 0) {
			return false;
		}

		VmciDriverInfo response = {};
		message = {};
		message.data.address = _IMM(&response);
		message.data.length = sizeof(response);
		if (Powercall::SysComm(COMM_RECV, Task_Devsman, &message) != 0 ||
			message.type != _IMM(VmciMsg::DriverAttach) ||
			message.data.length < sizeof(response) ||
			response.version != VmciProtocolVersion ||
			response.status != int32(VmciStatus::Success) ||
			response.context_id != g_vmci.context_id || !response.driver_tid) {
			return false;
		}
		if (response.features != request.features ||
			response.doorbell_capacity != request.doorbell_capacity ||
			response.event_subscription_capacity !=
				request.event_subscription_capacity ||
			response.queue_pair_capacity != request.queue_pair_capacity ||
			response.queue_pair_max_queue_size !=
				request.queue_pair_max_queue_size) return false;
		g_vmci.driver_tid = response.driver_tid;
		return true;
	}

	void DispatchIpcMessage(stduint type, stduint source, stduint length) {
		switch (VmciMsg(type)) {
		case VmciMsg::GetInfo:
		{
			const auto info = MakeDriverInfo(VmciStatus::Success);
			(void)SendIpcMessage(source, VmciMsg::GetInfo, &info, sizeof(info));
			break;
		}
		case VmciMsg::EndpointCreate:
		case VmciMsg::EndpointDestroy:
		{
			if (length < sizeof(VmciEndpointRequest)) {
				ReplyEndpoint(source, VmciMsg(type), VmciStatus::InvalidArgument);
				break;
			}
			VmciEndpointRequest request = {};
			MemCopyN(&request, g_message_buffer, sizeof(request));
			if (type == _IMM(VmciMsg::EndpointCreate))
				CreateEndpoint(source, request);
			else DestroyEndpoint(source, request);
			break;
		}
		case VmciMsg::DoorbellCreate:
		case VmciMsg::DoorbellDestroy:
		{
			if (length < sizeof(VmciDoorbellRequest)) {
				ReplyDoorbell(source, VmciMsg(type),
					VmciStatus::InvalidArgument);
				break;
			}
			VmciDoorbellRequest request = {};
			MemCopyN(&request, g_message_buffer, sizeof(request));
			if (type == _IMM(VmciMsg::DoorbellCreate))
				CreateDoorbell(source, request);
			else DestroyDoorbell(source, request);
			break;
		}
		case VmciMsg::DoorbellNotify:
		{
			if (length < sizeof(VmciDoorbellNotifyRequest)) {
				ReplyDoorbell(source, VmciMsg::DoorbellNotify,
					VmciStatus::InvalidArgument);
				break;
			}
			VmciDoorbellNotifyRequest request = {};
			MemCopyN(&request, g_message_buffer, sizeof(request));
			NotifyDoorbell(source, request);
			break;
		}
		case VmciMsg::EventSubscribe:
		{
			if (length < sizeof(VmciEventSubscribeRequest)) {
				ReplyEventSubscription(source, VmciMsg::EventSubscribe,
					VmciStatus::InvalidArgument);
				break;
			}
			VmciEventSubscribeRequest request = {};
			MemCopyN(&request, g_message_buffer, sizeof(request));
			SubscribeEvent(source, request);
			break;
		}
		case VmciMsg::EventUnsubscribe:
		{
			if (length < sizeof(VmciEventUnsubscribeRequest)) {
				ReplyEventSubscription(source, VmciMsg::EventUnsubscribe,
					VmciStatus::InvalidArgument);
				break;
			}
			VmciEventUnsubscribeRequest request = {};
			MemCopyN(&request, g_message_buffer, sizeof(request));
			UnsubscribeEvent(source, request);
			break;
		}
		case VmciMsg::QueuePairCreate:
		{
			if (length < sizeof(VmciQueuePairCreateRequest)) {
				VmciQueuePairResult result = {};
				result.version = VmciProtocolVersion;
				result.status = int32(VmciStatus::InvalidArgument);
				SendQueuePairResult(source, VmciMsg::QueuePairCreate, result);
				break;
			}
			VmciQueuePairCreateRequest request = {};
			MemCopyN(&request, g_message_buffer, sizeof(request));
			CreateQueuePair(source, request);
			break;
		}
		case VmciMsg::QueuePairDestroy:
		{
			if (length < sizeof(VmciQueuePairDestroyRequest)) {
				VmciQueuePairResult result = {};
				result.version = VmciProtocolVersion;
				result.status = int32(VmciStatus::InvalidArgument);
				SendQueuePairResult(source, VmciMsg::QueuePairDestroy, result);
				break;
			}
			VmciQueuePairDestroyRequest request = {};
			MemCopyN(&request, g_message_buffer, sizeof(request));
			DestroyQueuePair(source, request);
			break;
		}
		case VmciMsg::QueuePairSend:
		{
			if (length < VmciQueuePairIoPacketHeaderSize) {
				SendQueuePairIoResult(source, VmciMsg::QueuePairSend,
					VmciStatus::InvalidArgument, VmciInvalidId);
				break;
			}
			const auto& packet =
				*reinterpret_cast<const VmciQueuePairIoPacket*>(g_message_buffer);
			SendQueuePairData(source, packet, length);
			break;
		}
		case VmciMsg::QueuePairReceive:
		{
			if (length < VmciQueuePairIoPacketHeaderSize) {
				SendQueuePairIoResult(source, VmciMsg::QueuePairReceive,
					VmciStatus::InvalidArgument, VmciInvalidId);
				break;
			}
			const auto& packet =
				*reinterpret_cast<const VmciQueuePairIoPacket*>(g_message_buffer);
			ReceiveQueuePairData(source, packet);
			break;
		}
		case VmciMsg::DatagramSend:
		{
			if (length < VmciDatagramPacketHeaderSize) {
				VmciDatagramResult result = {};
				result.version = VmciProtocolVersion;
				result.status = int32(VmciStatus::InvalidArgument);
				g_vmci.transmit_requests++;
				g_vmci.transmit_failures++;
				(void)SendIpcMessage(source, VmciMsg::DatagramSend,
					&result, sizeof(result));
				break;
			}
			const auto& packet = *reinterpret_cast<const VmciDatagramPacket*>(
				g_message_buffer);
			SendClientDatagram(source, packet, length);
			break;
		}
		case VmciMsg::StatsQuery:
		{
			VmciStats stats = {};
			stats.version = VmciProtocolVersion;
			stats.status = int32(VmciStatus::Success);
			stats.endpoint_count = CountEndpoints();
			stats.endpoint_capacity = VmciEndpointCapacity;
			stats.transmit_requests = g_vmci.transmit_requests;
			stats.transmit_failures = g_vmci.transmit_failures;
			stats.receive_datagrams = g_vmci.receive_datagrams;
			stats.receive_delivered = g_vmci.receive_delivered;
			stats.receive_unbound = g_vmci.receive_unbound;
			stats.receive_malformed = g_vmci.malformed_datagrams;
			stats.receive_ipc_drops = g_vmci.receive_ipc_drops;
			stats.doorbell_count = CountDoorbells();
			stats.doorbell_capacity = VmciDoorbellCapacity;
			stats.doorbell_notify_requests = g_vmci.doorbell_notify_requests;
			stats.doorbell_notify_failures = g_vmci.doorbell_notify_failures;
			stats.doorbell_interrupts = g_vmci.doorbell_interrupts;
			stats.doorbell_delivered = g_vmci.doorbell_delivered;
			stats.doorbell_ipc_drops = g_vmci.doorbell_ipc_drops;
			stats.event_subscription_count = CountEventSubscriptions();
			stats.event_subscription_capacity = VmciEventSubscriptionCapacity;
			stats.event_datagrams = g_vmci.event_datagrams;
			stats.event_delivered = g_vmci.event_delivered;
			stats.event_unsubscribed = g_vmci.event_unsubscribed;
			stats.event_malformed = g_vmci.event_malformed;
			stats.event_ipc_drops = g_vmci.event_ipc_drops;
			stats.queue_pair_count = CountQueuePairs();
			stats.queue_pair_capacity = VmciQueuePairCapacity;
			stats.queue_pair_create_requests =
				g_vmci.queue_pair_create_requests;
			stats.queue_pair_create_failures =
				g_vmci.queue_pair_create_failures;
			stats.queue_pair_detach_requests =
				g_vmci.queue_pair_detach_requests;
			stats.queue_pair_detach_failures =
				g_vmci.queue_pair_detach_failures;
			stats.queue_pair_dma_bytes = g_vmci.queue_pair_dma_bytes;
			(void)SendIpcMessage(source, VmciMsg::StatsQuery,
				&stats, sizeof(stats));
			break;
		}
		default:
			break;
		}
	}

	bool ReceiveMessage() {
		MemSet(g_message_buffer, 0, sizeof(g_message_buffer));
		CommMsg message = {};
		message.data.address = _IMM(g_message_buffer);
		message.data.length = sizeof(g_message_buffer);
		if (Powercall::SysComm(COMM_RECV, ANYPROC, &message) != 0) {
			plogerro("[VMCI] Message receive failed");
			return false;
		}
		if (message.type != _IMM(KernelMsg::DeviceEvent)) {
			DispatchIpcMessage(message.type, message.src, message.data.length);
			return true;
		}
		if (message.data.length < sizeof(DeviceEvent)) return true;
		DeviceEvent event = {};
		MemCopyN(&event, g_message_buffer, sizeof(event));
		if (event.version != DeviceEventProtocolVersion ||
			event.device_handle != g_vmci.dev_handle) return true;
		switch (DeviceEventKind(event.kind)) {
		case DeviceEventKind::Interrupt:
		{
			const bool serviced = ServiceInterrupt();
			const bool acknowledged = Powercall::DevAck(g_vmci.dev_handle,
				stduint(event.sequence), event.generation) == 0;
			if (!acknowledged) plogerro("[VMCI] Interrupt acknowledgement failed");
			return serviced && acknowledged;
		}
		case DeviceEventKind::Removed:
		case DeviceEventKind::Fault:
		case DeviceEventKind::Shutdown:
			return false;
		default:
			return true;
		}
	}
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;

	if (Powercall::Hello() != 0) {
		plogerro("[VMCI] Powercall handshake failed");
		return -1;
	}
	if (!InitializeVmci()) {
		plogerro("[VMCI] Initialization failed");
		ShutdownVmci();
		return -1;
	}
	if (Powercall::DevPublish(g_vmci.dev_handle,
		PwcallDevicePublishCommand::Started) != 0) {
		plogerro("[VMCI] Device publish failed");
		ShutdownVmci();
		return -1;
	}
	if (!StartVmci()) {
		plogerro("[VMCI] Datagram start failed");
		ShutdownVmci();
		return -1;
	}
	if (!RegisterVmciService()) {
		plogerro("[VMCI] Service registration failed");
		ShutdownVmci();
		return -1;
	}
	ploginfo("[VMCI] Started pci=%02x:%02x.%u cid=%08x caps=%08x irq=%u qp=%u",
		unsigned(g_vmci.identity.pci_bus), unsigned(g_vmci.identity.pci_device),
		unsigned(g_vmci.identity.pci_function), unsigned(g_vmci.context_id),
		unsigned(g_vmci.capabilities_in_use), unsigned(g_vmci.irq.start),
		unsigned(g_vmci.queue_pair_available));
	while (ReceiveMessage()) {
		if (!g_vmci.service_refresh_pending) continue;
		if (RegisterVmciService()) g_vmci.service_refresh_pending = false;
		else plogwarn("[VMCI] Context update publish failed");
	}
	ShutdownVmci();
	return -1;
}

#else

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	return -1;
}

#endif
