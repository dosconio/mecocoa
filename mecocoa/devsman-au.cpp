// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] Device Management - Audio
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../include/mecocoa.hpp"
#include "../devdriv/audio/au-sb16.hpp"
#include <c/format/audio/WAV.h>

#if _MCCA == 0x8632

namespace {
	constexpr uint32 AudioMaxTracks = 8;
	constexpr uint32 AudioStreamChunkBytes = 4096;
	constexpr uint32 AudioStreamRingBytes = AudioStreamChunkBytes * 16;
	constexpr uint32 AudioStreamDmaPrimeBytes = 4096 * 4;

	constexpr uint16 AudioMasterSampleRate = 44100;
	constexpr uni::AudioSampleFormat AudioMasterFormat = uni::AudioSampleFormat::U8;
	constexpr uint8 AudioMasterChannels = 2; // Stereo

	struct AudioTrackState {
		uint8 ring[AudioStreamRingBytes];
		uint32 read_pos;
		uint32 write_pos;
		uint32 used;
		stduint owner_tid;
		uint16 sample_rate;
		uni::AudioSampleFormat sample_format;
		uint8 channels;
		uint64 played_bytes;
		uint32 volume_percent; // 0..100
		bool muted;
		bool open;
		bool paused;
		bool is_temporary;

		// Resampling state
		uint32 phase; // 16.16 fixed point
		int32 curr_l;
		int32 curr_r;
		int32 next_l;
		int32 next_r;
		bool has_frames;
	};

	AudioTrackState audio_tracks[AudioMaxTracks]{};
	bool master_stream_started = false;
	stduint master_last_active_tick = 0;

	static inline int16 ClampS16(int32 val) {
		if (val > 32767) return 32767;
		if (val < -32768) return -32768;
		return (int16)val;
	}

	bool IsSupportedPcmFormat(const uni::AudioPlayRequest& request) {
		const bool valid_format =
			(request.format.sample_format == uni::AudioSampleFormat::U8 ||
			 request.format.sample_format == uni::AudioSampleFormat::S16LE);
		const bool valid_channels =
			(request.format.channels == 1 || request.format.channels == 2);
		return valid_format && valid_channels &&
			request.format.sample_rate >= 5000 &&
			request.format.sample_rate <= 48000;
	}

	bool IsSupportedPcmRequest(const uni::AudioPlayRequest& request) {
		return request.buffer.data &&
			request.buffer.byte_count &&
			IsSupportedPcmFormat(request);
	}

	void ResetTrackState(AudioTrackState& track) {
		track.read_pos = 0;
		track.write_pos = 0;
		track.used = 0;
		track.owner_tid = 0;
		track.sample_rate = 0;
		track.sample_format = uni::AudioSampleFormat::U8;
		track.channels = 1;
		track.played_bytes = 0;
		track.volume_percent = 100;
		track.muted = false;
		track.open = false;
		track.paused = false;
		track.is_temporary = false;
		track.phase = 0;
		track.curr_l = 0;
		track.curr_r = 0;
		track.next_l = 0;
		track.next_r = 0;
		track.has_frames = false;
	}

	AudioTrackState* FindTrackByOwner(stduint owner_tid) {
		if (!owner_tid) return nullptr;
		for (uint32 i = 0; i < AudioMaxTracks; ++i) {
			if (audio_tracks[i].open && audio_tracks[i].owner_tid == owner_tid) {
				return &audio_tracks[i];
			}
		}
		return nullptr;
	}

	AudioTrackState* AllocateTrack(stduint owner_tid, bool is_temporary = false) {
		AudioTrackState* existing = FindTrackByOwner(owner_tid);
		if (existing) {
			ResetTrackState(*existing);
			existing->owner_tid = owner_tid;
			existing->open = true;
			existing->is_temporary = is_temporary;
			return existing;
		}
		for (uint32 i = 0; i < AudioMaxTracks; ++i) {
			if (!audio_tracks[i].open) {
				ResetTrackState(audio_tracks[i]);
				audio_tracks[i].owner_tid = owner_tid;
				audio_tracks[i].open = true;
				audio_tracks[i].is_temporary = is_temporary;
				return &audio_tracks[i];
			}
		}
		return nullptr;
	}

