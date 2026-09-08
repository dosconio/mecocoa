#pragma once

#include <c/stdinc.h>
#include <cpp/System/Audiosys.hpp>

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

// Call from ordinary kernel context.
bool SoundBlasterPlayPcmImmediate(const uint8* data, uint32 byte_count,
	uint16 sample_rate = 11025,
	uni::AudioSampleFormat sample_format = uni::AudioSampleFormat::U8,
	uint8 channels = 1);

bool SoundBlasterPlayPcmU8MonoImmediate(const uint8* data, uint32 byte_count,
	uint16 sample_rate = 11025);

using SoundBlasterPcmRefill = uint32 (*)(void* context, uint8* data,
	uint32 byte_count);

// Start/stop continuous PCM playback backed by SB16 auto-init DMA.
// Refills are performed only from ordinary kernel context by
// SoundBlasterServicePlayback(); the IRQ handler only publishes completed halves.
bool SoundBlasterStartPcmStream(uint16 sample_rate,
	uni::AudioSampleFormat sample_format, uint8 channels,
	SoundBlasterPcmRefill refill, void* context);

bool SoundBlasterStartPcmU8MonoStream(uint16 sample_rate,
	SoundBlasterPcmRefill refill, void* context);
bool SoundBlasterStopPcmStream();
void SoundBlasterAbortPcmStream();
bool SoundBlasterPausePcmStream();
bool SoundBlasterResumePcmStream();
bool SoundBlasterFlushPcmStream();
uint64 SoundBlasterGetPlayedBytes();
bool SoundBlasterSetVolume(uni::SoundBlasterMixerChannel channel, uint8 left, uint8 right);
bool SoundBlasterGetVolume(uni::SoundBlasterMixerChannel channel, uint8& left, uint8& right);
bool SoundBlasterSetMute(uni::SoundBlasterMixerChannel channel, bool mute);
bool SoundBlasterResetMixer();

// Call from ordinary kernel context; returns the number of refilled DMA blocks.
uint8 SoundBlasterServicePlayback();
