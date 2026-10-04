// ASCII g++ TAB4 LF
// ModuTitle: AMD PCnet-PCI II Ring1 Driver
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include <stdio.h>
#include <c/stdinc.h>
#include "../../include/taskman.com.hpp"
#include "../../include/syscall-pow.hpp"

#if defined(_ACCM) && ((_ACCM & 0xFF00) == 0x8600)

namespace {
	static_assert(sizeof(DeviceEvent) <= sizeof(FMT_NetworkMsg_DRV_FRAME));
	constexpr uint16 LanceVendorId = 0x1022u;
	constexpr uint16 LanceDeviceId = 0x2000u;
	constexpr uint16 LancePartId = 0x2621u;
	constexpr uint16 LanceChipSignature = 0x0003u;
	constexpr uint16 LanceSoftwareStyle32 = 0x0002u;

	enum class LanceIoOffset : uint32 {
		Aprom = 0x00u,
		Rdp = 0x10u,
		Rap = 0x12u,
		Reset = 0x14u,
		Bdp = 0x16u,
	};

	constexpr uint16 LanceCsr0Init = 1u << 0;
	constexpr uint16 LanceCsr0Start = 1u << 1;
	constexpr uint16 LanceCsr0Stop = 1u << 2;
	constexpr uint16 LanceCsr0Tdmd = 1u << 3;
	constexpr uint16 LanceCsr0TxOn = 1u << 4;
	constexpr uint16 LanceCsr0RxOn = 1u << 5;
	constexpr uint16 LanceCsr0Iena = 1u << 6;
	constexpr uint16 LanceCsr0Idon = 1u << 8;
	constexpr uint16 LanceCsr0Tint = 1u << 9;
	constexpr uint16 LanceCsr0Rint = 1u << 10;
	constexpr uint16 LanceCsr0Merr = 1u << 11;
	constexpr uint16 LanceCsr0Miss = 1u << 12;
	constexpr uint16 LanceCsr0Cerr = 1u << 13;
	constexpr uint16 LanceCsr0Babl = 1u << 14;
	constexpr uint16 LanceCsr0Error = 1u << 15;
	constexpr uint16 LanceCsr0AcknowledgeMask =
		LanceCsr0Idon | LanceCsr0Tint | LanceCsr0Rint | LanceCsr0Merr |
		LanceCsr0Miss | LanceCsr0Cerr | LanceCsr0Babl;
	constexpr uint16 LanceCsr0FatalMask = LanceCsr0Merr | LanceCsr0Babl;
	constexpr uint16 LanceCsr0Index = 0;
	constexpr uint16 LanceCsr1Index = 1;
	constexpr uint16 LanceCsr2Index = 2;
	constexpr uint16 LanceCsr15Index = 15;
	constexpr uint16 LanceCsr88Index = 88;
	constexpr uint16 LanceCsr89Index = 89;
	constexpr uint16 LanceBcr2Index = 2;
	constexpr uint16 LanceBcr4Index = 4;
	constexpr uint16 LanceBcr20Index = 20;
	constexpr uint16 LanceBcr2AutoSelect = 1u << 1;
	constexpr uint16 LanceBcr4LedOut = 1u << 15;
	constexpr uint16 LanceModePortSelectMask = 0x0180u;
	constexpr uint16 LanceModePortAui = 0x0000u;
	constexpr uint16 LanceModePort10BaseT = 0x0080u;
	constexpr uint64 LanceRequiredIoLength = _IMM(LanceIoOffset::Bdp) + sizeof(uint16);
	constexpr stduint LanceDescriptorAlignment = 16;
	constexpr stduint LanceRxRingLog2 = 3;
	constexpr stduint LanceTxRingLog2 = 2;
	constexpr stduint LanceRxDescriptorCount = stduint(1) << LanceRxRingLog2;
	constexpr stduint LanceTxDescriptorCount = stduint(1) << LanceTxRingLog2;
	constexpr stduint LanceTxQueueCount = 4;
	constexpr stduint LanceFrameBufferSize = 1600;
	constexpr stduint LanceEthernetMinFrame = 60;
	constexpr stduint LanceEthernetMaxFrame = 1518;
	constexpr stduint LanceEthernetFcsSize = 4;
	constexpr stduint LanceInitPollLimit = 10000;
	constexpr uint32 LanceDescriptorOwn = 1u << 31;
	constexpr uint32 LanceDescriptorError = 1u << 30;
	constexpr uint32 LanceTxDescriptorAddFcs = 1u << 29;
	constexpr uint32 LanceDescriptorStart = 1u << 25;
	constexpr uint32 LanceDescriptorEnd = 1u << 24;
	constexpr uint32 LanceDescriptorOnes = 0x0000F000u;
	constexpr uint32 LanceRxMessageLengthMask = 0x00000FFFu;
	constexpr uint16 LanceRxBufferLength = uint16(0u - uint32(LanceFrameBufferSize));

