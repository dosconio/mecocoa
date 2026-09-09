// UTF-8 g++ TAB4 LF
// ModuTitle: Sound Blaster 16
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include "au-sb16.hpp"
#include "../dma/dma-isa.hpp"
#include <c/driver/i8259A.h>
#include <cpp/Device/Audio/SoundBlaster.hpp>
#include <cpp/atomic>

#if _MCCA == 0x8632
extern const AudioBackendDriver g_sb16_audio_backend;

namespace {
	constexpr uint16 SoundBlasterTestSampleRate = 11025;
	constexpr uint16 SoundBlasterTestFrequency = 440;
	constexpr uint32 SoundBlasterTestDurationMs = 250;
	constexpr uint32 SoundBlasterTestSampleCount =
		(uint32(SoundBlasterTestSampleRate) * SoundBlasterTestDurationMs) / 1000;
	constexpr stduint SoundBlasterDmaBufferSize = 16384;
	constexpr uint8 SoundBlasterAutoInitBlockCount = 4;
	constexpr uint8 SoundBlasterInvalidBlock = 0xFF;
	constexpr uint32 SoundBlasterAutoInitBlockBytes = 4096;
	constexpr uint32 SoundBlasterAutoInitTestBlocks = 8;
	constexpr stduint SoundBlasterAutoInitTestTimeoutTicks =
		3 * CONFIG_SysTickFreq;
	constexpr stduint SoundBlasterAutoInitStopTimeoutTicks =
		CONFIG_SysTickFreq;
	constexpr uint32 SoundBlasterAutoInitStopPollLimit = 0x100000;
	constexpr stduint SoundBlasterSingleCycleStopTimeoutExtraTicks =
		CONFIG_SysTickFreq;
	constexpr uint16 SoundBlasterCompatProbeSampleRate = 11025;
	constexpr uint32 SoundBlasterCompatProbeBytes = SoundBlasterDmaBufferSize;
	constexpr stduint SoundBlasterCompatProbeMinPercent = 75;
	constexpr stduint SoundBlasterCompatProbeMaxPercent = 150;
	constexpr uint16 SoundBlasterMinimumOutputRate = 5000;
	constexpr uint16 SoundBlasterMaximumOutputRate = 45000;
	static_assert(SoundBlasterAutoInitBlockCount * SoundBlasterAutoInitBlockBytes <=
		SoundBlasterDmaBufferSize);

	// SB16 PCM notes:
	// - QEMU handles the modern 0x41/0xC0 8-bit single-cycle path normally.
	// - VMware reports a usable DSP, but its timing differs: the legacy
	//   0x40/0x14 path can finish a block immediately, while the modern path
	//   may run too fast. Probe both paths with a silent block and keep the one
	//   closest to the expected tick count; apply rate compensation when needed.
	// - A block is considered complete when the IRQ handler publishes Idle.
	//   DSP state bookkeeping may lag in virtual hardware, so normalize it
	//   after completion instead of treating that as a playback failure.
	// - Streaming uses a service-side ring buffer plus SB16 auto-init DMA.
	//   The IRQ handler only publishes completed DMA halves; it must not send
	//   messages or touch scheduler state. Ordinary audio-service context calls
	//   SoundBlasterServicePlayback() to refill completed halves.

	enum class SoundBlasterPlaybackMode : uint8 {
		Idle,
		SingleCycle8,
		SingleCycle16,
		AutoInit8,
		AutoInit16,
		AutoInit8Paused,
		AutoInit16Paused,
		AutoInitStopping,
	};

	enum class SoundBlasterPcmPath : uint8 {
		Unknown,
		Modern,
		Legacy,
	};

	struct SoundBlasterAutoInitBufferState {
		uint8* storage;
		uint32 block_bytes;
		uint32 phase;
		uint16 sample_rate;
		uni::AudioSampleFormat sample_format;
		uint8 channels;
		bool is_16bit;
		bool is_signed;
		bool stereo;
		SoundBlasterPcmRefill refill;
		void* refill_context;
		uni::Atomic<uint32> underrun_count;
		// Generation rejects refill work left over from an earlier playback.
		uni::Atomic<uint32> generation;
		uni::Atomic<uint8> completed_block;
		uni::Atomic<uint8> refill_pending_mask;
		uni::Atomic<uint32> completed_count;
		uni::Atomic<uint32> refilled_count;
		uni::Atomic<uint32> refill_miss_count;
	};

	struct SoundBlasterPcmPathProbe {
		bool completed;
		bool compatible;
		stduint elapsed_ticks;
		stduint expected_ticks;
	};

	volatile uint32 sound_blaster_irq_count;
	uni::Atomic<SoundBlasterPlaybackMode> sound_blaster_playback_mode{
		SoundBlasterPlaybackMode::Idle};
	SoundBlasterPcmPath sound_blaster_pcm_path = SoundBlasterPcmPath::Unknown;
	stduint sound_blaster_rate_scale_num = 1;
	stduint sound_blaster_rate_scale_den = 1;
	volatile bool sound_blaster_unexpected_irq_reported;
	volatile stduint sound_blaster_last_activity_tick = 0;
	uint8 sound_blaster_dma8_channel = SoundBlasterDefaultConfig.dma8;
	uint8 sound_blaster_dma16_channel = SoundBlasterDefaultConfig.dma16;
	uint8* sound_blaster_dma_buffer;
	SoundBlasterAutoInitBufferState sound_blaster_auto_buffer{
		.storage = nullptr,
		.block_bytes = SoundBlasterAutoInitBlockBytes,
		.phase = 0,
		.sample_rate = SoundBlasterTestSampleRate,
		.sample_format = uni::AudioSampleFormat::U8,
		.channels = 1,
		.is_16bit = false,
		.is_signed = false,
		.stereo = false,
		.refill = nullptr,
		.refill_context = nullptr,
		.underrun_count = 0,
		.generation = 0,
		.completed_block = SoundBlasterInvalidBlock,
		.refill_pending_mask = 0,
		.completed_count = 0,
		.refilled_count = 0,
		.refill_miss_count = 0,
	};

	uint8 SoundBlasterRead8(void*, uint16 port) {
		return innpb(port);
	}

	void SoundBlasterWrite8(void*, uint16 port, uint8 value) {
		outpb(port, value);
	}

	void SoundBlasterDelayUs(void*, uint32 microseconds) {
		for (uint32 count = 0; count < microseconds; ++count) {
			outpb(0x80, 0);
		}
	}

	const uni::SoundBlasterIo sound_blaster_io{
		.context = nullptr,
		.read8 = SoundBlasterRead8,
		.write8 = SoundBlasterWrite8,
		.delay_us = SoundBlasterDelayUs,
	};

	uni::SoundBlaster sound_blaster{
		SoundBlasterDefaultConfig.io_base,
		sound_blaster_io,
	};

	void FillSoundBlasterTestTone(uint8* destination, uint32 sample_count,
		uint32& phase) {
		for (uint32 index = 0; index < sample_count; ++index) {
			destination[index] =
				phase < SoundBlasterTestSampleRate / 2 ? 160 : 96;
			phase += SoundBlasterTestFrequency;
			if (phase >= SoundBlasterTestSampleRate) {
				phase -= SoundBlasterTestSampleRate;
			}
		}
	}

	uint8* GetSoundBlasterAutoInitBlock(uint8 block_index) {
		if (!sound_blaster_auto_buffer.storage ||
			block_index >= SoundBlasterAutoInitBlockCount) return nullptr;
		return sound_blaster_auto_buffer.storage +
			block_index * sound_blaster_auto_buffer.block_bytes;
	}

	bool FillSoundBlasterAutoInitBlock(uint8 block_index) {
		auto* block = GetSoundBlasterAutoInitBlock(block_index);
		if (!block) return false;
		if (sound_blaster_auto_buffer.refill) {
			const uint32 filled = sound_blaster_auto_buffer.refill(
				sound_blaster_auto_buffer.refill_context,
				block,
				sound_blaster_auto_buffer.block_bytes);
			if (filled < sound_blaster_auto_buffer.block_bytes) {
				const uint8 silence = sound_blaster_auto_buffer.is_signed ? 0x00 : 0x80;
				MemSet(block + filled, silence,
					sound_blaster_auto_buffer.block_bytes - filled);
				const auto mode = sound_blaster_playback_mode.load(uni::MemoryOrder_Relaxed);
				if (mode != SoundBlasterPlaybackMode::AutoInit8Paused &&
					mode != SoundBlasterPlaybackMode::AutoInit16Paused) {
					const uint32 underruns = sound_blaster_auto_buffer.underrun_count.fetch_add(
						1, uni::MemoryOrder_Relaxed) + 1;
					plogwarn("[SB16] Underrun block=%u filled=%u/%u total_underruns=%u",
						(stduint)block_index, (stduint)filled,
						(stduint)sound_blaster_auto_buffer.block_bytes, (stduint)underruns);
				}
			}
			return true;
		}
		FillSoundBlasterTestTone(block, sound_blaster_auto_buffer.block_bytes,
			sound_blaster_auto_buffer.phase);
		return true;
	}

