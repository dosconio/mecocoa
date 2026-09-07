// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] Device Management - Audio
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../include/mecocoa.hpp"
#include "../devdriv/audio/au-sb16.hpp"
#include <c/format/audio/WAV.h>

#if _MCCA == 0x8632

namespace {
	constexpr uint32 AudioStreamChunkBytes = 4096;
	constexpr uint32 AudioStreamRingBytes = AudioStreamChunkBytes * 16;
	constexpr uint32 AudioStreamDmaPrimeBytes = 4096 * 4;

	struct AudioStreamState {
		uint8 ring[AudioStreamRingBytes];
		uint32 read_pos;
		uint32 write_pos;
		uint32 used;
		stduint owner_tid;
		uint16 sample_rate;
		uni::AudioSampleFormat sample_format;
		uint8 channels;
		uint64 played_bytes;
		bool open;
		bool started;
		bool paused;
	};

	AudioStreamState audio_stream{};

	bool IsSupportedPcmFormat(const uni::AudioPlayRequest& request) {
		const bool valid_format =
			(request.format.sample_format == uni::AudioSampleFormat::U8 ||
			 request.format.sample_format == uni::AudioSampleFormat::S16LE);
		const bool valid_channels =
			(request.format.channels == 1 || request.format.channels == 2);
		return valid_format && valid_channels &&
			request.format.sample_rate >= 5000 &&
			request.format.sample_rate <= 45000;
	}

	bool IsSupportedPcmRequest(const uni::AudioPlayRequest& request) {
		return request.buffer.data &&
			request.buffer.byte_count &&
			IsSupportedPcmFormat(request);
	}

	uint32 AudioRingFree() {
		return AudioStreamRingBytes - audio_stream.used;
	}

	void AudioRingReset() {
		audio_stream.read_pos = 0;
		audio_stream.write_pos = 0;
		audio_stream.used = 0;
	}

	uint32 AudioRingRead(uint8* destination, uint32 byte_count) {
		if (!destination || !byte_count || !audio_stream.used) return 0;
		uint32 done = 0;
		while (done < byte_count && audio_stream.used) {
			uint32 chunk = byte_count - done;
			const uint32 until_end = AudioStreamRingBytes - audio_stream.read_pos;
			if (chunk > audio_stream.used) chunk = audio_stream.used;
			if (chunk > until_end) chunk = until_end;
			MemCopyN(destination + done, audio_stream.ring + audio_stream.read_pos,
				chunk);
			audio_stream.read_pos =
				(audio_stream.read_pos + chunk) % AudioStreamRingBytes;
			audio_stream.used -= chunk;
			audio_stream.played_bytes += chunk;
			done += chunk;
		}
		return done;
	}

	uint32 AudioStreamRefill(void*, uint8* destination, uint32 byte_count) {
		if (audio_stream.paused) return 0;
		return AudioRingRead(destination, byte_count);
	}

	bool IsAudioStreamOwnerAlive() {
		if (!audio_stream.owner_tid) return true;
		auto* owner_process = ProcessBlock::AcquireActive(audio_stream.owner_tid);
		if (!owner_process) return false;
		ProcessBlock::Release(owner_process);
		return true;
	}

	void AudioStreamClear() {
		AudioRingReset();
		audio_stream.owner_tid = 0;
		audio_stream.sample_rate = 0;
		audio_stream.sample_format = uni::AudioSampleFormat::U8;
		audio_stream.channels = 1;
		audio_stream.played_bytes = 0;
		audio_stream.open = false;
		audio_stream.started = false;
		audio_stream.paused = false;
	}

	void AudioStreamAbort() {
		SoundBlasterAbortPcmStream();
		AudioStreamClear();
	}

	void AudioServicePollStream() {
		if (!audio_stream.open) return;
		if (!IsAudioStreamOwnerAlive()) {
			plogwarn("[Audio] PCM stream owner gone tid=%u, abort",
				(stduint)audio_stream.owner_tid);
			AudioStreamAbort();
			return;
		}
		SoundBlasterServicePlayback();
	}

	void AudioServiceIdleWait() {
		syscall(syscall_t::REST, 1, 1);
	}

	bool AudioServiceHasMessage() {
		return syscall(syscall_t::TMSG) != 0;
	}

	bool AudioStreamStartIfReady(bool force) {
		if (!audio_stream.open || audio_stream.started || audio_stream.paused) return true;
		if (!force && audio_stream.used < AudioStreamDmaPrimeBytes) return true;
		if (!audio_stream.used) return true;
		if (!SoundBlasterStartPcmStream(
			audio_stream.sample_rate,
			audio_stream.sample_format,
			audio_stream.channels,
			AudioStreamRefill, nullptr)) {
			plogwarn("[Audio] Failed to start PCM stream rate=%u used=%u fmt=%u ch=%u",
				(stduint)audio_stream.sample_rate, (stduint)audio_stream.used,
				(stduint)audio_stream.sample_format, (stduint)audio_stream.channels);
			return false;
		}
		audio_stream.started = true;
		return true;
	}