	_PACKED(struct) LanceInitBlock32 {
		uint16 mode;
		uint16 ring_lengths;
		uint8 physical_address[6];
		uint16 reserved;
		uint32 logical_address_filter[2];
		uint32 rx_ring;
		uint32 tx_ring;
	};

	_PACKED(struct) LanceDescriptor32 {
		uint32 buffer_address;
		uint32 status_length;
		uint32 message_status;
		uint32 reserved;
	};

	static_assert(sizeof(LanceInitBlock32) == 28, "LANCE init block size mismatch");
	static_assert(sizeof(LanceDescriptor32) == 16, "LANCE descriptor size mismatch");
	static_assert(LanceRxBufferLength == 0xF9C0u, "LANCE receive BCNT mismatch");
	static_assert(LanceFrameBufferSize >= LanceEthernetMaxFrame,
		"LANCE frame buffer is too small");

	constexpr stduint AlignUp(stduint value, stduint alignment) {
		return (value + alignment - 1) & ~(alignment - 1);
	}

	constexpr stduint LanceInitOffset = 0;
	constexpr stduint LanceRxDescriptorOffset =
		AlignUp(LanceInitOffset + sizeof(LanceInitBlock32), LanceDescriptorAlignment);
	constexpr stduint LanceTxDescriptorOffset =
		AlignUp(LanceRxDescriptorOffset + sizeof(LanceDescriptor32) * LanceRxDescriptorCount,
			LanceDescriptorAlignment);
	constexpr stduint LanceRxBufferOffset =
		AlignUp(LanceTxDescriptorOffset + sizeof(LanceDescriptor32) * LanceTxDescriptorCount,
			LanceDescriptorAlignment);
	constexpr stduint LanceTxBufferOffset =
		AlignUp(LanceRxBufferOffset + LanceFrameBufferSize * LanceRxDescriptorCount,
			LanceDescriptorAlignment);
	constexpr stduint LanceDmaSize =
		LanceTxBufferOffset + LanceFrameBufferSize * LanceTxDescriptorCount;
	static_assert(LanceDmaSize == 19424, "LANCE DMA layout size mismatch");

	struct LanceDmaRegion {
		stduint handle = 0;
		uint64 physical = 0;
		uint64 length = 0;
		uint8* address = nullptr;
	};

	struct LanceTxQueuedFrame {
		uint16 length = 0;
		uint8 data[LanceEthernetMaxFrame]{};
	};

	struct LanceContext {
		stduint dev_handle = 0;
		PwcallDeviceResourceInfo io = {};
		LanceDmaRegion dma = {};
		volatile LanceDescriptor32* rx_descriptors = nullptr;
		volatile LanceDescriptor32* tx_descriptors = nullptr;
		uint8* rx_buffers = nullptr;
		uint8* tx_buffers = nullptr;
		uint8 mac[6]{};
		uint32 rx_index = 0;
		uint32 tx_index = 0;
		LanceTxQueuedFrame tx_queue[LanceTxQueueCount]{};
		stduint tx_queue_head = 0;
		stduint tx_queue_tail = 0;
		stduint tx_queue_count = 0;
		uint32 rx_errors = 0;
		uint32 tx_errors = 0;
		uint32 missed_frames = 0;
		uint16 bcr2 = 0;
		uint16 bcr4 = 0;
		uint16 csr15 = 0;
		bool io_ready = false;
		bool rings_ready = false;
		bool link_up = false;
		bool faulted = false;
	};

	LanceContext g_lance{};

	bool FindResource(stduint dev_handle, PwcallDeviceResourceType type,
		PwcallDeviceResourceInfo& resource) {
		const stdsint count = Powercall::DevGetResourceCount(dev_handle);
		if (count <= 0) return false;
		for (stdsint i = 0; i < count; ++i) {
			PwcallDeviceResourceQuery query = {};
			query.index = uint32(i);
			if (Powercall::DevGetResource(dev_handle, &query) != 0) continue;
			if (PwcallDeviceResourceType(query.resource.type) != type) continue;
			resource = query.resource;
			return true;
		}
		return false;
	}

