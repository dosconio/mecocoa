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
namespace {
	constexpr uint16 SoundBlasterTestSampleRate = 11025;
	constexpr uint16 SoundBlasterTestFrequency = 440;
	constexpr uint32 SoundBlasterTestDurationMs = 250;
	constexpr uint32 SoundBlasterTestSampleCount =
		(uint32(SoundBlasterTestSampleRate) * SoundBlasterTestDurationMs) / 1000;
	constexpr stduint SoundBlasterDmaBufferSize = 4096;
	constexpr uint8 SoundBlasterAutoInitBlockCount = 2;
	constexpr uint8 SoundBlasterInvalidBlock = 0xFF;
	constexpr uint32 SoundBlasterAutoInitBlockBytes = 1024;
	constexpr uint32 SoundBlasterAutoInitTestBlocks = 8;
	constexpr stduint SoundBlasterAutoInitTestTimeoutTicks =
		3 * CONFIG_SysTickFreq;
	constexpr stduint SoundBlasterAutoInitStopTimeoutTicks =
		CONFIG_SysTickFreq;
	constexpr stduint SoundBlasterSingleCycleStopTimeoutExtraTicks =
		CONFIG_SysTickFreq;
	static_assert(SoundBlasterAutoInitBlockCount * SoundBlasterAutoInitBlockBytes <=
		SoundBlasterDmaBufferSize);

	enum class SoundBlasterPlaybackMode : uint8 {
		Idle,
		SingleCycle8,
		AutoInit8,
		AutoInitStopping,
	};

	struct SoundBlasterAutoInitBufferState {
		uint8* storage;
		uint32 block_bytes;
		uint32 phase;
		// Generation rejects refill work left over from an earlier playback.
		uni::Atomic<uint32> generation;
		uni::Atomic<uint8> completed_block;
		uni::Atomic<uint8> refill_pending_mask;
		uni::Atomic<uint32> completed_count;
		uni::Atomic<uint32> refilled_count;
		uni::Atomic<uint32> refill_miss_count;
	};

	volatile uint32 sound_blaster_irq_count;
	uni::Atomic<SoundBlasterPlaybackMode> sound_blaster_playback_mode{
		SoundBlasterPlaybackMode::Idle};
	volatile bool sound_blaster_unexpected_irq_reported;
	uint8 sound_blaster_dma8_channel;
	uint8* sound_blaster_dma_buffer;
	SoundBlasterAutoInitBufferState sound_blaster_auto_buffer{
		.storage = nullptr,
		.block_bytes = SoundBlasterAutoInitBlockBytes,
		.phase = 0,
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
		FillSoundBlasterTestTone(block, sound_blaster_auto_buffer.block_bytes,
			sound_blaster_auto_buffer.phase);
		return true;
	}