	bool AudioStreamBegin(const uni::AudioPlayRequest& request, stduint owner_tid) {
		if (!IsSupportedPcmFormat(request)) return false;
		AudioStreamAbort();
		audio_stream.owner_tid = owner_tid;
		audio_stream.sample_rate = uint16(request.format.sample_rate);
		audio_stream.sample_format = request.format.sample_format;
		audio_stream.channels = request.format.channels ? request.format.channels : 1;
		audio_stream.open = true;
		return true;
	}

	stduint CopyPcmToRing(const void* source, ProcessBlock* source_process,
		uint32 byte_count) {
		if (!source || !byte_count) return 0;
		uint32 copied_total = 0;
		while (copied_total < byte_count && AudioRingFree()) {
			uint32 chunk = byte_count - copied_total;
			const uint32 free_bytes = AudioRingFree();
			const uint32 until_end =
				AudioStreamRingBytes - audio_stream.write_pos;
			if (chunk > free_bytes) chunk = free_bytes;
			if (chunk > until_end) chunk = until_end;

			const void* source_at = reinterpret_cast<const void*>(
				(stduint)source + copied_total);
			if (source_process && source_process->ring != RING_M) {
				const stduint copied = MccaMemCopyP(
					audio_stream.ring + audio_stream.write_pos,
					nullptr, true,
					source_at, source_process, false,
					chunk);
				if (copied != chunk) return copied_total + copied;
			}
			else {
				MemCopyN(audio_stream.ring + audio_stream.write_pos,
					source_at, chunk);
			}
			audio_stream.write_pos =
				(audio_stream.write_pos + chunk) % AudioStreamRingBytes;
			audio_stream.used += chunk;
			copied_total += chunk;
		}
		return copied_total;
	}

	stdsint AudioStreamWrite(const uni::AudioPlayRequest& request,
		ProcessBlock* source_process, stduint source_tid) {
		if (!audio_stream.open || !IsSupportedPcmRequest(request) ||
			uint16(request.format.sample_rate) != audio_stream.sample_rate ||
			request.format.sample_format != audio_stream.sample_format ||
			request.format.channels != audio_stream.channels ||
			source_tid != audio_stream.owner_tid) {
			return -1;
		}
		AudioServicePollStream();
		if (!audio_stream.open) return -1;
		const stduint copied = CopyPcmToRing(
			request.buffer.data, source_process, request.buffer.byte_count);
		if (!AudioStreamStartIfReady(false) && !copied) return -1;
		return stdsint(copied);
	}