	bool ReadIo(stduint dev_handle, const PwcallDeviceResourceInfo& io,
		LanceIoOffset offset, uint32 width, uint32& value) {
		PwcallDeviceIoRequest request = {};
		request.resource_type = _IMM(PwcallDeviceResourceType::PciBarIo);
		request.resource_index = io.index;
		request.width = width;
		request.offset = _IMM(offset);
		if (Powercall::DevIoRead(dev_handle, &request) != 0) return false;
		value = request.value;
		return true;
	}

	bool WriteIo(stduint dev_handle, const PwcallDeviceResourceInfo& io,
		LanceIoOffset offset, uint32 width, uint32 value) {
		PwcallDeviceIoRequest request = {};
		request.resource_type = _IMM(PwcallDeviceResourceType::PciBarIo);
		request.resource_index = io.index;
		request.width = width;
		request.offset = _IMM(offset);
		request.value = value;
		return Powercall::DevIoWrite(dev_handle, &request) == 0;
	}

	bool ReadCsr(stduint dev_handle, const PwcallDeviceResourceInfo& io,
		uint16 index, uint16& value) {
		if (!WriteIo(dev_handle, io, LanceIoOffset::Rap, sizeof(uint16), index)) return false;
		uint32 raw = 0;
		if (!ReadIo(dev_handle, io, LanceIoOffset::Rdp, sizeof(uint16), raw)) return false;
		value = uint16(raw);
		return true;
	}

	bool WriteCsr(stduint dev_handle, const PwcallDeviceResourceInfo& io,
		uint16 index, uint16 value) {
		if (!WriteIo(dev_handle, io, LanceIoOffset::Rap, sizeof(uint16), index)) return false;
		return WriteIo(dev_handle, io, LanceIoOffset::Rdp, sizeof(uint16), value);
	}

	bool ReadBcr(stduint dev_handle, const PwcallDeviceResourceInfo& io,
		uint16 index, uint16& value) {
		if (!WriteIo(dev_handle, io, LanceIoOffset::Rap, sizeof(uint16), index)) return false;
		uint32 raw = 0;
		if (!ReadIo(dev_handle, io, LanceIoOffset::Bdp, sizeof(uint16), raw)) return false;
		value = uint16(raw);
		return true;
	}

	bool WriteBcr(stduint dev_handle, const PwcallDeviceResourceInfo& io,
		uint16 index, uint16 value) {
		if (!WriteIo(dev_handle, io, LanceIoOffset::Rap, sizeof(uint16), index)) return false;
		return WriteIo(dev_handle, io, LanceIoOffset::Bdp, sizeof(uint16), value);
	}

	void DmaWriteBarrier() {
		_ASM volatile ("mfence":::"memory");
	}

	void DmaReadBarrier() {
		_ASM volatile ("mfence":::"memory");
	}

	bool AllocDmaRegion(stduint dev_handle, LanceDmaRegion& region) {
		const stdsint dma_handle = Powercall::DevDmaAlloc(dev_handle, LanceDmaSize, 0);
		if (dma_handle <= 0) return false;
		region.handle = stduint(dma_handle);

		PwcallDeviceDmaMapRequest request = {};
		request.map_flags = _IMM(PwcallDeviceDmaMapFlag::Writable);
		if (Powercall::DevDmaMap(region.handle, &request) != 0 ||
			!request.address || !request.physical || request.length < LanceDmaSize ||
			request.physical > 0xFFFFFFFFull ||
			uint64(LanceDmaSize - 1) > 0xFFFFFFFFull - request.physical) {
			Powercall::DevDmaFree(region.handle);
			region = {};
			return false;
		}

		region.physical = request.physical;
		region.length = request.length;
		region.address = reinterpret_cast<uint8*>(stduint(request.address));
		MemSet(region.address, 0, stduint(region.length));
		return true;
	}

	void FreeDmaRegion(LanceDmaRegion& region) {
		if (region.handle) Powercall::DevDmaFree(region.handle);
		region = {};
	}

