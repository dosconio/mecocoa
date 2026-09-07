// UTF-8 g++ TAB4 LF
// ModuTitle: Music Player Application (playmzk)
// Description: Streams uncompressed PCM WAV files to Audio System Service (Task_Audio_Serv).
//
// Playback Control Architecture & IPC Usage:
// - All audio output is managed by the kernel Audio Service (Task_Audio_Serv).
// - Streaming session lifecycle:
//     1. AudioMsg::STREAM_BEGIN   : Initializes a stream session with specified format & rate.
//     2. AudioMsg::STREAM_WRITE   : Pushes PCM chunks non-blockingly into the service ring buffer.
//     3. AudioMsg::STREAM_PAUSE   : Halts SB16 DMA playback without dropping queued ring buffer data.
//     4. AudioMsg::STREAM_RESUME  : Resumes SB16 DMA playback from the paused position.
//     5. AudioMsg::STREAM_GET_POS : Queries played byte/sample/ms count & paused state via AudioStreamPosition.
//     6. AudioMsg::STREAM_DRAIN   : Blocks until all buffered PCM frames finish playing.
//     7. AudioMsg::STREAM_STOP    : Immediately aborts playback and resets DMA/DSP.

#include "aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <stdio.h>
#include <stdlib.h>
#include <c/format/audio/WAV.h>
#include <cpp/trait/StorageTrait.hpp>
#include "../include/devsman.hpp"
#include "../include/syscall.hpp"

using namespace uni;

static constexpr uint32 kPlayChunkBytes = 4096;

// Static buffers allocated in BSS section to avoid stack consumption
static byte s_play_raw_buffer[kPlayChunkBytes];
static byte s_play_chunk_buffer[kPlayChunkBytes];

static void ApplySoftwareVolume(void* pcm_data, uint32 byte_count, AudioSampleFormat format, uint32 vol_percent) {
	if (!pcm_data || !byte_count || vol_percent >= 100) return;
	if (vol_percent == 0) {
		if (format == AudioSampleFormat::U8) {
			MemSet(pcm_data, 0x80, byte_count);
		} else {
			MemSet(pcm_data, 0x00, byte_count);
		}
		return;
	}

	if (format == AudioSampleFormat::U8) {
		uint8* samples = static_cast<uint8*>(pcm_data);
		for (uint32 i = 0; i < byte_count; ++i) {
			int sample = (int)samples[i] - 128;
			sample = (sample * (int)vol_percent) / 100;
			samples[i] = (uint8)(sample + 128);
		}
	} else if (format == AudioSampleFormat::S16LE) {
		int16* samples = static_cast<int16*>(pcm_data);
		const uint32 count = byte_count / 2;
		for (uint32 i = 0; i < count; ++i) {
			samples[i] = (int16)(((int32)samples[i] * (int32)vol_percent) / 100);
		}
	}
}

#define outsfmt(...) printf(__VA_ARGS__)
#if __BITS__ > 32
#define Task_Audio_Serv 0
#endif

static void PrintUsage(const char* prog_name) {
	outsfmt("Mecocoa PCM Music Player\n\r\n\r");
	outsfmt("Usage: %s [options] <file.wav>\n\r\n\r", prog_name ? prog_name : "playmzk");
	outsfmt("Interactive Controls (during playback):\n\r");
	outsfmt("  [Space] / [P]   : Pause or Resume playback\n\r");
	outsfmt("  [+]     / [-]   : Increase / Decrease Volume (+/- 5%%)\n\r");
	outsfmt("  [M]             : Mute or Unmute audio\n\r");
	outsfmt("  [Q]     / [Esc] : Stop playback and Exit\n\r\n\r");
	outsfmt("Options:\n\r");
	outsfmt("  -h, --help      : Display this help message\n\r\n\r");
	outsfmt("Supported Formats:\n\r");
	outsfmt("  8/16-bit mono or stereo uncompressed PCM WAV (5000 - 45000 Hz)\n\r");
}

static int TryGetKeyboardChar() {
	stduint ret = syscall(syscall_t::INNC, 0, 0, 0);
	if (ret == (stduint)-1 || ret == 0) return -1;
	return (int)(ret & 0xFF);
}

static stdsint SendAudioRequest(AudioMsg type, const AudioPlayRequest& request) {
	CommMsg send_msg = {};
	send_msg.data.address = (stduint)&request;
	send_msg.data.length = sizeof(request);
	send_msg.type = (stduint)type;
	syscomm(1, Task_Audio_Serv, &send_msg);

	stdsint result = -1;
	CommMsg recv_msg = {};
	recv_msg.data.address = (stduint)&result;
	recv_msg.data.length = sizeof(result);
	syscomm(0, Task_Audio_Serv, &recv_msg);
	return result;
}

