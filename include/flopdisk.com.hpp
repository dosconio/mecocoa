#ifndef FLOPDISK_COM_HPP_
#define FLOPDISK_COM_HPP_

#include <c/stdinc.h>

enum class FloppyDriverMsg : stduint {
	Attach = 0x30000,
	Read,
	Write,
};

struct FloppyDriverRequest {
	uint32 drive = 0;
	uint32 lba = 0;
};

struct FloppyDriverAttach {
	uint8 drive_type[2] = {};
	uint8 media_present[2] = {};
};

#endif
