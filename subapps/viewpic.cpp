// ASCII C/C++ TAB4 CRLF
// Docutitle: BMP Image Viewer Application
// Codifiers: @Antigravity
// Attribute: Mecocoa Sub-Application
// Copyright: Dosconio Mecocoa

#include "aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <c/format/picture/BMP.h>

using namespace uni;

// USB-HID keycodes mapping
const byte kKEsc = 0x29; // Escape key
const byte kKF4  = 0x3D; // F4 key

int main(int argc, char** argv)
{
	#if __BITS__ == 64
	_preprocess();
	#endif

	if (argc < 2 || argv[1] == nullptr) {
		outsfmt("Usage: viewpic <filepath.bmp>\n\r");
		return -1;
	}

	// Open the target BMP image file
	int fd = open(argv[1], O_RDONLY);
	if (fd < 0) {
		outsfmt("Error: Failed to open file '%s'\n\r", argv[1]);
		return -1;
	}

	// Fetch file stats to obtain its file size
	struct stat st;
	if (fstat(fd, &st) != 0 || st.st_size <= 0) {
		outsfmt("Error: Failed to get file size or file is empty.\n\r");
		close(fd);
		return -1;
	}

	// Allocate buffer for reading the file contents into memory
	byte* fileData = (byte*)malloc(st.st_size);
	if (!fileData) {
		outsfmt("Error: Out of memory when allocating file buffer.\n\r");
		close(fd);
		return -1;
	}

	stdsint readBytes = read(fd, fileData, st.st_size);
	close(fd);

	if (readBytes != st.st_size) {
		outsfmt("Error: Failed to read complete file data.\n\r");
		free(fileData);
		return -1;
	}

	// Decode BMP image data using the freestanding decoder
	int width = 0;
	int height = 0;
	uni::Color* pixels = DecodeBMP(fileData, st.st_size, &width, &height);
	free(fileData); // Free raw file buffer immediately after decoding

	if (!pixels) {
		outsfmt("Error: Failed to decode BMP image.\n\r");
		outsfmt("Please ensure the file is a valid 24-bit or 32-bit uncompressed Windows BMP.\n\r");
		return -1;
	}

	// Window dimensions: width + 2 border pixels, height + 19 title bar/border pixels
	Rectangle rect{ Point(150, 100), Size2(width + 2, height + 19) };

	// Create graphical form window in Mecocoa
	auto form_id = sys_create_form(-_IMM0, &rect);
	if (form_id < 0) {
		outsfmt("Error: Failed to create form (code %d).\n\r", form_id);
		free(pixels);
		return -1;
	}

	// Set the decoded pixels directly as the window's Framebuffer
	sys_set_form_buffer(form_id, pixels);

	// Perform initial draw and synchronize the window content to kernel space
	sys_update_form(form_id, nullptr);

	// Main graphical message polling loop
	SheetMessage smsg;
	while (sys_fetch_msg(form_id, true, &smsg)) {
		switch (smsg.event) {
		case SheetEvent::onClick:
			// Close when left mouse button is released on the Close Button (args[3] == 1)
			if (smsg.args[3] == 1 && !(smsg.args[2] & 0x10)) {
				sys_close_form(form_id);
				free(pixels);
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
						free(pixels);
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
	free(pixels);
	return 0;
}