static bool SendAudioPause() {
	AudioPlayRequest req = {};
	return SendAudioRequest(AudioMsg::STREAM_PAUSE, req) == 0;
}

static bool SendAudioResume() {
	AudioPlayRequest req = {};
	return SendAudioRequest(AudioMsg::STREAM_RESUME, req) == 0;
}

static bool SendAudioGetPos(AudioStreamPosition& pos) {
	CommMsg send_msg = {};
	send_msg.type = (stduint)AudioMsg::STREAM_GET_POS;
	syscomm(1, Task_Audio_Serv, &send_msg);

	CommMsg recv_msg = {};
	recv_msg.data.address = (stduint)&pos;
	recv_msg.data.length = sizeof(pos);
	syscomm(0, Task_Audio_Serv, &recv_msg);
	return pos.is_active;
}

static bool SendAudioSetVolume(SoundBlasterMixerChannel channel, uint8 left, uint8 right, bool mute = false) {
	AudioVolumeRequest req{};
	req.channel = channel;
	req.left = left;
	req.right = right;
	req.mute = mute;
	CommMsg send_msg = {};
	send_msg.data.address = (stduint)&req;
	send_msg.data.length = sizeof(req);
	send_msg.type = (stduint)AudioMsg::SET_VOLUME;
	syscomm(1, Task_Audio_Serv, &send_msg);

	stdsint result = -1;
	CommMsg recv_msg = {};
	recv_msg.data.address = (stduint)&result;
	recv_msg.data.length = sizeof(result);
	syscomm(0, Task_Audio_Serv, &recv_msg);
	return result == 0;
}

static bool SendAudioGetVolume(SoundBlasterMixerChannel channel, AudioVolumeRequest& req) {
	req.channel = channel;
	CommMsg send_msg = {};
	send_msg.data.address = (stduint)&req;
	send_msg.data.length = sizeof(req);
	send_msg.type = (stduint)AudioMsg::GET_VOLUME;
	syscomm(1, Task_Audio_Serv, &send_msg);

	CommMsg recv_msg = {};
	recv_msg.data.address = (stduint)&req;
	recv_msg.data.length = sizeof(req);
	syscomm(0, Task_Audio_Serv, &recv_msg);
	return true;
}

static uint32 GetCurrentTimeMs() {
	return (uint32)syscall(syscall_t::TIME, 1, 0, 0);
}