	stdsint AudioStreamDrain() {
		if (!audio_stream.open) return 0;
		if (!AudioStreamStartIfReady(true)) return -1;

		const uint8 frame_size = (audio_stream.sample_format == uni::AudioSampleFormat::S16LE ? 2 : 1) * audio_stream.channels;
		const stduint bytes_per_sec = (stduint)audio_stream.sample_rate * (frame_size ? frame_size : 1);

		const stduint drain_timeout =
			((stduint)(audio_stream.used + AudioStreamDmaPrimeBytes) *
				CONFIG_SysTickFreq + bytes_per_sec - 1) /
			bytes_per_sec + 2 * CONFIG_SysTickFreq;
		const stduint drain_start = tick;
		while (audio_stream.open && audio_stream.used &&
			tick - drain_start < drain_timeout) {
			AudioServicePollStream();
			AudioServiceIdleWait();
		}
		if (!audio_stream.open) return -1;
		if (audio_stream.used) {
			plogwarn("[Audio] PCM stream drain timed out used=%u",
				(stduint)audio_stream.used);
			AudioStreamAbort();
			return -1;
		}

		const stduint tail_ticks =
			((stduint)AudioStreamDmaPrimeBytes * CONFIG_SysTickFreq +
				bytes_per_sec - 1) /
			bytes_per_sec + CONFIG_SysTickFreq / 10 + 1;
		const stduint tail_start = tick;
		while (audio_stream.open && audio_stream.started &&
			tick - tail_start < tail_ticks) {
			AudioServicePollStream();
			AudioServiceIdleWait();
		}
		const bool stopped = SoundBlasterStopPcmStream();
		if (!stopped) {
			SoundBlasterAbortPcmStream();
		}
		AudioStreamClear();
		if (!stopped) {
			plogwarn("[Audio] PCM stream drained, stop completed by fallback");
		}
		return 0;
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

		const bool ok = SoundBlasterPlayPcmImmediate(
			pcm_data,
			request.buffer.byte_count,
			uint16(request.format.sample_rate),
			request.format.sample_format,
			request.format.channels ? request.format.channels : 1);
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
		_IMM(AudioMsg::PLAY_PCM))) return false;
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
		AudioServicePollStream();
		if (audio_stream.open && !AudioServiceHasMessage()) {
			AudioServiceIdleWait();
			continue;
		}
		sysrecv(ANYPROC, &request, sizeof(request), &sig_type, &sig_src);
		switch ((AudioMsg)sig_type) {
		case AudioMsg::TEST:
		{
			stdsint result = SoundBlasterRunAutoInitSmokeTest() ? 0 : -1;
			if (sig_src) syssend(sig_src, &result, sizeof(result));
			break;
		}
		case AudioMsg::PLAY_PCM:
		{
			ProcessBlock* source_process = nullptr;
			if (auto* source_thread = Taskman::LocateThread(sig_src)) {
				source_process = source_thread->parent_process;
			}
			stdsint result = PlayImmediate(request, source_process) ? 0 : -1;
			if (sig_src) syssend(sig_src, &result, sizeof(result));
			break;
		}
		case AudioMsg::STREAM_BEGIN:
		{
			stdsint result = AudioStreamBegin(request, sig_src) ? 0 : -1;
			if (sig_src) syssend(sig_src, &result, sizeof(result));
			break;
		}
		case AudioMsg::STREAM_WRITE:
		{
			ProcessBlock* source_process = nullptr;
			if (auto* source_thread = Taskman::LocateThread(sig_src)) {
				source_process = source_thread->parent_process;
			}
			stdsint result = AudioStreamWrite(request, source_process, sig_src);
			if (sig_src) syssend(sig_src, &result, sizeof(result));
			break;
		}
		case AudioMsg::STREAM_DRAIN:
		{
			stdsint result = AudioStreamDrain();
			if (sig_src) syssend(sig_src, &result, sizeof(result));
			break;
		}
		case AudioMsg::STREAM_STOP:
		{
			AudioStreamAbort();
			stdsint result = 0;
			if (sig_src) syssend(sig_src, &result, sizeof(result));
			break;
		}
		case AudioMsg::STREAM_PAUSE:
		{
			stdsint result = -1;
			if (audio_stream.open && sig_src == audio_stream.owner_tid) {
				audio_stream.paused = true;
				if (audio_stream.started) {
					SoundBlasterPausePcmStream();
				}
				result = 0;
			}
			if (sig_src) syssend(sig_src, &result, sizeof(result));
			break;
		}
		case AudioMsg::STREAM_RESUME:
		{
			stdsint result = -1;
			if (audio_stream.open && sig_src == audio_stream.owner_tid) {
				audio_stream.paused = false;
				if (audio_stream.started) {
					SoundBlasterResumePcmStream();
				} else {
					AudioStreamStartIfReady(false);
				}
				result = 0;
			}
			if (sig_src) syssend(sig_src, &result, sizeof(result));
			break;
		}
		case AudioMsg::STREAM_GET_POS:
		{
			AudioStreamPosition pos{};
			pos.is_active = audio_stream.open;
			pos.is_paused = audio_stream.paused;
			if (audio_stream.open) {
				pos.played_bytes = audio_stream.played_bytes;
				const uint8 frame_size =
					(audio_stream.sample_format == uni::AudioSampleFormat::S16LE ? 2 : 1) *
					(audio_stream.channels ? audio_stream.channels : 1);
				if (frame_size) {
					pos.played_samples = uint32(pos.played_bytes / frame_size);
				}
				if (audio_stream.sample_rate) {
					pos.played_ms = uint32(((uint64)pos.played_samples * 1000) / audio_stream.sample_rate);
				}
			}
			if (sig_src) syssend(sig_src, &pos, sizeof(pos));
			break;
		}
		case AudioMsg::SET_VOLUME:
		{
			const auto* vol_req = reinterpret_cast<const AudioVolumeRequest*>(&request);
			stdsint result = -1;
			if (vol_req->mute) {
				result = SoundBlasterSetMute(vol_req->channel, true) ? 0 : -1;
			} else {
				result = SoundBlasterSetVolume(vol_req->channel, vol_req->left, vol_req->right) ? 0 : -1;
			}
			if (sig_src) syssend(sig_src, &result, sizeof(result));
			break;
		}
		case AudioMsg::GET_VOLUME:
		{
			const auto* in_req = reinterpret_cast<const AudioVolumeRequest*>(&request);
			AudioVolumeRequest resp = *in_req;
			if (!SoundBlasterGetVolume(in_req->channel, resp.left, resp.right)) {
				resp.left = resp.right = 0;
			}
			resp.mute = (resp.left == 0 && resp.right == 0);
			if (sig_src) syssend(sig_src, &resp, sizeof(resp));
			break;
		}
		default:
			plogwarn("[Audio] Unknown message type=%u src=%u",
				sig_type, sig_src);
			break;
		}
	}
}
#endif