	void BuildDmaStructures(LanceContext& lance) {
		auto& dma = lance.dma;
		auto* init = reinterpret_cast<LanceInitBlock32*>(dma.address + LanceInitOffset);
		lance.rx_descriptors = reinterpret_cast<volatile LanceDescriptor32*>(
			dma.address + LanceRxDescriptorOffset);
		lance.tx_descriptors = reinterpret_cast<volatile LanceDescriptor32*>(
			dma.address + LanceTxDescriptorOffset);
		lance.rx_buffers = dma.address + LanceRxBufferOffset;
		lance.tx_buffers = dma.address + LanceTxBufferOffset;

		init->mode = 0;
		init->ring_lengths = uint16((LanceRxRingLog2 << 4) | (LanceTxRingLog2 << 12));
		for0(i, numsof(init->physical_address)) init->physical_address[i] = lance.mac[i];
		init->rx_ring = uint32(dma.physical + LanceRxDescriptorOffset);
		init->tx_ring = uint32(dma.physical + LanceTxDescriptorOffset);

		for0(i, LanceRxDescriptorCount) {
			lance.rx_descriptors[i].buffer_address = uint32(
				dma.physical + LanceRxBufferOffset + i * LanceFrameBufferSize);
			lance.rx_descriptors[i].status_length = uint32(LanceRxBufferLength);
		}
		for0(i, LanceTxDescriptorCount) {
			lance.tx_descriptors[i].buffer_address = uint32(
				dma.physical + LanceTxBufferOffset + i * LanceFrameBufferSize);
			lance.tx_descriptors[i].status_length = LanceDescriptorOnes;
		}
		DmaWriteBarrier();
		for0(i, LanceRxDescriptorCount) {
			lance.rx_descriptors[i].status_length =
				LanceDescriptorOwn | uint32(LanceRxBufferLength);
		}
		DmaWriteBarrier();

		lance.rx_index = 0;
		lance.tx_index = 0;
		lance.tx_queue_head = 0;
		lance.tx_queue_tail = 0;
		lance.tx_queue_count = 0;
		lance.rx_errors = 0;
		lance.tx_errors = 0;
		lance.missed_frames = 0;
		lance.rings_ready = false;
		lance.faulted = false;
	}

	bool WaitForCsr0(stduint dev_handle, const PwcallDeviceResourceInfo& io,
		uint16 required, uint16 forbidden, uint16& csr0) {
		for0(i, LanceInitPollLimit) {
			if (!ReadCsr(dev_handle, io, LanceCsr0Index, csr0)) return false;
			if (csr0 & forbidden) return false;
			if ((csr0 & required) == required) return true;
		}
		return false;
	}

	bool InitializeAndStart(stduint dev_handle, const PwcallDeviceResourceInfo& io,
		const LanceDmaRegion& dma, uint16& bcr20, uint16& init_csr0, uint16& running_csr0) {
		if (!WriteBcr(dev_handle, io, LanceBcr20Index, LanceSoftwareStyle32) ||
			!ReadBcr(dev_handle, io, LanceBcr20Index, bcr20) ||
			(bcr20 & 0x00FFu) != LanceSoftwareStyle32 || !(bcr20 & 0x0100u)) return false;

		const uint32 init_address = uint32(dma.physical + LanceInitOffset);
		if (!WriteCsr(dev_handle, io, LanceCsr1Index, uint16(init_address)) ||
			!WriteCsr(dev_handle, io, LanceCsr2Index, uint16(init_address >> 16)) ||
			!WriteCsr(dev_handle, io, LanceCsr0Index, LanceCsr0Init) ||
			!WaitForCsr0(dev_handle, io, LanceCsr0Idon, LanceCsr0Error, init_csr0)) return false;

		if (!WriteCsr(dev_handle, io, LanceCsr0Index, LanceCsr0Idon | LanceCsr0Start)) return false;
		return WaitForCsr0(dev_handle, io, LanceCsr0RxOn | LanceCsr0TxOn,
			LanceCsr0Error, running_csr0);
	}

	bool StopController(stduint dev_handle, const PwcallDeviceResourceInfo& io, uint16& csr0) {
		if (!WriteCsr(dev_handle, io, LanceCsr0Index, LanceCsr0Stop) ||
			!ReadCsr(dev_handle, io, LanceCsr0Index, csr0)) return false;
		return (csr0 & LanceCsr0Stop) && !(csr0 & (LanceCsr0RxOn | LanceCsr0TxOn));
	}

	bool ReadMac(stduint dev_handle, const PwcallDeviceResourceInfo& io, uint8 (&mac)[6]) {
		for0(i, numsof(mac)) {
			uint32 value = 0;
			const auto offset = LanceIoOffset(_IMM(LanceIoOffset::Aprom) + i);
			if (!ReadIo(dev_handle, io, offset, sizeof(uint8), value)) return false;
			mac[i] = byte(value);
		}
		return true;
	}

	bool IsValidMac(const uint8 (&mac)[6]) {
		bool any_nonzero = false;
		bool any_not_ff = false;
		for0(i, numsof(mac)) {
			if (mac[i]) any_nonzero = true;
			if (mac[i] != 0xFFu) any_not_ff = true;
		}
		return any_nonzero && any_not_ff && (mac[0] & 0x01u) == 0;
	}