	void FreeTrack(AudioTrackState& track) {
		ResetTrackState(track);
	}

	uint32 CountActiveTracks() {
		uint32 count = 0;
		for (uint32 i = 0; i < AudioMaxTracks; ++i) {
			if (audio_tracks[i].open && (audio_tracks[i].used > 0 || !audio_tracks[i].is_temporary)) {
				++count;
			}
		}
		return count;
	}

	uint32 TrackRingFree(const AudioTrackState& track) {
		return AudioStreamRingBytes - track.used;
	}

	bool FetchTrackFrame(AudioTrackState& track, int32& out_l, int32& out_r) {
		const uint32 bytes_per_frame = (track.sample_format == uni::AudioSampleFormat::S16LE ? 2 : 1) * track.channels;
		if (track.used < bytes_per_frame) {
			out_l = 0;
			out_r = 0;
			return false;
		}

		if (track.sample_format == uni::AudioSampleFormat::U8) {
			if (track.channels == 1) {
				uint8 s0 = track.ring[track.read_pos];
				out_l = ((int32)s0 - 128) << 8;
				out_r = out_l;
			} else {
				uint8 s0 = track.ring[track.read_pos];
				uint8 s1 = track.ring[(track.read_pos + 1) % AudioStreamRingBytes];
				out_l = ((int32)s0 - 128) << 8;
				out_r = ((int32)s1 - 128) << 8;
			}
		} else { // S16LE
			if (track.channels == 1) {
				uint8 b0 = track.ring[track.read_pos];
				uint8 b1 = track.ring[(track.read_pos + 1) % AudioStreamRingBytes];
				int16 s0 = (int16)(uint16(b0) | (uint16(b1) << 8));
				out_l = (int32)s0;
				out_r = out_l;
			} else {
				uint8 b0 = track.ring[track.read_pos];
				uint8 b1 = track.ring[(track.read_pos + 1) % AudioStreamRingBytes];
				uint8 b2 = track.ring[(track.read_pos + 2) % AudioStreamRingBytes];
				uint8 b3 = track.ring[(track.read_pos + 3) % AudioStreamRingBytes];
				int16 s0 = (int16)(uint16(b0) | (uint16(b1) << 8));
				int16 s1 = (int16)(uint16(b2) | (uint16(b3) << 8));
				out_l = (int32)s0;
				out_r = (int32)s1;
			}
		}

		track.read_pos = (track.read_pos + bytes_per_frame) % AudioStreamRingBytes;
		track.used -= bytes_per_frame;
		track.played_bytes += bytes_per_frame;
		return true;
	}

	bool GetNextTrackSample(AudioTrackState& track, int32& out_l, int32& out_r) {
		if (track.sample_rate == AudioMasterSampleRate) {
			if (!FetchTrackFrame(track, out_l, out_r)) return false;
		} else {
			const uint32 step = uint32(((uint64)track.sample_rate << 16) / AudioMasterSampleRate);
			if (!track.has_frames) {
				if (!FetchTrackFrame(track, track.curr_l, track.curr_r)) return false;
				if (!FetchTrackFrame(track, track.next_l, track.next_r)) {
					track.next_l = track.curr_l;
					track.next_r = track.curr_r;
				}
				track.has_frames = true;
				track.phase = 0;
			}

			const int32 frac = int32(track.phase & 0xFFFF);
			out_l = track.curr_l + (int32)(((int64)(track.next_l - track.curr_l) * frac) >> 16);
			out_r = track.curr_r + (int32)(((int64)(track.next_r - track.curr_r) * frac) >> 16);

			track.phase += step;
			while (track.phase >= 0x10000) {
				track.phase -= 0x10000;
				track.curr_l = track.next_l;
				track.curr_r = track.next_r;
				if (!FetchTrackFrame(track, track.next_l, track.next_r)) {
					track.has_frames = false;
					break;
				}
			}
		}

		if (track.muted || track.volume_percent == 0) {
			out_l = 0;
			out_r = 0;
		} else if (track.volume_percent < 100) {
			out_l = (out_l * (int32)track.volume_percent) / 100;
			out_r = (out_r * (int32)track.volume_percent) / 100;
		}
		return true;
	}

