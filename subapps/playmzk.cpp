#include "aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <c/format/audio/WAV.h>
#include "../include/devsman.hpp"

using namespace uni;

static constexpr uint32 kPlayChunkBytes = 4096;

static bool ReadWholeFile(const char* path, byte*& out_data, stduint& out_size) {
	out_data = nullptr;
	out_size = 0;
	if (!path) return false;

	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		outsfmt("playmzk: failed to open '%s'\n\r", path);
		return false;
	}

	struct stat st = {};
	if (fstat(fd, &st) != 0 || st.st_size <= 0) {
		outsfmt("playmzk: bad file size '%s'\n\r", path);
		close(fd);
		return false;
	}

	byte* file_data = (byte*)malloc((size_t)st.st_size);
	if (!file_data) {
		outsfmt("playmzk: out of memory (%u bytes)\n\r", (stduint)st.st_size);
		close(fd);
		return false;
	}

	stdsint read_bytes = read(fd, file_data, st.st_size);
	close(fd);
	if (read_bytes != st.st_size) {
		outsfmt("playmzk: read failed %d/%d\n\r", (int)read_bytes, (int)st.st_size);
		free(file_data);
		return false;
	}

	out_data = file_data;
	out_size = (stduint)st.st_size;
	return true;
}

#define outsfmt(...)

static bool PlayU8Mono(const WAVPCMVIEW& wav) {
	const byte* pcm = (const byte*)wav.pcm_data;
	uint32 remain = wav.pcm_size;
	uint32 chunk_index = 0;
	while (remain) {
		AudioPlayRequest request = {};
		request.format.sample_format = AudioSampleFormat::U8;
		request.format.channels = wav.channel_count;
		request.format.sample_rate = wav.sample_rate;
		request.buffer.data = pcm;
		request.buffer.byte_count = remain > kPlayChunkBytes ?
			kPlayChunkBytes : remain;
		outsfmt("playmzk: chunk=%u send bytes=%u remain=%u\n\r",
			(stduint)chunk_index,
			(stduint)request.buffer.byte_count,
			(stduint)remain);

		CommMsg send_msg = {};
		send_msg.data.address = (stduint)&request;
		send_msg.data.length = sizeof(request);
		send_msg.type = (stduint)AudioMsg::PLAY_PCM_U8_MONO;
		syscomm(1, Task_Audio_Serv, &send_msg);
		outsfmt("playmzk: chunk=%u sent\n\r", (stduint)chunk_index);

		stdsint result = -1;
		CommMsg recv_msg = {};
		recv_msg.data.address = (stduint)&result;
		recv_msg.data.length = sizeof(result);
		outsfmt("playmzk: chunk=%u waiting reply\n\r", (stduint)chunk_index);
		syscomm(0, Task_Audio_Serv, &recv_msg);
		outsfmt("playmzk: chunk=%u reply=%d\n\r", (stduint)chunk_index, (int)result);
		if (result != 0) return false;

		pcm += request.buffer.byte_count;
		remain -= request.buffer.byte_count;
		++chunk_index;
	}
	return true;
}

int main(int argc, char** argv)
{
	#if __BITS__ == 64
	_preprocess();
	#endif

	if (argc < 2 || !argv[1]) {
		outsfmt("Usage: playmzk <file.wav>\n\r");
		outsfmt("Only PCM U8 mono WAV is supported for now.\n\r");
		return -1;
	}

	byte* wav_file = nullptr;
	stduint wav_size = 0;
	if (!ReadWholeFile(argv[1], wav_file, wav_size)) {
		return -1;
	}

	WAVPCMVIEW wav = {};
	if (!WAV_ParsePCM(wav_file, wav_size, &wav)) {
		outsfmt("playmzk: invalid wav file\n\r");
		free(wav_file);
		return -1;
	}

	outsfmt("playmzk: format=%u ch=%u rate=%u bits=%u bytes=%u\n\r",
		(stduint)wav.audio_format,
		(stduint)wav.channel_count,
		(stduint)wav.sample_rate,
		(stduint)wav.bits_per_sample,
		(stduint)wav.pcm_size);

	if (wav.audio_format != WAV_FORMAT_PCM) {
		outsfmt("playmzk: only PCM wav is supported\n\r");
		free(wav_file);
		return -1;
	}
	if (wav.bits_per_sample != 8 || wav.channel_count != 1) {
		outsfmt("playmzk: only U8 mono wav is supported for now\n\r");
		free(wav_file);
		return -1;
	}

	const bool ok = PlayU8Mono(wav);
	free(wav_file);

	if (!ok) {
		outsfmt("playmzk: playback failed\n\r");
		return -1;
	}

	outsfmt("playmzk: playback ok\n\r");
	return 0;
}