static bool PlayAudioStream(IAudioStream* stream, const AudioInfo& info) {
	if (!stream) return false;

	AudioPlayRequest begin_request = {};
	begin_request.format = info.format;
	if (SendAudioRequest(AudioMsg::STREAM_BEGIN, begin_request) != 0) {
		return false;
	}

	const uint32 total_duration_sec = info.durationMs / 1000;
	uint32 last_printed_sec = (uint32)-1;
	bool is_paused = false;
	bool last_paused_state = false;
	bool is_muted = false;
	uint32 volume_percent = 80;
	uint32 last_vol_percent = (uint32)-1;
	bool last_mute_state = false;
	uint32 last_pause_toggle_ms = 0;
	uint32 chunk_index = 0;

	AudioVolumeRequest init_vol{};
	if (SendAudioGetVolume(SoundBlasterMixerChannel::MasterVolume, init_vol)) {
		if (init_vol.left > 0) {
			volume_percent = ((uint32)init_vol.left * 100 + 127) / 255;
			if (volume_percent > 100) volume_percent = 100;
		}
		is_muted = init_vol.mute;
	}

	outsfmt("Controls: [Space] Pause/Resume, [+/-] Volume, [M] Mute, [Q] Quit\n\r");

	uint32 chunk_bytes_read = 0;
	uint32 chunk_offset = 0;

	while (true) {
		// Non-blocking keyboard check
		int key = TryGetKeyboardChar();
		while (key > 0) {
			if (key == ' ' || key == 'p' || key == 'P') {
				const uint32 now_ms = GetCurrentTimeMs();
				if (now_ms - last_pause_toggle_ms >= 250) {
					last_pause_toggle_ms = now_ms;
					if (is_paused) {
						if (SendAudioResume()) {
							is_paused = false;
							last_printed_sec = (uint32)-1;
							const uint8 raw_vol = (uint8)(((uint32)volume_percent * 255) / 100);
							SendAudioSetVolume(SoundBlasterMixerChannel::MasterVolume, raw_vol, raw_vol, is_muted);
						}
					} else {
						if (SendAudioPause()) {
							is_paused = true;
							last_printed_sec = (uint32)-1;
						}
					}
				}
			} else if (key == '+' || key == '=') {
				if (volume_percent < 100) {
					volume_percent = (volume_percent + 5 <= 100) ? volume_percent + 5 : 100;
				}
				if (is_muted) is_muted = false;
				if (chunk_bytes_read > chunk_offset) {
					MemCopyN(s_play_chunk_buffer + chunk_offset,
						s_play_raw_buffer + chunk_offset,
						chunk_bytes_read - chunk_offset);
					ApplySoftwareVolume(s_play_chunk_buffer + chunk_offset,
						chunk_bytes_read - chunk_offset,
						info.format.sample_format, volume_percent);
				}
				const uint8 raw_vol = (uint8)(((uint32)volume_percent * 255) / 100);
				SendAudioSetVolume(SoundBlasterMixerChannel::MasterVolume, raw_vol, raw_vol, false);
				last_printed_sec = (uint32)-1;
			} else if (key == '-' || key == '_') {
				if (volume_percent >= 5) {
					volume_percent -= 5;
				} else {
					volume_percent = 0;
				}
				if (chunk_bytes_read > chunk_offset) {
					MemCopyN(s_play_chunk_buffer + chunk_offset,
						s_play_raw_buffer + chunk_offset,
						chunk_bytes_read - chunk_offset);
					ApplySoftwareVolume(s_play_chunk_buffer + chunk_offset,
						chunk_bytes_read - chunk_offset,
						info.format.sample_format, volume_percent);
				}
				const uint8 raw_vol = (uint8)(((uint32)volume_percent * 255) / 100);
				SendAudioSetVolume(SoundBlasterMixerChannel::MasterVolume, raw_vol, raw_vol, is_muted);
				last_printed_sec = (uint32)-1;
			} else if (key == 'm' || key == 'M') {
				is_muted = !is_muted;
				const uint8 raw_vol = (uint8)(((uint32)volume_percent * 255) / 100);
				SendAudioSetVolume(SoundBlasterMixerChannel::MasterVolume, raw_vol, raw_vol, is_muted);
				last_printed_sec = (uint32)-1;
			} else if (key == 'q' || key == 'Q' || key == 27 || key == 3) {
				SendAudioRequest(AudioMsg::STREAM_STOP, begin_request);
				outsfmt("\n\r[Stopped by user]\n\r");
				return true;
			}
			key = TryGetKeyboardChar();
		}

		if (is_paused) {
			AudioStreamPosition pos{};
			if (SendAudioGetPos(pos)) {
				uint32 cur_sec = pos.played_ms / 1000;
				if (cur_sec != last_printed_sec || is_paused != last_paused_state ||
					volume_percent != last_vol_percent || is_muted != last_mute_state) {
					auto str = "\r[⏸︎ Paused %3u%%] %02u:%02u / %02u:%02u (Space: Resume, +/-: Vol, M: Mute, Q: Quit) "_ustr;
					outsfmt(str.reference(),
						volume_percent,
						cur_sec / 60, cur_sec % 60,
						total_duration_sec / 60, total_duration_sec % 60);
					last_printed_sec = cur_sec;
					last_paused_state = is_paused;
					last_vol_percent = volume_percent;
					last_mute_state = is_muted;
				}
			}
			sysrest(1, 50);
			continue;
		}

		last_paused_state = is_paused;

		// Read next chunk from audio stream when current chunk is exhausted
		if (chunk_offset >= chunk_bytes_read) {
			chunk_offset = 0;
			chunk_bytes_read = 0;
			AudioResult res = stream->ReadSamples(
				s_play_raw_buffer, sizeof(s_play_raw_buffer), chunk_bytes_read);
			if (res == AudioResult::EndOfStream || (res == AudioResult::OK && chunk_bytes_read == 0)) {
				break;
			}
			if (res != AudioResult::OK) {
				SendAudioRequest(AudioMsg::STREAM_STOP, begin_request);
				return false;
			}
			MemCopyN(s_play_chunk_buffer, s_play_raw_buffer, chunk_bytes_read);
			ApplySoftwareVolume(s_play_chunk_buffer, chunk_bytes_read,
				info.format.sample_format, volume_percent);
		}

		// Push samples to audio service ring buffer
		AudioPlayRequest request = {};
		request.format = info.format;
		request.buffer.data = s_play_chunk_buffer + chunk_offset;
		request.buffer.byte_count = chunk_bytes_read - chunk_offset;

		const stdsint accepted =
			SendAudioRequest(AudioMsg::STREAM_WRITE, request);
		if (accepted < 0 ||
			(uint32)accepted > request.buffer.byte_count) {
			SendAudioRequest(AudioMsg::STREAM_STOP, begin_request);
			return false;
		}
		if (accepted == 0) {
			// Service ring buffer full; yield CPU briefly
			sysrest(1, 10);
		} else {
			chunk_offset += (uint32)accepted;
		}

		AudioStreamPosition pos{};
		if (SendAudioGetPos(pos)) {
			uint32 cur_sec = pos.played_ms / 1000;
			if (cur_sec != last_printed_sec || volume_percent != last_vol_percent || is_muted != last_mute_state) {
				if (is_muted) {
					auto str = "\r[▶︎ Muted    ] %02u:%02u / %02u:%02u (Space: Pause, +/-: Vol, M: Mute, Q: Quit) "_ustr;
					outsfmt(str.reference(),
						cur_sec / 60, cur_sec % 60,
						total_duration_sec / 60, total_duration_sec % 60);
				} else {
					auto str = "\r[▶︎ %3u%% Vol ] %02u:%02u / %02u:%02u (Space: Pause, +/-: Vol, M: Mute, Q: Quit) "_ustr;
					outsfmt(str.reference(),
						volume_percent,
						cur_sec / 60, cur_sec % 60,
						total_duration_sec / 60, total_duration_sec % 60);
				}
				last_printed_sec = cur_sec;
				last_vol_percent = volume_percent;
				last_mute_state = is_muted;
			}
		}

		++chunk_index;
	}

	const bool drained =
		SendAudioRequest(AudioMsg::STREAM_DRAIN, begin_request) == 0;
	if (!drained) {
		SendAudioRequest(AudioMsg::STREAM_STOP, begin_request);
	}
	outsfmt("\r[Finished ] %02u:%02u / %02u:%02u                                \n\r",
		total_duration_sec / 60, total_duration_sec % 60,
		total_duration_sec / 60, total_duration_sec % 60);
	return drained;
}