	uint32 AudioMasterMixerRefill(void*, uint8* destination, uint32 byte_count) {
		if (!destination || !byte_count) return 0;
		const uint32 frame_count = byte_count / 2; // 8-bit stereo = 2 bytes per frame

		for (uint32 f = 0; f < frame_count; ++f) {
			int32 sum_l = 0;
			int32 sum_r = 0;

			for (uint32 t = 0; t < AudioMaxTracks; ++t) {
				AudioTrackState& track = audio_tracks[t];
				if (!track.open || track.paused) continue;

				int32 l = 0, r = 0;
				if (GetNextTrackSample(track, l, r)) {
					sum_l += l;
					sum_r += r;
				} else if (track.is_temporary && track.used == 0) {
					FreeTrack(track);
				}
			}

			const int16 final_l = ClampS16(sum_l);
			const int16 final_r = ClampS16(sum_r);

			// Convert signed 16-bit to unsigned 8-bit PCM [0..255]
			destination[f * 2 + 0] = uint8((final_l >> 8) + 128);
			destination[f * 2 + 1] = uint8((final_r >> 8) + 128);
		}

		return byte_count;
	}

	bool AudioMasterEnsureRunning() {
		if (master_stream_started) return true;
		const auto* backend = Devsman::GetActiveAudioBackend();
		if (!backend || !backend->start_stream) {
			plogwarn("[Audio] No active audio backend available");
			return false;
		}
		if (!backend->start_stream(
			AudioMasterSampleRate,
			AudioMasterFormat,
			AudioMasterChannels,
			AudioMasterMixerRefill, nullptr)) {
			plogwarn("[Audio] Failed to start master audio mixer stream on backend %s",
				backend->name ? backend->name : "unknown");
			return false;
		}
		master_stream_started = true;
		master_last_active_tick = tick;
		ploginfo("[Audio] Master mixer started 44.1kHz U8 Stereo on %s",
			backend->name ? backend->name : "unknown");
		return true;
	}

	bool IsTrackOwnerAlive(stduint owner_tid) {
		if (!owner_tid) return true;
		auto* owner_process = ProcessBlock::AcquireActive(owner_tid);
		if (!owner_process) return false;
		ProcessBlock::Release(owner_process);
		return true;
	}

	void AudioServicePollStream() {
		uint32 active_tracks = 0;
		for (uint32 i = 0; i < AudioMaxTracks; ++i) {
			if (audio_tracks[i].open && !audio_tracks[i].is_temporary) {
				if (!IsTrackOwnerAlive(audio_tracks[i].owner_tid)) {
					plogwarn("[Audio] Track %u owner gone tid=%u, free track",
						(stduint)i, (stduint)audio_tracks[i].owner_tid);
					FreeTrack(audio_tracks[i]);
				} else if (!audio_tracks[i].paused && audio_tracks[i].used > 0) {
					++active_tracks;
				}
			}
		}

		if (master_stream_started) {
			const auto* backend = Devsman::GetActiveAudioBackend();
			if (active_tracks > 0) {
				master_last_active_tick = tick;
				if (backend && backend->watchdog_check && !backend->watchdog_check()) {
					plogwarn("[Audio] Watchdog: device recovery failed");
				}
			} else if (CountActiveTracks() == 0) {
				if (tick - master_last_active_tick > (CONFIG_SysTickFreq / 4)) {
					// Stop hardware DMA stream when completely idle to save CPU/DMA cycles and prevent loop residue
					if (backend && backend->stop_stream) backend->stop_stream();
					master_stream_started = false;
					ploginfo("[Audio] Master mixer entered idle sleep");
				}
			}
			if (backend && backend->service_playback) backend->service_playback();
		}
	}

	void AudioServiceIdleWait() {
		syscall(syscall_t::REST, 1, 1);
	}

	bool AudioServiceHasMessage() {
		return syscall(syscall_t::TMSG) != 0;
	}

