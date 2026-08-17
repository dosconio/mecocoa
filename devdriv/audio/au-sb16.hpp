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

// Call from ordinary kernel context after IRQ dispatch is running.
bool SoundBlasterRunAutoInitSmokeTest();

// Call from ordinary kernel context. Current format is U8 mono PCM only.
bool SoundBlasterPlayPcmU8MonoImmediate(const uint8* data, uint32 byte_count,
	uint16 sample_rate = 11025);

// Call from ordinary kernel context; returns the number of refilled DMA blocks.
uint8 SoundBlasterServicePlayback();
