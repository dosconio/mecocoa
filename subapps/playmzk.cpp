#include "aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <stdio.h>
#include <stdlib.h>
#include <c/format/audio/WAV.h>
#include <cpp/trait/StorageTrait.hpp>
#include "../include/devsman.hpp"

using namespace uni;

static constexpr uint32 kPlayChunkBytes = 4096;

// Static buffer allocated in BSS section to avoid stack consumption
static byte s_play_chunk_buffer[kPlayChunkBytes];

#define outsfmt(...) printf(__VA_ARGS__)
#if __BITS__ > 32
#define Task_Audio_Serv 0
#endif

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

static bool PlayAudioStream(IAudioStream* stream, const AudioInfo& info) {
	if (!stream) return false;

	AudioPlayRequest begin_request = {};
	begin_request.format = info.format;
	if (SendAudioRequest(AudioMsg::STREAM_BEGIN, begin_request) != 0) {
		return false;
	}

	uint32 chunk_index = 0;
	while (true) {
		uint32 bytes_read = 0;
		AudioResult res = stream->ReadSamples(
			s_play_chunk_buffer, sizeof(s_play_chunk_buffer), bytes_read);
		if (res == AudioResult::EndOfStream || (res == AudioResult::OK && bytes_read == 0)) {
			break;
		}
		if (res != AudioResult::OK) {
			SendAudioRequest(AudioMsg::STREAM_STOP, begin_request);
			return false;
		}

		uint32 offset = 0;
		while (offset < bytes_read) {
			AudioPlayRequest request = {};
			request.format = info.format;
			request.buffer.data = s_play_chunk_buffer + offset;
			request.buffer.byte_count = bytes_read - offset;

			const stdsint accepted =
				SendAudioRequest(AudioMsg::STREAM_WRITE, request);
			if (accepted < 0 ||
				(uint32)accepted > request.buffer.byte_count) {
				SendAudioRequest(AudioMsg::STREAM_STOP, begin_request);
				return false;
			}
			if (accepted == 0) {
				sysrest(1, 1);
				continue;
			}

			offset += (uint32)accepted;
		}
		++chunk_index;
	}

	const bool drained =
		SendAudioRequest(AudioMsg::STREAM_DRAIN, begin_request) == 0;
	if (!drained) {
		SendAudioRequest(AudioMsg::STREAM_STOP, begin_request);
	}
	return drained;
}

int main(int argc, char** argv)
{
	#if __BITS__ == 64
	return -1;
	#endif

	if (argc < 2 || !argv[1]) {
		outsfmt("Usage: playmzk <file.wav>\n\r");
		outsfmt("Supports 8/16-bit mono/stereo PCM WAV.\n\r");
		return -1;
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