	bool AudioStreamBegin(const uni::AudioPlayRequest& request, stduint owner_tid) {
		if (!IsSupportedPcmFormat(request)) return false;
		AudioTrackState* track = AllocateTrack(owner_tid, false);
		if (!track) return false;

		track->sample_rate = uint16(request.format.sample_rate);
		track->sample_format = request.format.sample_format;
		track->channels = request.format.channels ? request.format.channels : 1;
		track->volume_percent = 100;
		track->muted = false;
		track->open = true;
		track->paused = false;

		return AudioMasterEnsureRunning();
	}

	stduint CopyPcmToTrackRing(AudioTrackState& track, const void* source,
		ProcessBlock* source_process, uint32 byte_count) {
		if (!source || !byte_count) return 0;
		uint32 copied_total = 0;
		while (copied_total < byte_count && TrackRingFree(track)) {
			uint32 chunk = byte_count - copied_total;
			const uint32 free_bytes = TrackRingFree(track);
			const uint32 until_end = AudioStreamRingBytes - track.write_pos;
			if (chunk > free_bytes) chunk = free_bytes;
			if (chunk > until_end) chunk = until_end;

			const void* source_at = reinterpret_cast<const void*>(
				(stduint)source + copied_total);
			if (source_process && source_process->ring != RING_M) {
				const stduint copied = MccaMemCopyP(
					track.ring + track.write_pos,
					nullptr, true,
					source_at, source_process, false,
					chunk);
				if (copied != chunk) return copied_total + copied;
			}
			else {
				MemCopyN(track.ring + track.write_pos, source_at, chunk);
			}
			track.write_pos = (track.write_pos + chunk) % AudioStreamRingBytes;
			track.used += chunk;
			copied_total += chunk;
		}
		return copied_total;
	}

	stdsint AudioStreamWrite(const uni::AudioPlayRequest& request,
		ProcessBlock* source_process, stduint source_tid) {
		AudioTrackState* track = FindTrackByOwner(source_tid);
		if (!track || !track->open || !IsSupportedPcmRequest(request) ||
			uint16(request.format.sample_rate) != track->sample_rate ||
			request.format.sample_format != track->sample_format ||
			request.format.channels != track->channels) {
			return -1;
		}
		AudioServicePollStream();
		if (!track->open) return -1;
		const stduint copied = CopyPcmToTrackRing(
			*track, request.buffer.data, source_process, request.buffer.byte_count);
		if (!master_stream_started && track->used >= AudioStreamDmaPrimeBytes) {
			AudioMasterEnsureRunning();
		}
		return stdsint(copied);
	}