	bool ResetAndIdentify(stduint dev_handle, const PwcallDeviceResourceInfo& io,
		uint16& csr0, uint32& chip_id) {
		uint32 reset_value = 0;
		if (!ReadIo(dev_handle, io, LanceIoOffset::Reset, sizeof(uint16), reset_value)) return false;
		if (!WriteCsr(dev_handle, io, LanceCsr0Index, LanceCsr0Stop)) return false;
		if (!ReadCsr(dev_handle, io, LanceCsr0Index, csr0) || !(csr0 & LanceCsr0Stop)) return false;

		uint16 csr88 = 0;
		uint16 csr89 = 0;
		if (!ReadCsr(dev_handle, io, LanceCsr88Index, csr88) ||
			!ReadCsr(dev_handle, io, LanceCsr89Index, csr89)) return false;
		chip_id = uint32(csr88) | (uint32(csr89) << 16);
		return (chip_id & 0x0FFFu) == LanceChipSignature &&
			((chip_id >> 12) & 0xFFFFu) == LancePartId;
	}

	uint32 DescriptorByteCount(stduint length) {
		return uint32(uint16(0u - uint32(length)));
	}

	bool UpdateLinkState() {
		if (!ReadBcr(g_lance.dev_handle, g_lance.io, LanceBcr2Index, g_lance.bcr2) ||
			!ReadBcr(g_lance.dev_handle, g_lance.io, LanceBcr4Index, g_lance.bcr4) ||
			!ReadCsr(g_lance.dev_handle, g_lance.io, LanceCsr15Index, g_lance.csr15)) return false;

		const uint16 port = g_lance.csr15 & LanceModePortSelectMask;
		if (g_lance.bcr2 & LanceBcr2AutoSelect) g_lance.link_up = true;
		else if (port == LanceModePortAui) g_lance.link_up = true;
		else if (port == LanceModePort10BaseT) {
			g_lance.link_up = (g_lance.bcr4 & LanceBcr4LedOut) != 0;
		}
		else g_lance.link_up = false;
		return true;
	}

	void ReleaseRxDescriptor(uint32 index) {
		auto& descriptor = g_lance.rx_descriptors[index];
		descriptor.message_status = 0;
		descriptor.reserved = 0;
		descriptor.status_length = uint32(LanceRxBufferLength);
		DmaWriteBarrier();
		descriptor.status_length = LanceDescriptorOwn | uint32(LanceRxBufferLength);
		DmaWriteBarrier();
	}

	stdsint PeekFrame(void* data, stduint count, uint32& index) {
		if (!g_lance.rings_ready || !data || !count) return -1;
		for0(i, LanceRxDescriptorCount) {
			auto& descriptor = g_lance.rx_descriptors[g_lance.rx_index];
			if (descriptor.status_length & LanceDescriptorOwn) return 0;
			DmaReadBarrier();

			index = g_lance.rx_index;
			const uint32 status = descriptor.status_length;
			const uint32 message_length = descriptor.message_status & LanceRxMessageLengthMask;
			const bool complete =
				(status & (LanceDescriptorStart | LanceDescriptorEnd)) ==
				(LanceDescriptorStart | LanceDescriptorEnd);
			if ((status & LanceDescriptorError) || !complete ||
				message_length < LanceEthernetFcsSize || message_length > LanceFrameBufferSize ||
				message_length - LanceEthernetFcsSize > LanceEthernetMaxFrame) {
				g_lance.rx_errors++;
				g_lance.rx_index = (g_lance.rx_index + 1) % LanceRxDescriptorCount;
				ReleaseRxDescriptor(index);
				continue;
			}

			const stduint frame_length = message_length - LanceEthernetFcsSize;
			const stduint copy_length = minof(count, frame_length);
			MemCopyN(data, g_lance.rx_buffers + index * LanceFrameBufferSize, copy_length);
			return stdsint(copy_length);
		}
		return 0;
	}

	void ConsumeFrame(uint32 index) {
		if (index != g_lance.rx_index) return;
		g_lance.rx_index = (g_lance.rx_index + 1) % LanceRxDescriptorCount;
		ReleaseRxDescriptor(index);
	}

	stdsint ReadFrame(void* data, stduint count) {
		uint32 index = 0;
		const stdsint length = PeekFrame(data, count, index);
		if (length > 0) ConsumeFrame(index);
		return length;
	}