	bool PrepareSoundBlasterAutoInitBuffers() {
		sound_blaster_auto_buffer.storage = sound_blaster_dma_buffer;
		sound_blaster_auto_buffer.block_bytes = SoundBlasterAutoInitBlockBytes;
		sound_blaster_auto_buffer.phase = 0;
		sound_blaster_auto_buffer.underrun_count.store(
			0, uni::MemoryOrder_Release);
		sound_blaster_auto_buffer.completed_block.store(
			SoundBlasterInvalidBlock, uni::MemoryOrder_Release);
		sound_blaster_auto_buffer.refill_pending_mask.store(
			0, uni::MemoryOrder_Release);
		sound_blaster_auto_buffer.completed_count.store(
			0, uni::MemoryOrder_Release);
		sound_blaster_auto_buffer.refilled_count.store(
			0, uni::MemoryOrder_Release);
		sound_blaster_auto_buffer.refill_miss_count.store(
			0, uni::MemoryOrder_Release);
		sound_blaster_auto_buffer.generation.fetch_add(
			1, uni::MemoryOrder_Acq_Rel);
		for (uint8 block_index = 0;
			block_index < SoundBlasterAutoInitBlockCount; ++block_index) {
			if (!FillSoundBlasterAutoInitBlock(block_index)) return false;
		}
		return true;
	}

	uint8 MarkSoundBlasterAutoInitBlockComplete() {
		const uint8 previous_block = sound_blaster_auto_buffer.completed_block.load(
			uni::MemoryOrder_Relaxed);
		// Each auto-init IRQ completes the next block in the circular DMA buffer.
		const uint8 completed_block = previous_block == SoundBlasterInvalidBlock ?
			0 : uint8((previous_block + 1) % SoundBlasterAutoInitBlockCount);
		sound_blaster_auto_buffer.completed_block.store(
			completed_block, uni::MemoryOrder_Release);

		const uint8 completed_bit = uint8(1u << completed_block);
		uint8 pending_mask = sound_blaster_auto_buffer.refill_pending_mask.load(
			uni::MemoryOrder_Relaxed);
		bool refill_missed = (pending_mask & completed_bit) != 0;
		while (!sound_blaster_auto_buffer.refill_pending_mask.compare_exchange(
			pending_mask, uint8(pending_mask | completed_bit),
			uni::MemoryOrder_Release, uni::MemoryOrder_Relaxed)) {
			if (pending_mask & completed_bit) refill_missed = true;
		}
		sound_blaster_auto_buffer.completed_count.fetch_add(
			1, uni::MemoryOrder_Relaxed);
		if (refill_missed) {
			sound_blaster_auto_buffer.refill_miss_count.fetch_add(
				1, uni::MemoryOrder_Relaxed);
		}
		sound_blaster_last_activity_tick = tick;
		return completed_block;
	}

	uint8 RefillSoundBlasterPendingBlocks(uint32 generation) {
		if (sound_blaster_auto_buffer.generation.load(
			uni::MemoryOrder_Acquire) != generation) return 0;
		const uint8 pending_mask = sound_blaster_auto_buffer.refill_pending_mask.exchange(
			0, uni::MemoryOrder_Acq_Rel);
		if (!pending_mask) return 0;

		const uint8 last_completed = sound_blaster_auto_buffer.completed_block.load(
			uni::MemoryOrder_Acquire);

		uint8 refill_count = 0;
		for (uint8 pass = 1; pass <= SoundBlasterAutoInitBlockCount; ++pass) {
			const uint8 block_index = uint8((last_completed + pass) % SoundBlasterAutoInitBlockCount);
			if (!(pending_mask & uint8(1u << block_index))) continue;
			if (sound_blaster_auto_buffer.generation.load(
				uni::MemoryOrder_Acquire) != generation) return refill_count;
			if (FillSoundBlasterAutoInitBlock(block_index)) ++refill_count;
		}
		return refill_count;
	}

	void GenerateSoundBlasterTestTone() {
		uint32 phase = 0;
		FillSoundBlasterTestTone(sound_blaster_dma_buffer,
			SoundBlasterTestSampleCount, phase);
	}

	bool PrepareSoundBlasterDma(DeviceNode* node) {
		const auto* dma8_res = Devsman::FindResource(
			node, DeviceResourceType::DmaChannel, 0);
		if (dma8_res && dma8_res->start <= 3 && dma8_res->extra == 8) {
			sound_blaster_dma8_channel = uint8(dma8_res->start);
		} else {
			sound_blaster_dma8_channel = SoundBlasterDefaultConfig.dma8;
		}
		const auto* dma16_res = Devsman::FindResource(
			node, DeviceResourceType::DmaChannel, 1);
		if (dma16_res && dma16_res->start >= 4 && dma16_res->start <= 7 && dma16_res->extra == 16) {
			sound_blaster_dma16_channel = uint8(dma16_res->start);
		} else {
			sound_blaster_dma16_channel = SoundBlasterDefaultConfig.dma16;
		}
		if (!sound_blaster_dma_buffer) {
			sound_blaster_dma_buffer = static_cast<uint8*>(
				DmaLowAlloc(SoundBlasterDmaBufferSize));
			if (!sound_blaster_dma_buffer) {
				sound_blaster_dma_buffer = static_cast<uint8*>(
					mempool.allocate(SoundBlasterDmaBufferSize, PAGESIZE_4KB, 16));
			}
		}
		if (!sound_blaster_dma_buffer) {
			plogwarn("[SB16] ISA DMA buffer allocation failed");
			return false;
		}
		return true;
	}

	SoundBlasterPcmPath GetNominalSoundBlasterPcmPath() {
		return sound_blaster.GetDspMajorVersion() >= 4 ?
			SoundBlasterPcmPath::Modern : SoundBlasterPcmPath::Legacy;
	}

	SoundBlasterPcmPath GetEffectiveSoundBlasterPcmPath() {
		return sound_blaster_pcm_path == SoundBlasterPcmPath::Unknown ?
			SoundBlasterPcmPath::Modern : sound_blaster_pcm_path;
	}

	uint16 GetProgrammedSoundBlasterRate(uint16 sample_rate) {
		stduint programmed_rate = sample_rate;
		if (sound_blaster_rate_scale_den) {
			programmed_rate =
				(programmed_rate * sound_blaster_rate_scale_num +
					sound_blaster_rate_scale_den / 2) /
				sound_blaster_rate_scale_den;
		}
		if (programmed_rate < SoundBlasterMinimumOutputRate) {
			programmed_rate = SoundBlasterMinimumOutputRate;
		}
		if (programmed_rate > SoundBlasterMaximumOutputRate) {
			programmed_rate = SoundBlasterMaximumOutputRate;
		}
		return uint16(programmed_rate);
	}