	bool PrepareSoundBlasterAutoInitBuffers() {
		sound_blaster_auto_buffer.storage = sound_blaster_dma_buffer;
		sound_blaster_auto_buffer.phase = 0;
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
		// Each auto-init IRQ completes the other half of the circular DMA buffer.
		const uint8 completed_block = previous_block == SoundBlasterInvalidBlock ?
			0 : uint8(previous_block ^ 1);
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
		uint8 first_block = last_completed;
		// If both halves are pending, refill the older completion first.
		if (pending_mask == 0x03) first_block ^= 1;

		uint8 refill_count = 0;
		for (uint8 pass = 0; pass < SoundBlasterAutoInitBlockCount; ++pass) {
			const uint8 block_index = uint8(first_block ^ pass);
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

	bool PrepareSoundBlasterDma8(DeviceNode* node) {
		const auto* dma_resource = Devsman::FindResource(
			node, DeviceResourceType::DmaChannel, 0);
		if (!dma_resource || dma_resource->start > 3 || dma_resource->extra != 8) {
			plogwarn("[SB16] Invalid or missing DMA8 resource");
			return false;
		}
		sound_blaster_dma8_channel = uint8(dma_resource->start);
		if (!sound_blaster_dma_buffer) {
			sound_blaster_dma_buffer = static_cast<uint8*>(
				mempool.allocate(SoundBlasterDmaBufferSize, PAGESIZE_4KB, 16));
		}
		if (!sound_blaster_dma_buffer) {
			plogwarn("[SB16] ISA DMA buffer allocation failed");
			return false;
		}
		return true;
	}

	bool WaitSoundBlasterSingleCycleDone(uint32 byte_count, uint16 sample_rate) {
		const stduint playback_ticks = ((stduint)byte_count * CONFIG_SysTickFreq +
			sample_rate - 1) / sample_rate;
		const stduint timeout_ticks =
			playback_ticks + SoundBlasterSingleCycleStopTimeoutExtraTicks;
		const stduint playback_start = tick;
		while (sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire) !=
			SoundBlasterPlaybackMode::Idle &&
			tick - playback_start < timeout_ticks) {
			HALT();
			Taskman::Schedule(true);
		}
		if (sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire) !=
			SoundBlasterPlaybackMode::Idle ||
			sound_blaster.GetState() != uni::SoundBlasterState::Ready) {
			IsaDmaMask(sound_blaster_dma8_channel);
			sound_blaster_playback_mode.store(
				SoundBlasterPlaybackMode::Idle, uni::MemoryOrder_Release);
			sound_blaster.Complete8BitPlayback();
			plogwarn("[SB16] PCM playback timed out rate=%u bytes=%u",
				(stduint)sample_rate, (stduint)byte_count);
			return false;
		}
		return true;
	}

	bool StartSoundBlasterSingleCyclePcm(DeviceNode* node, const uint8* data,
		uint32 byte_count, uint16 sample_rate, bool log_request) {
		if (!data || !byte_count || byte_count > SoundBlasterDmaBufferSize) {
			plogwarn("[SB16] Invalid PCM request rate=%u bytes=%u",
				(stduint)sample_rate, (stduint)byte_count);
			return false;
		}
		if (!PrepareSoundBlasterDma8(node)) return false;
		if (sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire) !=
			SoundBlasterPlaybackMode::Idle ||
			sound_blaster.GetState() != uni::SoundBlasterState::Ready) {
			plogwarn("[SB16] PCM request rejected while device busy state=%u mode=%u",
				(stduint)sound_blaster.GetState(),
				(stduint)sound_blaster_playback_mode.load(
					uni::MemoryOrder_Acquire));
			return false;
		}

		MemCopyN(sound_blaster_dma_buffer, data, byte_count);
		if (!IsaDma8Prepare(sound_blaster_dma8_channel,
			(stduint)sound_blaster_dma_buffer, byte_count,
			IsaDmaDirection::MemoryToDevice)) {
			plogwarn("[SB16] ISA DMA8 prepare failed buffer=%p bytes=%u",
				sound_blaster_dma_buffer, (stduint)byte_count);
			return false;
		}

