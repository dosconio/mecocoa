// ASCII CPP-ISO11 TAB4 LF
// ModuTitle: Audio codec driver descriptors
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#ifndef MECOCOA_DEVDRIV_AUDIO_AUCODER_HPP_
#define MECOCOA_DEVDRIV_AUDIO_AUCODER_HPP_

#include <c/stdinc.h>

namespace uni {
	class IIC_t;
	class AudioDeviceInterface;
}

struct AudioCodecDriver {
	const char* NodeName;
	const char* DriverName;
	uni::AudioDeviceInterface* (*ProbeInitialize)(uni::IIC_t& bus, byte address);
};

struct AudioCodecCandidate {
	const AudioCodecDriver* Driver;
	byte Address;
};

extern const AudioCodecDriver AudioCodecES8388;
extern const AudioCodecDriver AudioCodecWM8978;

#endif