	bool WaitSoundBlasterSingleCycleDone(uint32 byte_count, uint16 sample_rate,
		uint8 frame_size = 1, stduint* elapsed_ticks = nullptr) {
		const stduint sample_frames = frame_size ? (byte_count / frame_size) : byte_count;
		const stduint playback_ticks = ((stduint)sample_frames * CONFIG_SysTickFreq +
			sample_rate - 1) / sample_rate;
		const stduint timeout_ticks =
			playback_ticks + SoundBlasterSingleCycleStopTimeoutExtraTicks;
		const stduint playback_start = tick;
		ploginfo("[SB16] Wait single-cycle start rate=%u bytes=%u timeout=%u",
			(stduint)sample_rate, (stduint)byte_count, (stduint)timeout_ticks);
		IC.enInterrupt();
		while (sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire) !=
			SoundBlasterPlaybackMode::Idle &&
			tick - playback_start < timeout_ticks) {
			asm volatile("pause" ::: "memory");
		}
		if (sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire) !=
			SoundBlasterPlaybackMode::Idle) {
			IsaDmaMask(sound_blaster_dma8_channel);
			IsaDmaMask(sound_blaster_dma16_channel);
			sound_blaster_playback_mode.store(
				SoundBlasterPlaybackMode::Idle, uni::MemoryOrder_Release);
			sound_blaster.Complete8BitPlayback();
			sound_blaster.Complete16BitPlayback();
			plogwarn("[SB16] PCM playback timed out rate=%u bytes=%u",
				(stduint)sample_rate, (stduint)byte_count);
			if (elapsed_ticks) *elapsed_ticks = tick - playback_start;
			return false;
		}
		sound_blaster.Complete8BitPlayback();
		sound_blaster.Complete16BitPlayback();
		if (elapsed_ticks) *elapsed_ticks = tick - playback_start;
		ploginfo("[SB16] Wait single-cycle done rate=%u bytes=%u elapsed=%u",
			(stduint)sample_rate, (stduint)byte_count,
			(stduint)(tick - playback_start));
		return true;
	}

	bool ProgramSoundBlasterSingleCyclePcm(uint32 byte_count,
		uint16 sample_rate, uni::AudioSampleFormat sample_format,
		uint8 channels, SoundBlasterPcmPath pcm_path) {
		const uint16 programmed_rate =
			GetProgrammedSoundBlasterRate(sample_rate);
		const bool is_16bit = (sample_format == uni::AudioSampleFormat::S16LE);
		const bool is_signed = (sample_format == uni::AudioSampleFormat::S16LE);
		const bool stereo = (channels == 2);
		const uint32 dsp_count = is_16bit ? (byte_count / 2) : byte_count;

		if (is_16bit) {
			return sound_blaster.SpeakerOn() &&
				sound_blaster.SetOutputRate(programmed_rate) &&
				sound_blaster.StartSingleCycle16(dsp_count, is_signed, stereo);
		}

		if (pcm_path == SoundBlasterPcmPath::Modern) {
			return sound_blaster.SpeakerOn() &&
				sound_blaster.SetOutputRate(programmed_rate) &&
				sound_blaster.StartSingleCycle8(dsp_count, is_signed, stereo);
		}
		if (pcm_path == SoundBlasterPcmPath::Legacy && !stereo && !is_signed) {
			return sound_blaster.SpeakerOn() &&
				sound_blaster.SetTimeConstant(programmed_rate) &&
				sound_blaster.StartSingleCycle8Legacy(byte_count);
		}
		return sound_blaster.SpeakerOn() &&
			sound_blaster.SetOutputRate(programmed_rate) &&
			sound_blaster.StartSingleCycle8(dsp_count, is_signed, stereo);
	}

	bool StartSoundBlasterSingleCyclePcm(DeviceNode* node, const uint8* data,
		uint32 byte_count, uint16 sample_rate,
		uni::AudioSampleFormat sample_format, uint8 channels,
		bool log_request, SoundBlasterPcmPath pcm_path) {
		if (!data || !byte_count || byte_count > SoundBlasterDmaBufferSize) {
			plogwarn("[SB16] Invalid PCM request rate=%u bytes=%u",
				(stduint)sample_rate, (stduint)byte_count);
			return false;
		}
		if (!PrepareSoundBlasterDma(node)) return false;
		if (sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire) !=
			SoundBlasterPlaybackMode::Idle ||
			sound_blaster.GetState() != uni::SoundBlasterState::Ready) {
			plogwarn("[SB16] PCM request rejected while device busy state=%u mode=%u",
				(stduint)sound_blaster.GetState(),
				(stduint)sound_blaster_playback_mode.load(
					uni::MemoryOrder_Acquire));
			return false;
		}

		const bool is_16bit = (sample_format == uni::AudioSampleFormat::S16LE);
		if (is_16bit && (byte_count & 1)) {
			plogwarn("[SB16] 16-bit PCM unaligned byte count %u", (stduint)byte_count);
			return false;
		}

		MemCopyN(sound_blaster_dma_buffer, data, byte_count);

		if (is_16bit) {
			if (!IsaDma16Prepare(sound_blaster_dma16_channel,
				(stduint)sound_blaster_dma_buffer, byte_count,
				IsaDmaDirection::MemoryToDevice)) {
				plogwarn("[SB16] ISA DMA16 prepare failed buffer=%p bytes=%u",
					sound_blaster_dma_buffer, (stduint)byte_count);
				return false;
			}
			sound_blaster_playback_mode.store(
				SoundBlasterPlaybackMode::SingleCycle16, uni::MemoryOrder_Release);
		} else {
			if (!IsaDma8Prepare(sound_blaster_dma8_channel,
				(stduint)sound_blaster_dma_buffer, byte_count,
				IsaDmaDirection::MemoryToDevice)) {
				plogwarn("[SB16] ISA DMA8 prepare failed buffer=%p bytes=%u",
					sound_blaster_dma_buffer, (stduint)byte_count);
				return false;
			}
			sound_blaster_playback_mode.store(
				SoundBlasterPlaybackMode::SingleCycle8, uni::MemoryOrder_Release);
		}

		sound_blaster_unexpected_irq_reported = false;
		if (!ProgramSoundBlasterSingleCyclePcm(
			byte_count, sample_rate, sample_format, channels, pcm_path)) {
			sound_blaster_playback_mode.store(
				SoundBlasterPlaybackMode::Idle, uni::MemoryOrder_Release);
			if (is_16bit) IsaDmaMask(sound_blaster_dma16_channel);
			else IsaDmaMask(sound_blaster_dma8_channel);
			plogwarn("[SB16] Failed to start PCM playback path=%u",
				(stduint)pcm_path);
			return false;
		}
		if (log_request) {
			ploginfo("[SB16] PCM playback started rate=%u prog=%u bytes=%u dma=%u fmt=%u ch=%u path=%u",
				(stduint)sample_rate,
				(stduint)GetProgrammedSoundBlasterRate(sample_rate),
				(stduint)byte_count,
				(stduint)(is_16bit ? sound_blaster_dma16_channel : sound_blaster_dma8_channel),
				(stduint)sample_format, (stduint)channels, (stduint)pcm_path);
		}
		else {
			ploginfo("[SB16] PCM playback armed rate=%u prog=%u bytes=%u dma=%u fmt=%u ch=%u path=%u",
				(stduint)sample_rate,
				(stduint)GetProgrammedSoundBlasterRate(sample_rate),
				(stduint)byte_count,
				(stduint)(is_16bit ? sound_blaster_dma16_channel : sound_blaster_dma8_channel),
				(stduint)sample_format, (stduint)channels, (stduint)pcm_path);
		}
		return true;
	}

	stduint SoundBlasterProbeError(const SoundBlasterPcmPathProbe& probe) {
		if (!probe.completed) return ~stduint(0);
		return probe.elapsed_ticks > probe.expected_ticks ?
			probe.elapsed_ticks - probe.expected_ticks :
			probe.expected_ticks - probe.elapsed_ticks;
	}

	bool SoundBlasterProbeCanScale(const SoundBlasterPcmPathProbe& probe) {
		return probe.completed && probe.elapsed_ticks != 0 &&
			probe.expected_ticks != 0;
	}

	void ApplySoundBlasterProbeScale(const SoundBlasterPcmPathProbe& probe) {
		if (!SoundBlasterProbeCanScale(probe) || probe.compatible) {
			sound_blaster_rate_scale_num = 1;
			sound_blaster_rate_scale_den = 1;
			return;
		}
		// If a block finishes in elapsed/expected time, program future blocks at
		// requested_rate * elapsed / expected to compensate a fast virtual DSP.
		sound_blaster_rate_scale_num = probe.elapsed_ticks;
		sound_blaster_rate_scale_den = probe.expected_ticks;
		plogwarn("[SB16] PCM rate compensation %u/%u",
			(stduint)sound_blaster_rate_scale_num,
			(stduint)sound_blaster_rate_scale_den);
	}

	SoundBlasterPcmPathProbe ProbeSoundBlasterPcmPath(
		DeviceNode* node, SoundBlasterPcmPath pcm_path) {
		SoundBlasterPcmPathProbe probe{
			.completed = false,
			.compatible = false,
			.elapsed_ticks = 0,
			.expected_ticks =
				((stduint)SoundBlasterCompatProbeBytes * CONFIG_SysTickFreq +
					SoundBlasterCompatProbeSampleRate - 1) /
				SoundBlasterCompatProbeSampleRate,
		};
		if (!PrepareSoundBlasterDma(node)) return probe;
		MemSet(sound_blaster_dma_buffer, 0x80, SoundBlasterCompatProbeBytes);
		probe.completed = StartSoundBlasterSingleCyclePcm(
			node, sound_blaster_dma_buffer, SoundBlasterCompatProbeBytes,
			SoundBlasterCompatProbeSampleRate, uni::AudioSampleFormat::U8, 1, false,
			pcm_path) &&
			WaitSoundBlasterSingleCycleDone(
				SoundBlasterCompatProbeBytes,
				SoundBlasterCompatProbeSampleRate,
				1,
				&probe.elapsed_ticks);
		if (!probe.completed) {
			plogwarn("[SB16] PCM compat probe path=%u failed elapsed=%u expected=%u",
				(stduint)pcm_path, (stduint)probe.elapsed_ticks,
				(stduint)probe.expected_ticks);
			return probe;
		}
		const stduint min_ticks =
			(probe.expected_ticks * SoundBlasterCompatProbeMinPercent + 99) / 100;
		const stduint max_ticks =
			(probe.expected_ticks * SoundBlasterCompatProbeMaxPercent + 99) / 100;
		probe.compatible =
			probe.elapsed_ticks >= min_ticks && probe.elapsed_ticks <= max_ticks;
		ploginfo("[SB16] PCM compat probe path=%u elapsed=%u expected=%u ok=%u",
			(stduint)pcm_path, (stduint)probe.elapsed_ticks,
			(stduint)probe.expected_ticks, (stduint)probe.compatible);
		return probe;
	}

	bool SelectSoundBlasterPcmPath(DeviceNode* node) {
		if (sound_blaster_pcm_path != SoundBlasterPcmPath::Unknown) return true;
		if (sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire) !=
			SoundBlasterPlaybackMode::Idle ||
			sound_blaster.GetState() != uni::SoundBlasterState::Ready) {
			// The boot smoke test may still be completing when user playback starts.
			// Do not fail the request just because the compatibility probe cannot
			// run yet; use the modern path for this block. VMware has shown that
			// the legacy path can complete a block immediately without audible PCM.
			plogwarn("[SB16] PCM compat probe deferred state=%u mode=%u",
				(stduint)sound_blaster.GetState(),
				(stduint)sound_blaster_playback_mode.load(
					uni::MemoryOrder_Acquire));
			return true;
		}

		const SoundBlasterPcmPathProbe modern_probe =
			ProbeSoundBlasterPcmPath(node, SoundBlasterPcmPath::Modern);
		if (modern_probe.compatible) {
			sound_blaster_pcm_path = SoundBlasterPcmPath::Modern;
			ApplySoundBlasterProbeScale(modern_probe);
		}
		else {
			if (sound_blaster.GetState() == uni::SoundBlasterState::Failed) {
				sound_blaster.Probe();
			}
			const SoundBlasterPcmPathProbe legacy_probe =
				ProbeSoundBlasterPcmPath(node, SoundBlasterPcmPath::Legacy);
			if (legacy_probe.compatible) {
				sound_blaster_pcm_path = SoundBlasterPcmPath::Legacy;
				ApplySoundBlasterProbeScale(legacy_probe);
			}
			else {
				const stduint modern_error = SoundBlasterProbeError(modern_probe);
				const stduint legacy_error = SoundBlasterProbeError(legacy_probe);
				sound_blaster_pcm_path =
					legacy_error < modern_error ?
					SoundBlasterPcmPath::Legacy : SoundBlasterPcmPath::Modern;
				plogwarn("[SB16] PCM probe no exact match modern=%u/%u legacy=%u/%u",
					(stduint)modern_probe.elapsed_ticks,
					(stduint)modern_probe.expected_ticks,
					(stduint)legacy_probe.elapsed_ticks,
					(stduint)legacy_probe.expected_ticks);
				ApplySoundBlasterProbeScale(
					sound_blaster_pcm_path == SoundBlasterPcmPath::Legacy ?
					legacy_probe : modern_probe);
			}
		}
		ploginfo("[SB16] PCM path selected=%u", (stduint)sound_blaster_pcm_path);
		return sound_blaster.GetState() == uni::SoundBlasterState::Ready;
	}

	bool StartSoundBlasterTestTone(DeviceNode* node) {
		if (!PrepareSoundBlasterDma(node)) return false;
		GenerateSoundBlasterTestTone();
		if (!StartSoundBlasterSingleCyclePcm(node, sound_blaster_dma_buffer,
			SoundBlasterTestSampleCount, SoundBlasterTestSampleRate,
			uni::AudioSampleFormat::U8, 1, false,
			GetNominalSoundBlasterPcmPath())) {
			return false;
		}
		ploginfo("[SB16] PCM test started rate=%u bytes=%u dma=%u",
			(stduint)SoundBlasterTestSampleRate,
			(stduint)SoundBlasterTestSampleCount,
			(stduint)sound_blaster_dma8_channel);
		return true;
	}

	bool RunSoundBlasterSingleCyclePcm(DeviceNode* node, const uint8* data,
		uint32 byte_count, uint16 sample_rate,
		uni::AudioSampleFormat sample_format = uni::AudioSampleFormat::U8,
		uint8 channels = 1) {
		if (!SelectSoundBlasterPcmPath(node)) return false;
		const SoundBlasterPcmPath primary_path = GetEffectiveSoundBlasterPcmPath();
		const uint8 frame_size = (sample_format == uni::AudioSampleFormat::S16LE ? 2 : 1) * channels;
		if (StartSoundBlasterSingleCyclePcm(
			node, data, byte_count, sample_rate, sample_format, channels, true,
			primary_path) &&
			WaitSoundBlasterSingleCycleDone(byte_count, sample_rate, frame_size)) {
			return true;
		}

		if (primary_path == SoundBlasterPcmPath::Modern) return false;

		const SoundBlasterPcmPath fallback_path = SoundBlasterPcmPath::Modern;
		if (sound_blaster.GetState() == uni::SoundBlasterState::Failed) {
			sound_blaster.Probe();
		}
		if (sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire) ==
			SoundBlasterPlaybackMode::Idle &&
			sound_blaster.GetState() == uni::SoundBlasterState::Ready) {
			plogwarn("[SB16] PCM retry with fallback path=%u after path=%u",
				(stduint)fallback_path, (stduint)primary_path);
			stduint fallback_elapsed_ticks = 0;
			if (StartSoundBlasterSingleCyclePcm(
				node, data, byte_count, sample_rate, sample_format, channels, true,
				fallback_path) &&
				WaitSoundBlasterSingleCycleDone(
					byte_count, sample_rate, frame_size, &fallback_elapsed_ticks)) {
				if (fallback_elapsed_ticks) {
					sound_blaster_pcm_path = fallback_path;
				}
				return true;
			}
		}
		return false;
	}

	bool StartSoundBlasterAutoInitTest(DeviceNode* node) {
		if (!PrepareSoundBlasterDma(node)) return false;
		if (sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire) !=
			SoundBlasterPlaybackMode::Idle) return false;
		sound_blaster_auto_buffer.sample_rate = SoundBlasterTestSampleRate;
		sound_blaster_auto_buffer.sample_format = uni::AudioSampleFormat::U8;
		sound_blaster_auto_buffer.channels = 1;
		sound_blaster_auto_buffer.is_16bit = false;
		sound_blaster_auto_buffer.is_signed = false;
		sound_blaster_auto_buffer.stereo = false;
		sound_blaster_auto_buffer.refill = nullptr;
		sound_blaster_auto_buffer.refill_context = nullptr;
		if (!sound_blaster_dma_buffer || !PrepareSoundBlasterAutoInitBuffers()) {
			plogwarn("[SB16] Auto-init DMA buffer preparation failed");
			return false;
		}

		const uint32 dma_bytes = SoundBlasterAutoInitBlockCount *
			SoundBlasterAutoInitBlockBytes;
		if (!IsaDma8Prepare(sound_blaster_dma8_channel,
			(stduint)sound_blaster_dma_buffer, dma_bytes,
			IsaDmaDirection::MemoryToDevice,
			IsaDmaReloadMode::AutoInitialize)) {
			plogwarn("[SB16] ISA DMA8 auto-init prepare failed");
			return false;
		}

		// Publish the mode before the DSP can raise its first block IRQ.
		sound_blaster_playback_mode.store(
			SoundBlasterPlaybackMode::AutoInit8, uni::MemoryOrder_Release);
		sound_blaster_unexpected_irq_reported = false;
		if (!sound_blaster.SpeakerOn() ||
			!sound_blaster.SetOutputRate(SoundBlasterTestSampleRate) ||
			!sound_blaster.StartAutoInit8(
				SoundBlasterAutoInitBlockBytes, false, false)) {
			sound_blaster_playback_mode.store(
				SoundBlasterPlaybackMode::Idle, uni::MemoryOrder_Release);
			IsaDmaMask(sound_blaster_dma8_channel);
			plogwarn("[SB16] Failed to start auto-init PCM test");
			return false;
		}
		ploginfo("[SB16] Auto-init PCM test ready rate=%u block=%u dma=%u",
			(stduint)SoundBlasterTestSampleRate,
			(stduint)SoundBlasterAutoInitBlockBytes,
			(stduint)sound_blaster_dma8_channel);
		return true;
	}

	bool StartSoundBlasterAutoInitStream(DeviceNode* node, uint16 sample_rate,
		uni::AudioSampleFormat sample_format, uint8 channels,
		SoundBlasterPcmRefill refill, void* refill_context) {
		if (!refill ||
			sample_rate < SoundBlasterMinimumOutputRate ||
			sample_rate > SoundBlasterMaximumOutputRate) {
			return false;
		}
		if (!PrepareSoundBlasterDma(node)) return false;
		if (sound_blaster.GetDspMajorVersion() < 4) {
			plogwarn("[SB16] Auto-init stream requires SB16 DSP");
			return false;
		}
		if (sound_blaster_pcm_path == SoundBlasterPcmPath::Unknown) {
			sound_blaster_pcm_path = SoundBlasterPcmPath::Modern;
			sound_blaster_rate_scale_num = 1;
			sound_blaster_rate_scale_den = 1;
		}
		if (sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire) !=
			SoundBlasterPlaybackMode::Idle ||
			sound_blaster.GetState() != uni::SoundBlasterState::Ready) {
			return false;
		}

		const bool is_16bit = (sample_format == uni::AudioSampleFormat::S16LE);
		const bool is_signed = (sample_format == uni::AudioSampleFormat::S16LE);
		const bool stereo = (channels == 2);

		sound_blaster_auto_buffer.sample_rate = sample_rate;
		sound_blaster_auto_buffer.sample_format = sample_format;
		sound_blaster_auto_buffer.channels = channels;
		sound_blaster_auto_buffer.is_16bit = is_16bit;
		sound_blaster_auto_buffer.is_signed = is_signed;
		sound_blaster_auto_buffer.stereo = stereo;
		sound_blaster_auto_buffer.refill = refill;
		sound_blaster_auto_buffer.refill_context = refill_context;
		if (!sound_blaster_dma_buffer || !PrepareSoundBlasterAutoInitBuffers()) {
			sound_blaster_auto_buffer.refill = nullptr;
			sound_blaster_auto_buffer.refill_context = nullptr;
			plogwarn("[SB16] Auto-init stream buffer preparation failed");
			return false;
		}

		const uint32 dma_bytes = SoundBlasterAutoInitBlockCount *
			SoundBlasterAutoInitBlockBytes;

		if (is_16bit) {
			if (!IsaDma16Prepare(sound_blaster_dma16_channel,
				(stduint)sound_blaster_dma_buffer, dma_bytes,
				IsaDmaDirection::MemoryToDevice,
				IsaDmaReloadMode::AutoInitialize)) {
				sound_blaster_auto_buffer.refill = nullptr;
				sound_blaster_auto_buffer.refill_context = nullptr;
				plogwarn("[SB16] ISA DMA16 stream prepare failed");
				return false;
			}
			sound_blaster_playback_mode.store(
				SoundBlasterPlaybackMode::AutoInit16, uni::MemoryOrder_Release);
		} else {
			if (!IsaDma8Prepare(sound_blaster_dma8_channel,
				(stduint)sound_blaster_dma_buffer, dma_bytes,
				IsaDmaDirection::MemoryToDevice,
				IsaDmaReloadMode::AutoInitialize)) {
				sound_blaster_auto_buffer.refill = nullptr;
				sound_blaster_auto_buffer.refill_context = nullptr;
				plogwarn("[SB16] ISA DMA8 stream prepare failed");
				return false;
			}
			sound_blaster_playback_mode.store(
				SoundBlasterPlaybackMode::AutoInit8, uni::MemoryOrder_Release);
		}

		sound_blaster_unexpected_irq_reported = false;
		const uint16 programmed_rate = GetProgrammedSoundBlasterRate(sample_rate);
		const uint32 dsp_count = is_16bit ? (SoundBlasterAutoInitBlockBytes / 2) : SoundBlasterAutoInitBlockBytes;
		bool start_ok = false;
		if (sound_blaster.SpeakerOn() && sound_blaster.SetOutputRate(programmed_rate)) {
			if (is_16bit) {
				start_ok = sound_blaster.StartAutoInit16(dsp_count, is_signed, stereo);
			} else {
				start_ok = sound_blaster.StartAutoInit8(dsp_count, is_signed, stereo);
			}
		}

		if (!start_ok) {
			sound_blaster_playback_mode.store(
				SoundBlasterPlaybackMode::Idle, uni::MemoryOrder_Release);
			sound_blaster_auto_buffer.refill = nullptr;
			sound_blaster_auto_buffer.refill_context = nullptr;
			if (is_16bit) IsaDmaMask(sound_blaster_dma16_channel);
			else IsaDmaMask(sound_blaster_dma8_channel);
			plogwarn("[SB16] Failed to start auto-init stream");
			return false;
		}
		sound_blaster_last_activity_tick = tick;
		ploginfo("[SB16] Auto-init stream started rate=%u prog=%u block=%u dma=%u fmt=%u ch=%u",
			(stduint)sample_rate, (stduint)programmed_rate,
			(stduint)SoundBlasterAutoInitBlockBytes,
			(stduint)(is_16bit ? sound_blaster_dma16_channel : sound_blaster_dma8_channel),
			(stduint)sample_format, (stduint)channels);
		return true;
	}

	bool RequestSoundBlasterAutoInitStop() {
		auto mode = sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire);
		if (mode != SoundBlasterPlaybackMode::AutoInit8 &&
			mode != SoundBlasterPlaybackMode::AutoInit16 &&
			mode != SoundBlasterPlaybackMode::AutoInit8Paused &&
			mode != SoundBlasterPlaybackMode::AutoInit16Paused) return false;
		if (!sound_blaster_playback_mode.compare_exchange(
			mode, SoundBlasterPlaybackMode::AutoInitStopping,
			uni::MemoryOrder_Acq_Rel, uni::MemoryOrder_Acquire)) return false;

		const bool exit_ok = sound_blaster.ExitAutoInit();
		if (exit_ok) return true;

		// A failed DSP command cannot provide the expected final block IRQ.
		sound_blaster_playback_mode.store(
			SoundBlasterPlaybackMode::Idle, uni::MemoryOrder_Release);
		sound_blaster_auto_buffer.generation.fetch_add(
			1, uni::MemoryOrder_Acq_Rel);
		sound_blaster_auto_buffer.refill_pending_mask.store(
			0, uni::MemoryOrder_Release);
		IsaDmaMask(sound_blaster_dma8_channel);
		IsaDmaMask(sound_blaster_dma16_channel);
		return false;
	}

	bool RunSoundBlasterAutoInitTest(DeviceNode* node) {
		if (!StartSoundBlasterAutoInitTest(node)) return false;

		const stduint playback_start = tick;
		while (sound_blaster_auto_buffer.completed_count.load(
			uni::MemoryOrder_Acquire) < SoundBlasterAutoInitTestBlocks &&
			tick - playback_start < SoundBlasterAutoInitTestTimeoutTicks) {
			SoundBlasterServicePlayback();
			HALT();
		}
		SoundBlasterServicePlayback();

		const uint32 completed = sound_blaster_auto_buffer.completed_count.load(
			uni::MemoryOrder_Acquire);
		const bool all_blocks_completed =
			completed >= SoundBlasterAutoInitTestBlocks;
		if (!RequestSoundBlasterAutoInitStop()) {
			plogwarn("[SB16] Auto-init PCM stop request failed blocks=%u",
				(stduint)completed);
			return false;
		}

		const stduint stop_start = tick;
		while (sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire) !=
			SoundBlasterPlaybackMode::Idle &&
			tick - stop_start < SoundBlasterAutoInitStopTimeoutTicks) {
			HALT();
		}
		if (sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire) !=
			SoundBlasterPlaybackMode::Idle ||
			sound_blaster.GetState() != uni::SoundBlasterState::Ready) {
			IsaDmaMask(sound_blaster_dma8_channel);
			IsaDmaMask(sound_blaster_dma16_channel);
			plogwarn("[SB16] Auto-init PCM stop timed out");
			return false;
		}
		if (!all_blocks_completed) {
			plogwarn("[SB16] Auto-init PCM test timed out blocks=%u",
				(stduint)completed);
			return false;
		}

		const uint32 refilled = sound_blaster_auto_buffer.refilled_count.load(
			uni::MemoryOrder_Acquire);
		const uint32 refill_misses =
			sound_blaster_auto_buffer.refill_miss_count.load(
				uni::MemoryOrder_Acquire);
		ploginfo("[SB16] Auto-init PCM test complete blocks=%u refilled=%u misses=%u",
			(stduint)completed, (stduint)refilled, (stduint)refill_misses);
		return refill_misses == 0;
	}

	bool StartSoundBlaster(DeviceNode* node) {
		const auto* io_resource = Devsman::FindResource(
			node, DeviceResourceType::IoPortRange, 0);
		if (!io_resource || io_resource->length < SoundBlasterDefaultConfig.io_length ||
			io_resource->start > 0xFFFF) {
			plogwarn("[SB16] Invalid or missing I/O resource");
			return false;
		}

		sound_blaster = uni::SoundBlaster(
			uint16(io_resource->start), sound_blaster_io);
		if (!sound_blaster.Probe()) {
			plogwarn("[SB16] DSP probe failed at %[16H]", uint16(io_resource->start));
			return false;
		}

		ploginfo("[SB16] DSP reset ok");
		ploginfo("[SB16] DSP version %u.%u",
			(stduint)sound_blaster.GetDspMajorVersion(),
			(stduint)sound_blaster.GetDspMinorVersion());
		sound_blaster_pcm_path = SoundBlasterPcmPath::Unknown;
		(void)PrepareSoundBlasterDma(node);
		(void)sound_blaster.ResetMixer();
		(void)sound_blaster.SetVolume(uni::SoundBlasterMixerChannel::MasterVolume, 204, 204);
		(void)sound_blaster.SetVolume(uni::SoundBlasterMixerChannel::VoiceVolume, 204, 204);
		// Register as unified audio backend in Devsman
		(void)Devsman::RegisterAudioBackend(&::g_sb16_audio_backend);
		// Keep boot quiet. Explicit AudioMsg::TEST / playback paths can still
		// exercise the device after normal service scheduling is available.
		return true;
	}
}