	bool SubmitFrame(const void* data, stduint count) {
		auto& descriptor = g_lance.tx_descriptors[g_lance.tx_index];
		const uint32 old_status = descriptor.status_length;
		if (old_status & LanceDescriptorOwn) return false;
		DmaReadBarrier();
		if (old_status & LanceDescriptorError) g_lance.tx_errors++;

		const stduint wire_length = maxof(count, LanceEthernetMinFrame);
		uint8* buffer = g_lance.tx_buffers + g_lance.tx_index * LanceFrameBufferSize;
		MemSet(buffer, 0, wire_length);
		MemCopyN(buffer, data, count);

		const uint32 status = LanceTxDescriptorAddFcs | LanceDescriptorStart |
			LanceDescriptorEnd | DescriptorByteCount(wire_length);
		descriptor.message_status = 0;
		descriptor.reserved = 0;
		descriptor.status_length = status;
		DmaWriteBarrier();
		descriptor.status_length = status | LanceDescriptorOwn;
		DmaWriteBarrier();

		g_lance.tx_index = (g_lance.tx_index + 1) % LanceTxDescriptorCount;
		if (!WriteCsr(g_lance.dev_handle, g_lance.io, LanceCsr0Index,
			LanceCsr0Iena | LanceCsr0Tdmd)) g_lance.faulted = true;
		return true;
	}

	bool EnqueueFrame(const void* data, stduint count) {
		if (g_lance.tx_queue_count >= LanceTxQueueCount) return false;
		auto& queued = g_lance.tx_queue[g_lance.tx_queue_tail];
		queued.length = uint16(count);
		MemCopyN(queued.data, data, count);
		g_lance.tx_queue_tail = (g_lance.tx_queue_tail + 1) % LanceTxQueueCount;
		g_lance.tx_queue_count++;
		return true;
	}

	stduint FlushTxQueue() {
		stduint flushed = 0;
		while (g_lance.tx_queue_count && !g_lance.faulted) {
			auto& queued = g_lance.tx_queue[g_lance.tx_queue_head];
			if (!SubmitFrame(queued.data, queued.length)) break;
			queued.length = 0;
			g_lance.tx_queue_head = (g_lance.tx_queue_head + 1) % LanceTxQueueCount;
			g_lance.tx_queue_count--;
			flushed++;
		}
		return flushed;
	}

	stdsint SendFrame(const void* data, stduint count) {
		if (!g_lance.rings_ready || g_lance.faulted || !data || !count ||
			count > LanceEthernetMaxFrame) return -1;
		(void)FlushTxQueue();
		if (!g_lance.tx_queue_count && SubmitFrame(data, count)) return stdsint(count);
		return EnqueueFrame(data, count) ? stdsint(count) : 0;
	}

	bool EnableInterrupts() {
		uint16 csr0 = 0;
		if (!ReadCsr(g_lance.dev_handle, g_lance.io, LanceCsr0Index, csr0) ||
			(csr0 & LanceCsr0FatalMask)) return false;
		return WriteCsr(g_lance.dev_handle, g_lance.io, LanceCsr0Index,
			(csr0 & LanceCsr0AcknowledgeMask) | LanceCsr0Iena);
	}

	bool ServiceInterrupt() {
		uint16 csr0 = 0;
		if (!ReadCsr(g_lance.dev_handle, g_lance.io, LanceCsr0Index, csr0)) return false;
		if (!WriteCsr(g_lance.dev_handle, g_lance.io, LanceCsr0Index,
			(csr0 & LanceCsr0AcknowledgeMask) | LanceCsr0Iena)) return false;

		if (csr0 & LanceCsr0Miss) g_lance.missed_frames++;
		if (csr0 & LanceCsr0Tint) (void)FlushTxQueue();
		(void)UpdateLinkState();
		if (csr0 & LanceCsr0FatalMask) g_lance.faulted = true;
		return !g_lance.faulted;
	}

