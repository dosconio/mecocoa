// ASCII C/C++ TAB4 CRLF
// Docutitle: Image Viewer Application
// Attribute: Mecocoa Sub-Application
// Copyright: Dosconio Mecocoa

#include "aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <c/ustring.h>

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

#include <c/format/picture/BMP.h>
#include <c/format/picture/JPEG.h>
#include <c/format/picture/PNG.h>
#include <c/format/picture/GIF.h>
#include <cpp/trait/StorageTrait.hpp>

using namespace uni;

// USB-HID keycodes mapping
const byte kKEsc = 0x29; // Escape key
const byte kKF4  = 0x3D; // F4 key

// Lightweight file-based StorageTrait for reading image files with zero full-buffer memory allocation
class FileBlockDevice : public StorageTrait {
private:
	FILE*   m_fp;
	stduint m_size;

public:
	FileBlockDevice(FILE* fp, stduint size, stduint blockSize = 512)
		: m_fp(fp), m_size(size) {
		Block_Size = blockSize;
		readable = true;
		writable = false;
	}

	virtual ~FileBlockDevice() = default;

	virtual bool Read(stduint BlockIden, void* Dest) override {
		if (BlockIden >= getUnits()) return false;
		if (fseek(m_fp, (long)(BlockIden * Block_Size), SEEK_SET) != 0) return false;
		size_t rd = fread(Dest, 1, Block_Size, m_fp);
		return rd == Block_Size || (rd > 0 && BlockIden + 1 == getUnits());
	}

	virtual bool Write(stduint BlockIden, const void* Sors) override {
		return false;
	}

	virtual stduint getUnits() override {
		return (m_size + Block_Size - 1) / Block_Size;
	}

	virtual int operator[](uint64 bytid) override {
		if (bytid >= m_size) return -1;
		byte b = 0;
		if (fseek(m_fp, (long)bytid, SEEK_SET) != 0) return -1;
		if (fread(&b, 1, 1, m_fp) == 1) return b;
		return -1;
	}
};