bool SoundBlasterRunAutoInitSmokeTest() {
	auto* node = Devsman::FindNamedNode(
		DeviceNodeType::PlatformDevice, "sound-blaster");
	if (!node) {
		plogwarn("[SB16] sound-blaster node not found for auto-init test");
		return false;
	}
	return RunSoundBlasterAutoInitTest(node);
}

bool SoundBlasterPlayPcmImmediate(const uint8* data, uint32 byte_count,
	uint16 sample_rate, uni::AudioSampleFormat sample_format, uint8 channels) {
	auto* node = Devsman::FindNamedNode(
		DeviceNodeType::PlatformDevice, "sound-blaster");
	if (!node) {
		plogwarn("[SB16] sound-blaster node not found for PCM playback");
		return false;
	}
	return RunSoundBlasterSingleCyclePcm(node, data, byte_count, sample_rate, sample_format, channels);
}

bool SoundBlasterPlayPcmU8MonoImmediate(const uint8* data, uint32 byte_count,
	uint16 sample_rate) {
	return SoundBlasterPlayPcmImmediate(data, byte_count, sample_rate, uni::AudioSampleFormat::U8, 1);
}

bool SoundBlasterStartPcmStream(uint16 sample_rate,
	uni::AudioSampleFormat sample_format, uint8 channels,
	SoundBlasterPcmRefill refill, void* context) {
	auto* node = Devsman::FindNamedNode(
		DeviceNodeType::PlatformDevice, "sound-blaster");
	if (!node) {
		plogwarn("[SB16] sound-blaster node not found for PCM stream");
		return false;
	}
	return StartSoundBlasterAutoInitStream(node, sample_rate, sample_format, channels, refill, context);
}