	bool PublishAttach() {
		FMT_NetworkMsg_DRV_ATTACH attach = {};
		attach.version = NetworkDriverProtocolVersion;
		attach.caps = NetworkDriverCap_Poll | NetworkDriverCap_RxEvent;
		attach.dev_handle = uint32(g_lance.dev_handle);
		attach.mtu = 1500;
		for0(i, numsof(g_lance.mac)) attach.mac[i] = g_lance.mac[i];
		attach.link_state = g_lance.link_up ? 1 : 0;
		const char* name = "lance";
		for0(i, NetworkDriverNameCapacity - 1) {
			attach.name[i] = name[i];
			if (!name[i]) break;
		}

		CommMsg send_msg = {};
		send_msg.data.address = _IMM(&attach);
		send_msg.data.length = sizeof(attach);
		send_msg.type = _IMM(NetworkMsg::DRV_ATTACH);
		if (Powercall::SysComm(COMM_SEND, Task_Net_Serv, &send_msg)) return false;

		stdsint result = -1;
		CommMsg recv_msg = {};
		recv_msg.data.address = _IMM(&result);
		recv_msg.data.length = sizeof(result);
		if (Powercall::SysComm(COMM_RECV, Task_Net_Serv, &recv_msg)) return false;
		return result == 0;
	}

	bool InitializeLance() {
		const stduint cls = (stduint(LanceVendorId) << 16) | LanceDeviceId;
		const stdsint opened = Powercall::DevOpen(0, cls, _IMM(PwcallDeviceOpenFlag::Interrupt));
		if (opened <= 0) return false;
		g_lance.dev_handle = stduint(opened);

		PwcallDeviceIdentity identity = {};
		if (Powercall::DevGetIdentity(g_lance.dev_handle, &identity) != 0 ||
			identity.vendor_id != LanceVendorId || identity.device_id != LanceDeviceId) return false;

		PwcallDeviceResourceInfo irq = {};
		if (!FindResource(g_lance.dev_handle, PwcallDeviceResourceType::PciBarIo, g_lance.io) ||
			g_lance.io.length < LanceRequiredIoLength ||
			g_lance.io.start + LanceRequiredIoLength > 0x10000u ||
			!FindResource(g_lance.dev_handle, PwcallDeviceResourceType::IrqLine, irq)) return false;
		g_lance.io_ready = true;

		uint16 csr0 = 0;
		uint32 chip_id = 0;
		if (!ReadMac(g_lance.dev_handle, g_lance.io, g_lance.mac) ||
			!IsValidMac(g_lance.mac) ||
			!ResetAndIdentify(g_lance.dev_handle, g_lance.io, csr0, chip_id) ||
			!AllocDmaRegion(g_lance.dev_handle, g_lance.dma)) return false;
		BuildDmaStructures(g_lance);

		uint16 bcr20 = 0;
		uint16 init_csr0 = 0;
		uint16 running_csr0 = 0;
		if (!InitializeAndStart(g_lance.dev_handle, g_lance.io, g_lance.dma,
			bcr20, init_csr0, running_csr0)) return false;
		g_lance.rings_ready = true;
		if (!UpdateLinkState()) return false;

		printf("lance: pci=%02x:%02x.%u io-bar=%u base=%04x len=%u irq=%u "
			"mac=%02x:%02x:%02x:%02x:%02x:%02x chip=%08x\n\r",
			unsigned(identity.pci_bus), unsigned(identity.pci_device),
			unsigned(identity.pci_function), unsigned(g_lance.io.index),
			unsigned(g_lance.io.start), unsigned(g_lance.io.length), unsigned(irq.start),
			unsigned(g_lance.mac[0]), unsigned(g_lance.mac[1]), unsigned(g_lance.mac[2]),
			unsigned(g_lance.mac[3]), unsigned(g_lance.mac[4]), unsigned(g_lance.mac[5]),
			unsigned(chip_id));
		printf("lance: dma=%08x size=%u mapped=%u rx=%u tx=%u buffer=%u "
			"bcr20=%04x bcr2=%04x bcr4=%04x csr15=%04x init=%04x run=%04x link=%u\n\r",
			unsigned(uint32(g_lance.dma.physical)), unsigned(LanceDmaSize),
			unsigned(g_lance.dma.length),
			unsigned(LanceRxDescriptorCount), unsigned(LanceTxDescriptorCount),
			unsigned(LanceFrameBufferSize), unsigned(bcr20), unsigned(g_lance.bcr2),
			unsigned(g_lance.bcr4), unsigned(g_lance.csr15), unsigned(init_csr0),
			unsigned(running_csr0), unsigned(g_lance.link_up ? 1 : 0));
		return true;
	}

	void ShutdownLance() {
		bool stopped = true;
		if (g_lance.dev_handle && g_lance.io_ready) {
			uint16 csr0 = 0;
			stopped = StopController(g_lance.dev_handle, g_lance.io, csr0);
		}
		g_lance.rings_ready = false;
		if (!stopped) return;
		FreeDmaRegion(g_lance.dma);
		if (g_lance.dev_handle) Powercall::DevClose(g_lance.dev_handle);
		g_lance.dev_handle = 0;
	}