int main(int argc, char** argv)
{
	#if __BITS__ == 64
	return -1;
	#endif

	if (argc < 2 || !argv[1] ||
		StrCompare(argv[1], "-h") == 0 ||
		StrCompare(argv[1], "--help") == 0) {
		PrintUsage(argv[0]);
		return (argc < 2 || !argv[1]) ? -1 : 0;
	}

	FILE* fp = fopen(argv[1], "rb");
	if (!fp) {
		outsfmt("playmzk: failed to open '%s'\n\r", argv[1]);
		return -1;
	}

	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return -1;
	}
	long file_size = ftell(fp);
	if (file_size <= 0) {
		outsfmt("playmzk: bad file size '%s'\n\r", argv[1]);
		fclose(fp);
		return -1;
	}
	fseek(fp, 0, SEEK_SET);

	FileBlockDevice storage(fp, (stduint)file_size);
	StdMalloc my_malloc;
	WAVCodec wav_codec;

	IAudioStream* stream = nullptr;
	AudioResult res = wav_codec.OpenStream(storage, stream, my_malloc);
	if (res != AudioResult::OK || !stream) {
		outsfmt("playmzk: invalid or unsupported wav file\n\r");
		fclose(fp);
		return -1;
	}

	AudioInfo info{};
	stream->GetInfo(info);

	outsfmt("playmzk: format=%u ch=%u rate=%u bits=%u bytes=%u\n\r",
		(stduint)info.format.sample_format,
		(stduint)info.format.channels,
		(stduint)info.format.sample_rate,
		(stduint)info.bitsPerSample,
		(stduint)info.dataByteLength);

	if ((info.bitsPerSample != 8 && info.bitsPerSample != 16) ||
		(info.format.channels != 1 && info.format.channels != 2)) {
		outsfmt("playmzk: only 8/16-bit mono/stereo wav is supported\n\r");
		stream->Release();
		fclose(fp);
		return -1;
	}

	// Automatic resampling for high-rate (>44.1kHz like 48kHz/96kHz) or low-rate (<5kHz) audio
	if (info.format.sample_rate > 44100 || info.format.sample_rate < 5000) {
		const uint32 target_rate = (info.format.sample_rate > 44100) ? 44100 : 11025;
		void* resampler_mem = my_malloc.allocate(sizeof(ResamplerStream));
		if (resampler_mem) {
			ResamplerStream* rstream =
				new (resampler_mem) ResamplerStream(stream, target_rate, my_malloc);
			if (rstream->IsValid()) {
				outsfmt("playmzk: resampled %uHz -> %uHz\n\r",
					(stduint)info.format.sample_rate, (stduint)target_rate);
				stream = rstream;
				stream->GetInfo(info);
			} else {
				rstream->Release();
				stream = nullptr;
				outsfmt("playmzk: failed to initialize resampler\n\r");
				fclose(fp);
				return -1;
			}
		}
	}

	const bool ok = PlayAudioStream(stream, info);
	stream->Release();
	fclose(fp);

	if (!ok) {
		outsfmt("playmzk: playback failed\n\r");
		return -1;
	}

	outsfmt("playmzk: playback ok\n\r");
	return 0;
}