bool SoundBlasterStartPcmU8MonoStream(uint16 sample_rate,
	SoundBlasterPcmRefill refill, void* context) {
	return SoundBlasterStartPcmStream(sample_rate, uni::AudioSampleFormat::U8, 1, refill, context);
}

bool SoundBlasterStopPcmStream() {
	const auto mode = sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire);
	if (mode == SoundBlasterPlaybackMode::Idle) {
		sound_blaster_auto_buffer.refill = nullptr;
		sound_blaster_auto_buffer.refill_context = nullptr;
		return true;
	}
	if (mode != SoundBlasterPlaybackMode::AutoInit8 &&
		mode != SoundBlasterPlaybackMode::AutoInit16 &&
		mode != SoundBlasterPlaybackMode::AutoInit8Paused &&
		mode != SoundBlasterPlaybackMode::AutoInit16Paused &&
		mode != SoundBlasterPlaybackMode::AutoInitStopping) {
		return false;
	}
	if ((mode == SoundBlasterPlaybackMode::AutoInit8 || mode == SoundBlasterPlaybackMode::AutoInit16 ||
		mode == SoundBlasterPlaybackMode::AutoInit8Paused || mode == SoundBlasterPlaybackMode::AutoInit16Paused) &&
		!RequestSoundBlasterAutoInitStop()) {
		return false;
	}

	const stduint stop_start = tick;
	uint32 stop_polls = 0;
	while (sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire) !=
		SoundBlasterPlaybackMode::Idle &&
		tick - stop_start < SoundBlasterAutoInitStopTimeoutTicks &&
		stop_polls++ < SoundBlasterAutoInitStopPollLimit) {
		SoundBlasterServicePlayback();
		asm volatile("pause" ::: "memory");
	}
	const bool stopped =
		sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire) ==
		SoundBlasterPlaybackMode::Idle;
	if (!stopped) {
		IsaDmaMask(sound_blaster_dma8_channel);
		IsaDmaMask(sound_blaster_dma16_channel);
		sound_blaster_playback_mode.store(
			SoundBlasterPlaybackMode::Idle, uni::MemoryOrder_Release);
		sound_blaster_auto_buffer.generation.fetch_add(
			1, uni::MemoryOrder_Acq_Rel);
		sound_blaster_auto_buffer.refill_pending_mask.store(
			0, uni::MemoryOrder_Release);
		sound_blaster_auto_buffer.completed_block.store(
			SoundBlasterInvalidBlock, uni::MemoryOrder_Release);
		sound_blaster.Complete8BitPlayback();
		sound_blaster.Complete16BitPlayback();
		plogwarn("[SB16] PCM stream stop timed out");
	}
	ploginfo("[SB16] Stream stopped. Total completed=%u refilled=%u underruns=%u misses=%u",
		(stduint)sound_blaster_auto_buffer.completed_count.load(uni::MemoryOrder_Relaxed),
		(stduint)sound_blaster_auto_buffer.refilled_count.load(uni::MemoryOrder_Relaxed),
		(stduint)sound_blaster_auto_buffer.underrun_count.load(uni::MemoryOrder_Relaxed),
		(stduint)sound_blaster_auto_buffer.refill_miss_count.load(uni::MemoryOrder_Relaxed));
	sound_blaster_auto_buffer.refill = nullptr;
	sound_blaster_auto_buffer.refill_context = nullptr;
	return stopped;
}

