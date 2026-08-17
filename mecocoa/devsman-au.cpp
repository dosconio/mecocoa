// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] Device Management - Audio
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../include/mecocoa.hpp"
#include "../devdriv/audio/au-sb16.hpp"
#include <c/format/audio/WAV.h>

namespace {
	bool IsSupportedPcmRequest(const uni::AudioPlayRequest& request) {
		return request.buffer.data &&
			request.buffer.byte_count &&
			request.format.sample_format == uni::AudioSampleFormat::U8 &&
			request.format.channels == 1 &&
			request.format.sample_rate >= 5000 &&
			request.format.sample_rate <= 45000;
	}
}

bool AudioPlay(const uni::AudioPlayRequest& request) {
	if (!IsSupportedPcmRequest(request)) return false;
	stdsint result = -1;
	if (syssend(Task_Audio_Serv, &request, sizeof(request),
		_IMM(AudioMsg::PLAY_PCM_U8_MONO))) return false;
	if (sysrecv(Task_Audio_Serv, &result, sizeof(result))) return false;
	return result == 0;
}

bool AudioPlayWav(const void* wav_data, uint32 wav_size) {
	WAVPCMVIEW wav{};
	if (!WAV_ParsePCM(wav_data, wav_size, &wav)) return false;
	if (wav.audio_format != WAV_FORMAT_PCM) return false;

	uni::AudioPlayRequest request{};
	request.format.sample_rate = wav.sample_rate;
	request.format.channels = wav.channel_count;
	switch (wav.bits_per_sample) {
	case 8:
		request.format.sample_format = uni::AudioSampleFormat::U8;
		break;
	case 16:
		request.format.sample_format = uni::AudioSampleFormat::S16LE;
		break;
	default:
		return false;
	}
	request.buffer.data = wav.pcm_data;
	request.buffer.byte_count = wav.pcm_size;
	return AudioPlay(request);
}

bool SoundBlasterPlayPcmU8Mono(const uint8* data, uint32 byte_count,
	uint16 sample_rate) {
	uni::AudioPlayRequest request{};
	request.format.sample_format = uni::AudioSampleFormat::U8;
	request.format.channels = 1;
	request.format.sample_rate = sample_rate;
	request.buffer.data = data;
	request.buffer.byte_count = byte_count;
	return AudioPlay(request);
}

void serv_dev_audio_loop() {
	// Wait until timer IRQs are advancing before accepting IRQ-driven playback.
	const stduint start_tick = tick;
	while (tick == start_tick) {
		HALT();
		Taskman::Schedule(true);
	}

	// Give the rest of early boot one more tick to settle after enabling IRQs.
	const stduint ready_tick = tick + 1;
	while (tick < ready_tick) {
		HALT();
		Taskman::Schedule(true);
	}

	stduint sig_type = 0;
	stduint sig_src = 0;
	uni::AudioPlayRequest request{};
	while (true) {
		sysrecv(ANYPROC, &request, sizeof(request), &sig_type, &sig_src);
		switch ((AudioMsg)sig_type) {
		case AudioMsg::TEST:
		{
			stdsint result = SoundBlasterRunAutoInitSmokeTest() ? 0 : -1;
			if (sig_src) syssend(sig_src, &result, sizeof(result));
			break;
		}
		case AudioMsg::PLAY_PCM_U8_MONO:
		{
			stdsint result = SoundBlasterPlayPcmU8MonoImmediate(
				static_cast<const uint8*>(request.buffer.data),
				request.buffer.byte_count,
				uint16(request.format.sample_rate)) ? 0 : -1;
			if (sig_src) syssend(sig_src, &result, sizeof(result));
			break;
		}
		default:
			plogwarn("[Audio] Unknown message type=%u src=%u",
				sig_type, sig_src);
			break;
		}
	}
}

