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

#ifndef _INC_CPP
struct MemoryMap {
	UINTN  buffer_size;
	VOID*  buffer;
	UINTN  map_size;
	UINTN  map_key;
	UINTN  descriptor_size;
	UINT32 descriptor_version;
};
#endif

typedef struct {
	struct FrameBufferConfig frame_buffer_config;
	struct MemoryMap memory_map;
} UefiData;

#endif