void SoundBlasterAbortPcmStream() {
	const auto mode = sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire);
	if (mode == SoundBlasterPlaybackMode::AutoInit8 ||
		mode == SoundBlasterPlaybackMode::AutoInit16 ||
		mode == SoundBlasterPlaybackMode::AutoInit8Paused ||
		mode == SoundBlasterPlaybackMode::AutoInit16Paused ||
		mode == SoundBlasterPlaybackMode::AutoInitStopping) {
		IsaDmaMask(sound_blaster_dma8_channel);
		IsaDmaMask(sound_blaster_dma16_channel);
		(void)sound_blaster.ExitAutoInit();
	}
	sound_blaster_auto_buffer.refill = nullptr;
	sound_blaster_auto_buffer.refill_context = nullptr;
	sound_blaster_auto_buffer.generation.fetch_add(
		1, uni::MemoryOrder_Acq_Rel);
	sound_blaster_auto_buffer.refill_pending_mask.store(
		0, uni::MemoryOrder_Release);
	sound_blaster_auto_buffer.completed_block.store(
		SoundBlasterInvalidBlock, uni::MemoryOrder_Release);
	sound_blaster_playback_mode.store(
		SoundBlasterPlaybackMode::Idle, uni::MemoryOrder_Release);
	(void)sound_blaster.Reset();
}

	uint8 sound_blaster_master_vol_l = 204;
	uint8 sound_blaster_master_vol_r = 204;
	bool sound_blaster_master_mute = false;

	bool SoundBlasterPausePcmStream() {
		auto mode = sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire);
		if (mode == SoundBlasterPlaybackMode::AutoInit8Paused ||
			mode == SoundBlasterPlaybackMode::AutoInit16Paused) {
			return true;
		}
		if (mode != SoundBlasterPlaybackMode::AutoInit8 &&
			mode != SoundBlasterPlaybackMode::AutoInit16) {
			return false;
		}
		const auto paused_mode = (mode == SoundBlasterPlaybackMode::AutoInit16) ?
			SoundBlasterPlaybackMode::AutoInit16Paused :
			SoundBlasterPlaybackMode::AutoInit8Paused;
		sound_blaster_playback_mode.store(paused_mode, uni::MemoryOrder_Release);

		// Overwrite all pending buffer blocks with silence immediately
		const uint8 last_completed = sound_blaster_auto_buffer.completed_block.load(uni::MemoryOrder_Acquire);
		const uint8 active_block = (last_completed == SoundBlasterInvalidBlock) ? 0 :
			uint8((last_completed + 1) % SoundBlasterAutoInitBlockCount);
		const uint8 silence = sound_blaster_auto_buffer.is_signed ? 0x00 : 0x80;
		for (uint8 block_index = 0; block_index < SoundBlasterAutoInitBlockCount; ++block_index) {
			if (block_index == active_block) continue;
			auto* block = GetSoundBlasterAutoInitBlock(block_index);
			if (block) {
				MemSet(block, silence, sound_blaster_auto_buffer.block_bytes);
			}
		}

		ploginfo("[SB16] Stream paused");
		return true;
	}

	bool SoundBlasterResumePcmStream() {
		auto mode = sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire);
		if (mode == SoundBlasterPlaybackMode::AutoInit8 ||
			mode == SoundBlasterPlaybackMode::AutoInit16) {
			return true;
		}
		if (mode != SoundBlasterPlaybackMode::AutoInit8Paused &&
			mode != SoundBlasterPlaybackMode::AutoInit16Paused) {
			return false;
		}
		const auto resumed_mode = (mode == SoundBlasterPlaybackMode::AutoInit16Paused) ?
			SoundBlasterPlaybackMode::AutoInit16 :
			SoundBlasterPlaybackMode::AutoInit8;
		sound_blaster_playback_mode.store(resumed_mode, uni::MemoryOrder_Release);

		// Immediately refill upcoming blocks with real stream data
		const uint8 last_completed = sound_blaster_auto_buffer.completed_block.load(uni::MemoryOrder_Acquire);
		const uint8 active_block = (last_completed == SoundBlasterInvalidBlock) ? 0 :
			uint8((last_completed + 1) % SoundBlasterAutoInitBlockCount);
		for (uint8 pass = 1; pass < SoundBlasterAutoInitBlockCount; ++pass) {
			const uint8 block_index = uint8((active_block + pass) % SoundBlasterAutoInitBlockCount);
			FillSoundBlasterAutoInitBlock(block_index);
		}

		(void)SoundBlasterServicePlayback();
		ploginfo("[SB16] Stream resumed");
		return true;
	}

	bool SoundBlasterFlushPcmStream() {
		auto mode = sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire);
		if (mode != SoundBlasterPlaybackMode::AutoInit8 &&
			mode != SoundBlasterPlaybackMode::AutoInit16 &&
			mode != SoundBlasterPlaybackMode::AutoInit8Paused &&
			mode != SoundBlasterPlaybackMode::AutoInit16Paused) {
			return false;
		}

		// Immediately refill upcoming blocks with real stream data from new seek position
		const uint8 last_completed = sound_blaster_auto_buffer.completed_block.load(uni::MemoryOrder_Acquire);
		const uint8 active_block = (last_completed == SoundBlasterInvalidBlock) ? 0 :
			uint8((last_completed + 1) % SoundBlasterAutoInitBlockCount);
		for (uint8 pass = 1; pass < SoundBlasterAutoInitBlockCount; ++pass) {
			const uint8 block_index = uint8((active_block + pass) % SoundBlasterAutoInitBlockCount);
			FillSoundBlasterAutoInitBlock(block_index);
		}

		(void)SoundBlasterServicePlayback();
		return true;
	}

	uint64 SoundBlasterGetPlayedBytes() {
		const uint64 completed = sound_blaster_auto_buffer.completed_count.load(
			uni::MemoryOrder_Relaxed);
		return completed * sound_blaster_auto_buffer.block_bytes;
	}

	bool SoundBlasterSetVolume(uni::SoundBlasterMixerChannel channel, uint8 left, uint8 right) {
		if (channel == uni::SoundBlasterMixerChannel::MasterVolume) {
			sound_blaster_master_vol_l = left;
			sound_blaster_master_vol_r = right;
		}
		return sound_blaster.SetVolume(channel, left, right);
	}

	bool SoundBlasterGetVolume(uni::SoundBlasterMixerChannel channel, uint8& left, uint8& right) {
		return sound_blaster.GetVolume(channel, left, right);
	}

	bool SoundBlasterSetMute(uni::SoundBlasterMixerChannel channel, bool mute) {
		if (channel == uni::SoundBlasterMixerChannel::MasterVolume) {
			sound_blaster_master_mute = mute;
		}
		return sound_blaster.SetMute(channel, mute);
	}

