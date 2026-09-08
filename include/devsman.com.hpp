#ifndef DEVSMAN_COM_HPP_
#define DEVSMAN_COM_HPP_

#include <c/stdinc.h>
#include <cpp/System/Audiosys.hpp>
#include <cpp/Device/Audio/SoundBlaster.hpp>

// ---- AUDIO IPC PROTOCOL ----

enum class AudioMsg : stduint {
	TEST,
	PLAY_PCM,
	PLAY_PCM_U8_MONO = PLAY_PCM,
	STREAM_BEGIN,
	STREAM_WRITE,
	STREAM_DRAIN,
	STREAM_STOP,
	STREAM_PAUSE,
	STREAM_RESUME,
	STREAM_GET_POS,
	SET_VOLUME,
	GET_VOLUME,
	STREAM_SEEK,
};

struct AudioSeekRequest {
	uint32 target_ms;
	uint64 target_samples;
};

struct AudioVolumeRequest {
	uni::SoundBlasterMixerChannel channel;
	uint8 left;
	uint8 right;
	bool mute;
};

struct AudioStreamPosition {
	uint64 played_bytes;
	uint32 played_samples;
	uint32 played_ms;
	bool is_paused;
	bool is_active;
};

#endif /* DEVSMAN_COM_HPP_ */