	stdsint AudioStreamDrain(stduint source_tid) {
		AudioTrackState* track = FindTrackByOwner(source_tid);
		if (!track || !track->open) return 0;
		AudioMasterEnsureRunning();

		const uint8 frame_size = (track->sample_format == uni::AudioSampleFormat::S16LE ? 2 : 1) * track->channels;
		const stduint bytes_per_sec = (stduint)track->sample_rate * (frame_size ? frame_size : 1);

		const stduint drain_timeout =
			((stduint)(track->used + AudioStreamDmaPrimeBytes) *
				CONFIG_SysTickFreq + bytes_per_sec - 1) /
			(bytes_per_sec ? bytes_per_sec : 1) + 2 * CONFIG_SysTickFreq;
		const stduint drain_start = tick;
		while (track->open && track->used &&
			tick - drain_start < drain_timeout) {
			AudioServicePollStream();
			AudioServiceIdleWait();
		}
		FreeTrack(*track);
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

		// If master mixer is active, mix as temporary track concurrently
		if (master_stream_started || CountActiveTracks() > 0) {
			static stduint s_temp_counter = 0x80000000;
			AudioTrackState* temp_track = AllocateTrack(++s_temp_counter, true);
			if (temp_track) {
				temp_track->sample_rate = uint16(request.format.sample_rate);
				temp_track->sample_format = request.format.sample_format;
				temp_track->channels = request.format.channels ? request.format.channels : 1;
				temp_track->volume_percent = 100;
				temp_track->muted = false;
				temp_track->open = true;
				temp_track->paused = false;

				CopyPcmToTrackRing(*temp_track, pcm_data, nullptr, request.buffer.byte_count);
				AudioMasterEnsureRunning();
				delete[] copied_pcm;
				return true;
			}
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
		if ((master_stream_started || CountActiveTracks() > 0) && !AudioServiceHasMessage()) {
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
			stdsint result = AudioStreamDrain(sig_src);
			if (sig_src) syssend(sig_src, &result, sizeof(result));
			break;
		}
		case AudioMsg::STREAM_STOP:
		{
			AudioTrackState* track = FindTrackByOwner(sig_src);
			if (track) FreeTrack(*track);
			if (CountActiveTracks() == 0 && master_stream_started) {
				const auto* backend = Devsman::GetActiveAudioBackend();
				if (backend && backend->stop_stream) backend->stop_stream();
				master_stream_started = false;
			}
			stdsint result = 0;
			if (sig_src) syssend(sig_src, &result, sizeof(result));
			break;
		}
		case AudioMsg::STREAM_PAUSE:
		{
			AudioTrackState* track = FindTrackByOwner(sig_src);
			stdsint result = -1;
			if (track && track->open) {
				track->paused = true;
				result = 0;
			}
			if (sig_src) syssend(sig_src, &result, sizeof(result));
			break;
		}
		case AudioMsg::STREAM_RESUME:
		{
			AudioTrackState* track = FindTrackByOwner(sig_src);
			stdsint result = -1;
			if (track && track->open) {
				track->paused = false;
				AudioMasterEnsureRunning();
				result = 0;
			}
			if (sig_src) syssend(sig_src, &result, sizeof(result));
			break;
		}
		case AudioMsg::STREAM_GET_POS:
		{
			AudioTrackState* track = FindTrackByOwner(sig_src);
			AudioStreamPosition pos{};
			if (track && track->open) {
				pos.is_active = true;
				pos.is_paused = track->paused;
				pos.played_bytes = track->played_bytes;
				const uint8 frame_size =
					(track->sample_format == uni::AudioSampleFormat::S16LE ? 2 : 1) *
					(track->channels ? track->channels : 1);
				if (frame_size) {
					pos.played_samples = uint32(pos.played_bytes / frame_size);
				}
				if (track->sample_rate) {
					pos.played_ms = uint32(((uint64)pos.played_samples * 1000) / track->sample_rate);
				}
			}
			if (sig_src) syssend(sig_src, &pos, sizeof(pos));
			break;
		}
		case AudioMsg::SET_VOLUME:
		{
			const auto* vol_req = reinterpret_cast<const AudioVolumeRequest*>(&request);
			const auto* backend = Devsman::GetActiveAudioBackend();
			stdsint result = -1;
			if (backend) {
				if (vol_req->mute) {
					result = (backend->set_mute && backend->set_mute(vol_req->channel, true)) ? 0 : -1;
				} else {
					result = (backend->set_volume && backend->set_volume(vol_req->channel, vol_req->left, vol_req->right)) ? 0 : -1;
				}
			}
			if (sig_src) syssend(sig_src, &result, sizeof(result));
			break;
		}
		case AudioMsg::GET_VOLUME:
		{
			const auto* in_req = reinterpret_cast<const AudioVolumeRequest*>(&request);
			const auto* backend = Devsman::GetActiveAudioBackend();
			AudioVolumeRequest resp = *in_req;
			if (!backend || !backend->get_volume || !backend->get_volume(in_req->channel, resp.left, resp.right)) {
				resp.left = resp.right = 0;
			}
			resp.mute = (resp.left == 0 && resp.right == 0);
			if (sig_src) syssend(sig_src, &resp, sizeof(resp));
			break;
		}
		case AudioMsg::STREAM_SEEK:
		{
			const auto* seek_req = reinterpret_cast<const AudioSeekRequest*>(&request);
			AudioTrackState* track = FindTrackByOwner(sig_src);
			stdsint result = -1;
			if (track && track->open) {
				track->read_pos = 0;
				track->write_pos = 0;
				track->used = 0;
				track->has_frames = false;
				track->phase = 0;
				const uint8 frame_size =
					(track->sample_format == uni::AudioSampleFormat::S16LE ? 2 : 1) *
					(track->channels ? track->channels : 1);
				track->played_bytes = seek_req->target_samples * frame_size;
				result = 0;
			}
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
#endif
