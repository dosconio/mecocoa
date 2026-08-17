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

	bool PlayImmediate(const uni::AudioPlayRequest& request,
		ProcessBlock* source_process = nullptr) {
		if (!IsSupportedPcmRequest(request)) return false;

		const uint8* pcm_data = static_cast<const uint8*>(request.buffer.data);
		byte* copied_pcm = nullptr;

		// User-space IPC carries a user virtual pointer inside the request.
		// Copy it into kernel memory before the audio service dereferences it.
		if (source_process && source_process->ring != RING_M) {
			copied_pcm = new byte[request.buffer.byte_count];
			if (!copied_pcm) return false;
			const stduint copied = MccaMemCopyP(
				copied_pcm, nullptr, true,
				request.buffer.data, source_process, false,
				request.buffer.byte_count);
			if (copied != request.buffer.byte_count) {
				delete[] copied_pcm;
				plogwarn("[Audio] Failed to copy user PCM bytes=%u/%u",
					(stduint)copied, (stduint)request.buffer.byte_count);
				return false;
			}
			pcm_data = copied_pcm;
		}

		const bool ok = SoundBlasterPlayPcmU8MonoImmediate(
			pcm_data,
			request.buffer.byte_count,
			uint16(request.format.sample_rate));
		delete[] copied_pcm;
		return ok;
	}

	bool ReadWholeFile(const char* path, byte*& out_data, uint32& out_size) {
		out_data = nullptr;
		out_size = 0;
		if (!path) return false;

		ProcessBlock* pb = Taskman::CurrentPB();
		if (!pb) return false;

		const stdsint fd = pb->Open(path, O_RDONLY);
		if (fd < 0) {
			plogwarn("[Audio] Failed to open wav file %s", path);
			return false;
		}

		uint32 file_size = 0;
		{
			auto files = pb->fileman.Lock();
			if (fd < 0 || fd >= (stdsint)files->pfiles.Count() ||
				!files->pfiles[fd] || !files->pfiles[fd]->vfile ||
				!files->pfiles[fd]->vfile->f_inode) {
				pb->Close(fd);
				return false;
			}
			file_size = uint32(files->pfiles[fd]->vfile->f_inode->i_size);
		}
		if (!file_size) {
			pb->Close(fd);
			return false;
		}

		byte* buffer = new byte[file_size];
		if (!buffer) {
			pb->Close(fd);
			return false;
		}

		const stduint read_bytes = pb->Rdwt(false, fd, uni::Slice{
			(stduint)buffer, file_size });
		pb->Close(fd);
		if (read_bytes != file_size) {
			delete[] buffer;
			plogwarn("[Audio] Failed to read wav file %s bytes=%u/%u",
				path, (stduint)read_bytes, (stduint)file_size);
			return false;
		}

		out_data = buffer;
		out_size = file_size;
		return true;
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

bool AudioPlayWavFile(const char* path) {
	byte* wav_data = nullptr;
	uint32 wav_size = 0;
	if (!ReadWholeFile(path, wav_data, wav_size)) return false;
	const bool ok = AudioPlayWav(wav_data, wav_size);
	delete[] wav_data;
	return ok;
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
	ploginfo("[Audio] Service thread start pid=%u", Taskman::CurrentPID());

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
			ProcessBlock* source_process = nullptr;
			if (auto* source_thread = Taskman::LocateThread(sig_src)) {
				source_process = source_thread->parent_process;
			}
			stdsint result = PlayImmediate(request, source_process) ? 0 : -1;
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

