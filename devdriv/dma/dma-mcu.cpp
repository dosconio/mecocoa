// UTF-8 CPP-ISO11 TAB4 LF
// ModuTitle: STM32 MCU DMA
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"

#if defined(_MCU_STM32H7x) && ((_MCCA & 0xFFFF) == 0x2032)
#include <cpp/Device/DMA>
#include "unicfg.hpp"

_ESYM_C void R_DMA_INIT();

__attribute__((section(".init.rmod")))
extern const RMOD_LIST RMOD_LIST_DMA{
	R_DMA_INIT,
	"DMA-MCU",
	{}
};

namespace {
	constexpr stduint dma_register_size =
		(_IMM(uni::DMAReg::S7FCR) + 1) * sizeof(uint32);

	void RegisterDmaController(uint8 id, uni::DMA_t& dma, bool enable_clock) {
		const char* name = id == 1 ? "dma1" : "dma2";
		const bool clock_ready = enable_clock
			? dma.enClock() && dma.isAble() : dma.isAble();
		auto* node = Devsman::RegisterPlatformDevice(name, "stm32-dma", &dma);
		if (!node) {
			plogerro("[DMA] cannot register %s", name);
			return;
		}
		node->fields.rcc_state = clock_ready
			? DeviceRccState::Enabled : DeviceRccState::Disabled;
		if (!Devsman::AddMemoryResource(node, 0, dma.getAddress(), dma_register_size)) {
			plogerro("[DMA] cannot register %s MMIO", name);
			Devsman::UnregisterDevice(node);
			return;
		}
		if (enable_clock && !clock_ready)
			plogerro("[DMA] %s clock initialization failed", name);

		for (uint8 index = 0; index < DeviceDmaStreamCount; ++index) {
			char stream_name[] = "stream@0";
			stream_name[7] = char('0' + index);
			const auto& stream = dma[index];
			auto* stream_node = Devsman::RegisterPlatformDevice(
				node, stream_name, "stm32-dma-stream",
				const_cast<uni::DMAStream*>(&stream));
			if (!stream_node) {
				plogerro("[DMA] cannot register %s %s", name, stream_name);
				continue;
			}
			if (!Devsman::AddIrqResource(stream_node, stream.getRequest())) {
				plogerro("[DMA] cannot register %s %s IRQ", name, stream_name);
				Devsman::UnregisterDevice(stream_node);
				continue;
			}
			if (!Devsman::AddDmaStreamResource(stream_node, dma.getAddress(), index)) {
				plogerro("[DMA] cannot register %s %s identity", name, stream_name);
				Devsman::UnregisterDevice(stream_node);
				continue;
			}
			stream_node->fields.rcc_state = node->fields.rcc_state;
		}
	}
}

void R_DMA_INIT() {
	RegisterDmaController(1, uni::DMA1, BoardConfig::Dma::Dma1Enabled);
	RegisterDmaController(2, uni::DMA2, BoardConfig::Dma::Dma2Enabled);
}
#endif
