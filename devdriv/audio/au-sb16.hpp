#pragma once

#include <c/stdinc.h>

struct SoundBlasterPlatformConfig {
	uint16 io_base;
	uint16 io_length;
	uint8 irq;
	uint8 dma8;
	uint8 dma16;
};

constexpr SoundBlasterPlatformConfig SoundBlasterDefaultConfig{
	.io_base = 0x220,
	.io_length = 0x10,
	.irq = 5,
	.dma8 = 1,
	.dma16 = 5,
};