	bool ProcessReceivedEvent(CommMsg& recv_msg, FMT_NetworkMsg_DRV_FRAME& frame) {
		if (recv_msg.type == _IMM(KernelMsg::DeviceEvent)) {
			if (recv_msg.data.length < sizeof(DeviceEvent)) return false;
			const auto& event = *reinterpret_cast<const DeviceEvent*>(&frame);
			if (event.version != DeviceEventProtocolVersion ||
				DeviceEventKind(event.kind) != DeviceEventKind::Interrupt ||
				event.device_handle != g_lance.dev_handle) return false;
			const bool serviced = ServiceInterrupt();
			const bool acknowledged = Powercall::DevAck(g_lance.dev_handle,
				stduint(event.sequence), event.generation) == 0;
			return serviced && acknowledged;
		}

		switch (NetworkMsg(recv_msg.type)) {
			case NetworkMsg::DRV_SEND:
				frame.status = SendFrame(frame.data, frame.length);
				break;
			case NetworkMsg::DRV_RECV:
				frame.status = ReadFrame(frame.data,
					minof(stduint(frame.capacity), stduint(NetworkDriverFrameCapacity)));
				frame.length = frame.status > 0 ? uint32(frame.status) : 0;
				break;
			default:
				frame.status = -1;
				break;
		}

		CommMsg reply_msg = {};
		reply_msg.data.address = _IMM(&frame);
		reply_msg.data.length = sizeof(frame);
		reply_msg.type = recv_msg.type;
		Powercall::SysComm(COMM_SEND_ASYNC, recv_msg.src, &reply_msg);
		return true;
	}

	bool ReceiveEvent() {
		FMT_NetworkMsg_DRV_FRAME frame = {};
		CommMsg recv_msg = {};
		recv_msg.data.address = _IMM(&frame);
		recv_msg.data.length = sizeof(frame);
		if (Powercall::SysComm(COMM_RECV, ANYPROC, &recv_msg)) return false;
		return ProcessReceivedEvent(recv_msg, frame);
	}

	bool ProcessControlMessages() {
		bool handled = false;
		while (syscall(syscall_t::TMSG)) {
			if (!ReceiveEvent()) break;
			handled = true;
		}
		return handled;
	}

	stdsint PushRxFrames() {
		stdsint pushed = 0;
		while (true) {
			FMT_NetworkMsg_DRV_FRAME frame = {};
			frame.capacity = NetworkDriverFrameCapacity;
			uint32 rx_index = 0;
			frame.status = PeekFrame(frame.data, NetworkDriverFrameCapacity, rx_index);
			if (frame.status <= 0) return pushed;
			frame.length = uint32(frame.status);

			CommMsg send_msg = {};
			send_msg.data.address = _IMM(&frame);
			send_msg.data.length = sizeof(frame);
			send_msg.type = _IMM(NetworkMsg::DRV_RX);
			if (Powercall::SysComm(COMM_SEND_ASYNC, Task_Net_Serv, &send_msg)) return -1;
			ConsumeFrame(rx_index);
			pushed++;
		}
	}
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;

	if (Powercall::Hello() != 0) return -1;
	if (!InitializeLance()) {
		printf("lance: initialization failed\n\r");
		ShutdownLance();
		return -1;
	}
	if (!PublishAttach()) {
		printf("lance: network attach failed\n\r");
		ShutdownLance();
		return -1;
	}
	if (Powercall::DevPublish(g_lance.dev_handle, PwcallDevicePublishCommand::Started) != 0) {
		printf("lance: device publish failed\n\r");
		ShutdownLance();
		return -1;
	}
	if (!EnableInterrupts()) {
		printf("lance: interrupt enable failed\n\r");
		ShutdownLance();
		return -1;
	}
	(void)PushRxFrames();

	while (!g_lance.faulted) {
		bool active = ProcessControlMessages();
		if (g_lance.faulted) break;
		const stdsint pushed = PushRxFrames();
		if (g_lance.faulted) break;
		if (pushed < 0) {
			syscall(syscall_t::REST, 1, 1);
			continue;
		}
		if (pushed > 0) active = true;
		if (active) continue;
		if (!ReceiveEvent()) break;
	}

	printf("lance: stopped rx-errors=%u tx-errors=%u missed=%u\n\r",
		unsigned(g_lance.rx_errors), unsigned(g_lance.tx_errors),
		unsigned(g_lance.missed_frames));
	ShutdownLance();
	return -1;
}

#else

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	return -1;
}

#endif