		sound_blaster_playback_mode.store(
			SoundBlasterPlaybackMode::SingleCycle8, uni::MemoryOrder_Release);
		sound_blaster_unexpected_irq_reported = false;
		if (!sound_blaster.SpeakerOn() ||
			!sound_blaster.SetOutputRate(sample_rate) ||
			!sound_blaster.StartSingleCycle8(byte_count, false, false)) {
			sound_blaster_playback_mode.store(
				SoundBlasterPlaybackMode::Idle, uni::MemoryOrder_Release);
			IsaDmaMask(sound_blaster_dma8_channel);
			plogwarn("[SB16] Failed to start PCM playback");
			return false;
		}
		if (log_request) {
			ploginfo("[SB16] PCM playback started rate=%u bytes=%u dma=%u",
				(stduint)sample_rate, (stduint)byte_count,
				(stduint)sound_blaster_dma8_channel);
		}
		return true;
	}

	bool StartSoundBlasterTestTone(DeviceNode* node) {
		if (!PrepareSoundBlasterDma8(node)) return false;
		GenerateSoundBlasterTestTone();
		if (!StartSoundBlasterSingleCyclePcm(node, sound_blaster_dma_buffer,
			SoundBlasterTestSampleCount, SoundBlasterTestSampleRate, false)) {
			return false;
		}
		ploginfo("[SB16] PCM test started rate=%u bytes=%u dma=%u",
			(stduint)SoundBlasterTestSampleRate,
			(stduint)SoundBlasterTestSampleCount,
			(stduint)sound_blaster_dma8_channel);
		return true;
	}

	bool RunSoundBlasterSingleCyclePcm(DeviceNode* node, const uint8* data,
		uint32 byte_count, uint16 sample_rate) {
		if (!StartSoundBlasterSingleCyclePcm(
			node, data, byte_count, sample_rate, true)) return false;
		return WaitSoundBlasterSingleCycleDone(byte_count, sample_rate);
	}

	bool StartSoundBlasterAutoInitTest(DeviceNode* node) {
		if (!PrepareSoundBlasterDma8(node)) return false;
		if (sound_blaster_playback_mode.load(uni::MemoryOrder_Acquire) !=
			SoundBlasterPlaybackMode::Idle) return false;
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

	bool RequestSoundBlasterAutoInitStop() {
		auto expected_mode = SoundBlasterPlaybackMode::AutoInit8;
		if (!sound_blaster_playback_mode.compare_exchange(
			expected_mode, SoundBlasterPlaybackMode::AutoInitStopping,
			uni::MemoryOrder_Acq_Rel, uni::MemoryOrder_Acquire)) return false;
		if (sound_blaster.ExitAutoInit8()) return true;

		// A failed DSP command cannot provide the expected final block IRQ.
		sound_blaster_playback_mode.store(
			SoundBlasterPlaybackMode::Idle, uni::MemoryOrder_Release);
		sound_blaster_auto_buffer.generation.fetch_add(
			1, uni::MemoryOrder_Acq_Rel);
		sound_blaster_auto_buffer.refill_pending_mask.store(
			0, uni::MemoryOrder_Release);
		IsaDmaMask(sound_blaster_dma8_channel);
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
		// Driver probing runs before the kernel enables normal IRQ dispatch.
		// Keep the asynchronous single-cycle smoke test on the boot path.
		return StartSoundBlasterTestTone(node);
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

bool SoundBlasterPlayPcmU8MonoImmediate(const uint8* data, uint32 byte_count,
	uint16 sample_rate) {
	auto* node = Devsman::FindNamedNode(
		DeviceNodeType::PlatformDevice, "sound-blaster");
	if (!node) {
		plogwarn("[SB16] sound-blaster node not found for PCM playback");
		return false;
	}
	return RunSoundBlasterSingleCyclePcm(node, data, byte_count, sample_rate);
}

uint8 SoundBlasterServicePlayback() {
	const uint32 generation = sound_blaster_auto_buffer.generation.load(
		uni::MemoryOrder_Acquire);
	const uint8 refilled = RefillSoundBlasterPendingBlocks(generation);
	if (refilled) {
		sound_blaster_auto_buffer.refilled_count.fetch_add(
			refilled, uni::MemoryOrder_Relaxed);
	}
	return refilled;
}

void Handint_SB16() {
	sound_blaster.Acknowledge8BitIrq();
	const uint32 count = sound_blaster_irq_count + 1;
	sound_blaster_irq_count = count;
	const auto playback_mode = sound_blaster_playback_mode.load(
		uni::MemoryOrder_Acquire);
	const bool playing = sound_blaster.GetState() ==
		uni::SoundBlasterState::Playing;
	const bool single_cycle_completed = playing && playback_mode ==
		SoundBlasterPlaybackMode::SingleCycle8;
	const bool auto_init_block_completed = playing && playback_mode ==
		SoundBlasterPlaybackMode::AutoInit8;
	const bool auto_init_stop_completed =
		playback_mode == SoundBlasterPlaybackMode::AutoInitStopping &&
		sound_blaster.GetState() == uni::SoundBlasterState::Stopping;
	if (single_cycle_completed) {
		sound_blaster_playback_mode.store(
			SoundBlasterPlaybackMode::Idle, uni::MemoryOrder_Release);
		sound_blaster.Complete8BitPlayback();
		IsaDmaMask(sound_blaster_dma8_channel);
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
		IsaDmaMask(sound_blaster_dma8_channel);
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
