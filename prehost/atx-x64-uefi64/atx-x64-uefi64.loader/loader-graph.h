#ifndef _UEFI_GRAPH_H_
#define _UEFI_GRAPH_H_

#include <c/graphic/color.h>

struct FrameBufferConfig {
	uint8_t* frame_buffer;
	uint32_t pixels_per_scan_line;
	uint32_t horizontal_resolution;
	uint32_t vertical_resolution;
	enum PixelFormat pixel_format;
};

typedef struct FrameBufferConfig UefiData;

#endif
