// UTF-8 g++ TAB4 LF
// ModuTitle: ISA 8237 DMA Controller
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include "dma-isa.hpp"

#if _MCCA == 0x8632
namespace {
	constexpr uint16 Dma8AddressPorts[] = { 0x00, 0x02, 0x04, 0x06 };
	constexpr uint16 Dma8CountPorts[] = { 0x01, 0x03, 0x05, 0x07 };
	constexpr uint16 Dma8PagePorts[] = { 0x87, 0x83, 0x81, 0x82 };
	constexpr uint16 Dma8MaskPort = 0x0A;
	constexpr uint16 Dma8ModePort = 0x0B;
	constexpr uint16 Dma8ClearFlipFlopPort = 0x0C;
	constexpr uint8 DmaModeSingle = 0x40;
	constexpr uint8 DmaModeDeviceToMemory = 0x04;
	constexpr uint8 DmaModeMemoryToDevice = 0x08;
	constexpr stduint IsaDma8AddressLimit = 0x01000000;
	constexpr uint32 IsaDma8Boundary = 0x00010000;
	constexpr uint32 IsaDma8MaximumLength = 0x00010000;

	Spinlock isa_dma_lock;

	bool IsValidChannel(uint8 channel) {
		return channel < numsof(Dma8AddressPorts);
	}

	void MaskChannel(uint8 channel) {
		outpb(Dma8MaskPort, uint8(0x04 | channel));
	}

	void UnmaskChannel(uint8 channel) {
		outpb(Dma8MaskPort, channel);
	}

	bool IsValidTransfer(stduint physical_address, uint32 length) {
		if (!length || length > IsaDma8MaximumLength) return false;
		if (physical_address >= IsaDma8AddressLimit) return false;
		if (stduint(length) > IsaDma8AddressLimit - physical_address) return false;
		const uint32 boundary_offset = uint32(physical_address & (IsaDma8Boundary - 1));
		return length <= IsaDma8Boundary - boundary_offset;
	}
}

bool IsaDma8Prepare(uint8 channel, stduint physical_address, uint32 length,
	IsaDmaDirection direction) {
	if (!IsValidChannel(channel) || !IsValidTransfer(physical_address, length)) {
		return false;
	}

	SpinlockLocal guard(&isa_dma_lock);
	MaskChannel(channel);
	outpb(Dma8ClearFlipFlopPort, 0);
	const uint8 transfer_mode = direction == IsaDmaDirection::DeviceToMemory ?
		DmaModeDeviceToMemory : DmaModeMemoryToDevice;
	outpb(Dma8ModePort, uint8(DmaModeSingle | transfer_mode | channel));
	outpb(Dma8AddressPorts[channel], uint8(physical_address));
	outpb(Dma8AddressPorts[channel], uint8(physical_address >> 8));
	outpb(Dma8PagePorts[channel], uint8(physical_address >> 16));
	outpb(Dma8ClearFlipFlopPort, 0);
	const uint16 count = uint16(length - 1);
	outpb(Dma8CountPorts[channel], uint8(count));
	outpb(Dma8CountPorts[channel], uint8(count >> 8));
	UnmaskChannel(channel);
	return true;
}

void IsaDmaMask(uint8 channel) {
	if (!IsValidChannel(channel)) return;
	SpinlockLocal guard(&isa_dma_lock);
	MaskChannel(channel);
}

void IsaDmaUnmask(uint8 channel) {
	if (!IsValidChannel(channel)) return;
	SpinlockLocal guard(&isa_dma_lock);
	UnmaskChannel(channel);
}
#endif