bool SoundBlasterResetMixer() {
	return sound_blaster.ResetMixer();
}

uint8 SoundBlasterServicePlayback() {
	const uint32 generation = sound_blaster_auto_buffer.generation.load(
		uni::MemoryOrder_Acquire);
	const uint8 refilled = RefillSoundBlasterPendingBlocks(generation);
	if (refilled) {
		sound_blaster_auto_buffer.refilled_count.fetch_add(
			refilled, uni::MemoryOrder_Relaxed);
		sound_blaster_last_activity_tick = tick;
	}
	return refilled;
}

bool SoundBlasterRecoverDevice() {
	auto mode = sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire);
	const bool was_streaming = (mode == SoundBlasterPlaybackMode::AutoInit8 ||
		mode == SoundBlasterPlaybackMode::AutoInit16 ||
		mode == SoundBlasterPlaybackMode::AutoInit8Paused ||
		mode == SoundBlasterPlaybackMode::AutoInit16Paused);

	plogwarn("[SB16] Initiating device recovery and hardware reset");

	// 1. Mask DMA channels
	IsaDmaMask(sound_blaster_dma8_channel);
	IsaDmaMask(sound_blaster_dma16_channel);

	// 2. Reset DSP
	if (!sound_blaster.Reset()) {
		plogwarn("[SB16] Recovery failed: DSP reset failed");
		return false;
	}

	// 3. Restore mixer settings
	sound_blaster.SetVolume(uni::SoundBlasterMixerChannel::MasterVolume,
		sound_blaster_master_vol_l, sound_blaster_master_vol_r);
	if (sound_blaster_master_mute) {
		sound_blaster.SetMute(uni::SoundBlasterMixerChannel::MasterVolume, true);
	}
	sound_blaster.SetVolume(uni::SoundBlasterMixerChannel::VoiceVolume, 204, 204);

	// 4. If we were streaming, re-arm the auto-init DMA
	if (was_streaming && sound_blaster_auto_buffer.refill) {
		const bool is_16bit = sound_blaster_auto_buffer.is_16bit;
		const bool stereo = sound_blaster_auto_buffer.stereo;
		const uint32 dma_bytes = SoundBlasterAutoInitBlockCount * SoundBlasterAutoInitBlockBytes;

		if (is_16bit) {
			if (!IsaDma16Prepare(sound_blaster_dma16_channel,
				(stduint)sound_blaster_dma_buffer, dma_bytes,
				IsaDmaDirection::MemoryToDevice,
				IsaDmaReloadMode::AutoInitialize)) {
				plogwarn("[SB16] Recovery failed: ISA DMA16 prepare failed");
				return false;
			}
		} else {
			if (!IsaDma8Prepare(sound_blaster_dma8_channel,
				(stduint)sound_blaster_dma_buffer, dma_bytes,
				IsaDmaDirection::MemoryToDevice,
				IsaDmaReloadMode::AutoInitialize)) {
				plogwarn("[SB16] Recovery failed: ISA DMA8 prepare failed");
				return false;
			}
		}

		sound_blaster.Acknowledge8BitIrq();
		sound_blaster.Acknowledge16BitIrq();

		const uint16 prog_rate = GetProgrammedSoundBlasterRate(sound_blaster_auto_buffer.sample_rate);
		const uint32 dsp_count = is_16bit ? (SoundBlasterAutoInitBlockBytes / 2) : SoundBlasterAutoInitBlockBytes;

		bool start_ok = false;
		if (sound_blaster.SpeakerOn() && sound_blaster.SetOutputRate(prog_rate)) {
			if (is_16bit) {
				start_ok = sound_blaster.StartAutoInit16(dsp_count,
					sound_blaster_auto_buffer.is_signed,
					sound_blaster_auto_buffer.stereo);
			} else {
				start_ok = sound_blaster.StartAutoInit8(dsp_count,
					sound_blaster_auto_buffer.is_signed,
					sound_blaster_auto_buffer.stereo);
			}
		}

		if (!start_ok) {
			if (is_16bit) IsaDmaMask(sound_blaster_dma16_channel);
			else IsaDmaMask(sound_blaster_dma8_channel);
			plogwarn("[SB16] Recovery failed: DSP auto-init re-start failed");
			return false;
		}

		sound_blaster_last_activity_tick = tick;
		ploginfo("[SB16] Audio stream successfully recovered and re-armed");
	}

	return true;
}

