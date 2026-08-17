// UTF-8 g++ TAB4 LF
// ModuTitle: ISA 8237 DMA Controller
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include "dma-isa.hpp"

#if _MCCA == 0x8632
namespace {
	Spinlock isa_dma_lock;

	void IsaDmaWrite8(void*, uint16 port, uint8 value) {
		outpb(port, value);
	}

	const uni::DMA8237_IO isa_dma_io{
		.context = nullptr,
		.write8 = IsaDmaWrite8,
	};

	const uni::DMA8237_t isa_dma{isa_dma_io};
}

bool IsaDma8Prepare(uint8 channel, stduint physical_address, uint32 length,
	IsaDmaDirection direction, IsaDmaReloadMode reload_mode) {
	SpinlockLocal guard(&isa_dma_lock);
	return isa_dma.Transfer(
		channel, physical_address, length, direction, reload_mode);
}

void IsaDmaMask(uint8 channel) {
	SpinlockLocal guard(&isa_dma_lock);
	isa_dma.Abort(channel);
}

void IsaDmaUnmask(uint8 channel) {
	SpinlockLocal guard(&isa_dma_lock);
	isa_dma.enAble(true, channel);
}
#endif