int main(int argc, char** argv)
{

	if (argc < 2 || argv[1] == nullptr) {
		outsfmt("Usage: viewpic <filepath>\n\r");
		return -1;
	}

	// Open the target image file via standard stdio stream
	FILE* fp = fopen(argv[1], "rb");
	if (!fp) {
		outsfmt("Error: Failed to open file '%s'\n\r", argv[1]);
		return -1;
	}

	// Fetch file size via fseek/ftell
	fseek(fp, 0, SEEK_END);
	long fileSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	if (fileSize <= 0) {
		outsfmt("Error: Failed to get file size or file is empty.\n\r");
		fclose(fp);
		return -1;
	}

	// Use lightweight FileBlockDevice without loading entire file into memory
	FileBlockDevice storage(fp, (stduint)fileSize);

	// Decode image data using the C++ IImageCodec interface
	ImageBuffer imgBuf;
	ImageBufferClear(imgBuf);
	StdMalloc myMalloc;
	BMPCodec bmpCodec;
	JPEGCodec jpegCodec;
	PNGCodec pngCodec;
	GIFCodec gifCodec;
	const IImageCodec* codec = nullptr;
	ImageDecodeOptions options;
	ImageDecodeOptionsInit(options);

	// Probe image format
	bool matched = false;
	if (bmpCodec.Probe(storage, matched) == ImageResult::OK && matched) {
		codec = &bmpCodec;
	} else if (jpegCodec.Probe(storage, matched) == ImageResult::OK && matched) {
		codec = &jpegCodec;
	} else if (pngCodec.Probe(storage, matched) == ImageResult::OK && matched) {
		codec = &pngCodec;
	} else if (gifCodec.Probe(storage, matched) == ImageResult::OK && matched) {
		codec = &gifCodec;
	}

	if (!codec) {
		outsfmt("Error: Unsupported image format.\n\r");
		outsfmt("Please provide a valid BMP, JPEG, PNG, or GIF image file.\n\r");
		fclose(fp);
		return -1;
	}

	// Check if this is an animated GIF
	GIFAnimation gifAnim{};
	bool isAnimated = false;

	if (codec->GetFormat() == ImageFormat::GIF) {
		byte* fileData = (byte*)malloc(fileSize);
		if (fileData) {
			fseek(fp, 0, SEEK_SET);
			if (fread(fileData, 1, fileSize, fp) == (size_t)fileSize) {
				if (DecodeGIFAnimation(fileData, (size_t)fileSize, gifAnim) && gifAnim.frameCount > 1) {
					isAnimated = true;
				}
			}
			free(fileData);
		}
	}

	int width = 0;
	int height = 0;
	uni::Color* canvas = nullptr;
	size_t canvasSize = 0;
	uint32 animDelay = 100;

	if (isAnimated) {
		fclose(fp);
		width = (int)gifAnim.width;
		height = (int)gifAnim.height;
		canvasSize = (size_t)width * (size_t)height * sizeof(uni::Color);
		canvas = (uni::Color*)malloc(canvasSize);
		if (!canvas) {
			outsfmt("Error: Out of memory when allocating animation canvas.\n\r");
			FreeGIFAnimation(gifAnim);
			return -1;
		}
		MemCopyN(canvas, gifAnim.frames[0].pixels, canvasSize);
		animDelay = gifAnim.frames[0].delayMs;
		if (animDelay < 20) animDelay = 20; // Clamp minimal tick interval
	} else {
		// Clean up partial animation struct if any
		if (gifAnim.frames) {
			FreeGIFAnimation(gifAnim);
		}
		ImageResult imgRes = codec->Decode(storage, imgBuf, myMalloc, options);
		fclose(fp);

		if (imgRes != ImageResult::OK) {
			outsfmt("Error: Failed to decode image with %s codec.\n\r", codec->GetName());
			return -1;
		}
		width = (int)imgBuf.width;
		height = (int)imgBuf.height;
		canvas = (uni::Color*)imgBuf.pixels;
	}

	// Window dimensions: width + 2 border pixels, height + 19 title bar/border pixels
	Rectangle rect{ Point(150, 100), Size2(width + 2, height + 19) };

	// Create graphical form window in Mecocoa
	auto form_id = sys_create_form(-_IMM0, &rect);
	if (form_id < 0) {
		outsfmt("Error: Failed to create form (code %d).\n\r", form_id);
		if (isAnimated) {
			if (canvas) free(canvas);
			FreeGIFAnimation(gifAnim);
		} else {
			ImageBufferFree(imgBuf);
		}
		return -1;
	}

	// Register the canvas framebuffer ONCE with the window server
	sys_set_form_buffer(form_id, canvas);

	// Perform initial draw and synchronize window content to kernel space
	sys_update_form(form_id, nullptr);

	uint32 curFrame = 0;
	if (isAnimated) {
		sys_set_timer(form_id, animDelay);
	}

	// Main graphical message polling loop
	SheetMessage smsg;
	while (sys_fetch_msg(form_id, true, &smsg)) {
		switch (smsg.event) {
		case SheetEvent::onTimer:
			if (isAnimated && gifAnim.frameCount > 1) {
				curFrame = (curFrame + 1) % gifAnim.frameCount;
				MemCopyN(canvas, gifAnim.frames[curFrame].pixels, canvasSize);
				sys_update_form(form_id, nullptr);
			}
			break;

		case SheetEvent::onClick:
			// Close when left mouse button is released on the Close Button (args[3] == 1)
			if (smsg.args[3] == 1 && !(smsg.args[2] & 0x10)) {
				sys_close_form(form_id);
				if (isAnimated) {
					if (canvas) free(canvas);
					FreeGIFAnimation(gifAnim);
				} else {
					ImageBufferFree(imgBuf);
				}
				return 0;
			}
			break;

		case SheetEvent::onKeybd:
			{
				keyboard_event_t* key_event = (keyboard_event_t*)smsg.args;
				bool down = (key_event->method == keyboard_event_t::method_t::keydown ||
							 key_event->method == keyboard_event_t::method_t::keyrepeat);
				if (down) {
					// Close window when Alt+F4 or Escape is pressed
					if ((key_event->keycode == kKF4 && (key_event->mod.l_alt || key_event->mod.r_alt)) ||
						(key_event->keycode == kKEsc)) {
						sys_close_form(form_id);
						if (isAnimated) {
							if (canvas) free(canvas);
							FreeGIFAnimation(gifAnim);
						} else {
							ImageBufferFree(imgBuf);
						}
						return 0;
					}
				}
			}
			break;

		default:
			break;
		}
	}

	// Fallback cleanup in case the loop exits unexpectedly
	sys_close_form(form_id);
	if (isAnimated) {
		if (canvas) free(canvas);
		FreeGIFAnimation(gifAnim);
	} else {
		ImageBufferFree(imgBuf);
	}
	return 0;
}
