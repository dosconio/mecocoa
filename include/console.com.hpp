#ifndef CONSOLE_COM_HPP_
#define CONSOLE_COM_HPP_

#include <c/stdinc.h>

enum GraphicDriverCaps : uint32 {
	GraphicDriverCap_SetMode = 1 << 0,
	GraphicDriverCap_Flush = 1 << 1,
};

static constexpr uint32 GraphicDriverProtocolVersion = 1;
static constexpr uint32 GraphicDriverPixelFormatARGB8888 = 0;

_PACKED(struct) FMT_GraphicMsg_DRV_ATTACH {
	uint32 version;
	uint32 caps;
	uint32 dev_handle;
	uint32 resource_type;
	uint32 resource_index;
	uint32 format;
	uint64 fb_start;
	uint64 fb_offset;
	uint64 fb_length;
	uint32 width;
	uint32 height;
	uint32 pitch;
};

#endif
