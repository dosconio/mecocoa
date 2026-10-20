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
	constexpr stduint VmciDmaRegionSize = 2u * VmciDmaBufferSize;
	constexpr stduint VmciDmaPollLimit = 1000000;
	constexpr stduint VmciInterruptDrainLimit = 8;
	constexpr uint32 VmciGetContextId = 1;
	constexpr uint32 VmciErrorResultFloor = uint32(0u - 1000u);

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

	static_assert(sizeof(VmciDmaHeader) == 24, "VMCI DMA header size mismatch");
	static_assert(sizeof(VmciScatterGatherElement) == 16,
		"VMCI scatter/gather element size mismatch");
	static_assert(sizeof(VmciDatagram) == 24, "VMCI datagram header size mismatch");
	static_assert(VmciDmaBufferSize == 73728, "VMCI DMA buffer size mismatch");
	static_assert(VmciDmaRegionSize == 147456, "VMCI DMA region size mismatch");
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
		uint64 receive_physical = 0;
		uint64 transmit_physical = 0;
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
		bool use_mmio = false;
	};

	struct VmciEndpoint {
		stduint owner_tid = 0;
		uint32 resource_id = VmciInvalidId;
		bool active = false;
	};

	VmciContext g_vmci{};
	VmciEndpoint g_endpoints[VmciEndpointCapacity]{};
	uint32 g_next_resource_id = VmciFirstDynamicResourceId;
	alignas(stduint) uint8 g_message_buffer[sizeof(VmciDatagramPacket)]{};

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
		VmciDmaRegion& region) {
		const stdsint dma_handle = Powercall::DevDmaAlloc(dev_handle, size, 0);
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
		g_vmci.capabilities_in_use = VmciCapabilityDatagram |
			VmciCapabilityDmaDatagram;
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

	uint32 AllocateResourceId() {
		for0(i, VmciEndpointCapacity + 1) {
			const uint32 candidate = g_next_resource_id++;
			if (g_next_resource_id == VmciInvalidId ||
				g_next_resource_id < VmciFirstDynamicResourceId) {
				g_next_resource_id = VmciFirstDynamicResourceId;
			}
			if (candidate >= VmciFirstDynamicResourceId &&
				candidate != VmciInvalidId && !FindEndpoint(candidate)) return candidate;
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
		if (FindEndpoint(resource_id)) {
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

	void DeliverDatagram(const VmciDatagram& datagram, const uint8* payload) {
		g_vmci.receive_datagrams++;
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
		const uint32 interrupt_mask = VmciInterruptDatagram |
			VmciInterruptDmaDatagram;
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
				plogwarn("[VMCI] Notification interrupt without bitmap");
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
		g_vmci.receive_physical = 0;
		g_vmci.transmit_physical = 0;
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
		return EnableInterrupts() && QueryContextId();
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
	ploginfo("[VMCI] Started pci=%02x:%02x.%u cid=%08x caps=%08x irq=%u",
		unsigned(g_vmci.identity.pci_bus), unsigned(g_vmci.identity.pci_device),
		unsigned(g_vmci.identity.pci_function), unsigned(g_vmci.context_id),
		unsigned(g_vmci.capabilities_in_use), unsigned(g_vmci.irq.start));
	while (ReceiveMessage()) {}
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