bool SoundBlasterWatchdogCheck() {
	auto mode = sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire);
	if (mode != SoundBlasterPlaybackMode::AutoInit8 &&
		mode != SoundBlasterPlaybackMode::AutoInit16) {
		return true; // Not actively playing
	}

	const uint8 frame_size = (sound_blaster_auto_buffer.is_16bit ? 2 : 1) *
		sound_blaster_auto_buffer.channels;
	const stduint bytes_per_sec = (stduint)sound_blaster_auto_buffer.sample_rate * (frame_size ? frame_size : 1);
	stduint timeout_ticks = 3 * CONFIG_SysTickFreq;
	if (bytes_per_sec > 0) {
		const stduint expected_block_ticks =
			((stduint)SoundBlasterAutoInitBlockBytes * CONFIG_SysTickFreq * 4) / bytes_per_sec;
		timeout_ticks = expected_block_ticks + 3 * CONFIG_SysTickFreq;
	}

	if (tick - sound_blaster_last_activity_tick > timeout_ticks) {
		plogwarn("[SB16] Watchdog: Audio playback stalled for %u ticks! Attempting recovery...",
			(stduint)(tick - sound_blaster_last_activity_tick));
		sound_blaster_last_activity_tick = tick;
		return SoundBlasterRecoverDevice();
	}
	return true;
}

const AudioBackendDriver g_sb16_audio_backend{
	.name = "sb16",
	.start_stream = SoundBlasterStartPcmStream,
	.stop_stream = SoundBlasterStopPcmStream,
	.pause_stream = SoundBlasterPausePcmStream,
	.resume_stream = SoundBlasterResumePcmStream,
	.flush_stream = SoundBlasterFlushPcmStream,
	.set_volume = SoundBlasterSetVolume,
	.get_volume = SoundBlasterGetVolume,
	.set_mute = SoundBlasterSetMute,
	.watchdog_check = SoundBlasterWatchdogCheck,
	.service_playback = SoundBlasterServicePlayback,
};

void Handint_SB16() {
	sound_blaster.Acknowledge8BitIrq();
	sound_blaster.Acknowledge16BitIrq();
	sound_blaster_last_activity_tick = tick;
	const uint32 count = sound_blaster_irq_count + 1;
	sound_blaster_irq_count = count;
	const auto playback_mode = sound_blaster_playback_mode.load(
		uni::MemoryOrder_Acquire);
	const bool active = (sound_blaster.GetState() == uni::SoundBlasterState::Playing) ||
		(sound_blaster.GetState() == uni::SoundBlasterState::Paused);
	const bool single_cycle_completed = (sound_blaster.GetState() == uni::SoundBlasterState::Playing) &&
		(playback_mode == SoundBlasterPlaybackMode::SingleCycle8 ||
		 playback_mode == SoundBlasterPlaybackMode::SingleCycle16);
	const bool auto_init_block_completed = active &&
		(playback_mode == SoundBlasterPlaybackMode::AutoInit8 ||
		 playback_mode == SoundBlasterPlaybackMode::AutoInit16 ||
		 playback_mode == SoundBlasterPlaybackMode::AutoInit8Paused ||
		 playback_mode == SoundBlasterPlaybackMode::AutoInit16Paused);
	const bool auto_init_stop_completed =
		playback_mode == SoundBlasterPlaybackMode::AutoInitStopping &&
		sound_blaster.GetState() == uni::SoundBlasterState::Stopping;
	if (single_cycle_completed) {
		sound_blaster_playback_mode.store(
			SoundBlasterPlaybackMode::Idle, uni::MemoryOrder_Release);
		if (playback_mode == SoundBlasterPlaybackMode::SingleCycle16) {
			sound_blaster.Complete16BitPlayback();
			IsaDmaMask(sound_blaster_dma16_channel);
		} else {
			sound_blaster.Complete8BitPlayback();
			IsaDmaMask(sound_blaster_dma8_channel);
		}
	}
	else if (auto_init_block_completed) {
		// Keep DSP and DMA running; ordinary context refills this completed half.
		MarkSoundBlasterAutoInitBlockComplete();
	}
	else if (auto_init_stop_completed) {
		sound_blaster_playback_mode.store(
			SoundBlasterPlaybackMode::Idle, uni::MemoryOrder_Release);
		sound_blaster_auto_buffer.generation.fetch_add(
			1, uni::MemoryOrder_Acq_Rel);
		sound_blaster_auto_buffer.refill_pending_mask.store(
			0, uni::MemoryOrder_Release);
		sound_blaster.Complete8BitPlayback();
		sound_blaster.Complete16BitPlayback();
		IsaDmaMask(sound_blaster_dma8_channel);
		IsaDmaMask(sound_blaster_dma16_channel);
	}
	IC.SendEOI(IRQ_SB16);
	if (single_cycle_completed) {
		ploginfo("[SB16] PCM test complete irq-count=%u", (stduint)count);
	}
	else if (!auto_init_block_completed && !auto_init_stop_completed &&
		!sound_blaster_unexpected_irq_reported) {
		sound_blaster_unexpected_irq_reported = true;
		plogwarn("[SB16] Unexpected IRQ5 count=%u", (stduint)count);
	}
}

_ESYM_C void R_SB16_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_SB16{
	.init = R_SB16_INIT,
	.name = "Sound Blaster 16",
};

void R_SB16_INIT() {
	auto* node = Devsman::RegisterPlatformDevice("sound-blaster");
	if (!node) return;
	if (!Devsman::AddIoPortResource(node, 0,
		SoundBlasterDefaultConfig.io_base, SoundBlasterDefaultConfig.io_length)) {
		plogwarn("[SB16] Failed to register I/O resource");
		return;
	}
	if (!Devsman::AddIrqResource(node, IRQ_SB16)) {
		plogwarn("[SB16] Failed to register IRQ resource");
		return;
	}
	if (!Devsman::AddDmaResource(node, 0, SoundBlasterDefaultConfig.dma8, 8) ||
		!Devsman::AddDmaResource(node, 1, SoundBlasterDefaultConfig.dma16, 16)) {
		plogwarn("[SB16] Failed to register DMA resources");
		return;
	}
	IC[IRQ_SB16].setRange(mglb(Handint_SB16_Entry), SegCo32);
	register_interrupt_handler(IRQ_SB16, Handint_SB16);
	if (IC.getType() == 0) i8259Master_Enable(DEV_MAS_SB16);
	if (!Devsman::RegisterDriverStarter("sb16", StartSoundBlaster)) {
		plogwarn("[SB16] Failed to register driver starter");
		return;
	}
	(void)Devsman::RegisterPlatformDevice(
		"sound-blaster", "sb16", &sound_blaster);
}
#endif
