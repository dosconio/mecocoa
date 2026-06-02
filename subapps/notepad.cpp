#include "aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <cpp/Witch/Control/Control-TextBox.hpp>

using namespace uni;

int main(int argc, char** argv)
{
	#if __BITS__ == 64
	_preprocess();
	#endif

	// Define window rectangle
	Rectangle win_rect{ Point(150, 100), Size2(400, 300) };

	// Instantiate user-space GraphicForm wrapper
	GraphicForm form(win_rect, "Notepad");
	if (form.getFormId() < 0) {
		outsfmt("Notepad: Failed to initialize GraphicForm!\r\n");
		return -1;
	}

	// Mount a real user-space TextBox control filling the client area
	uni::witch::control::TextBox textbox;
	
	// Default to empty string
	textbox.text = "";

	// Attempt to load file if path argument is provided
	if (argc >= 2 && argv[1] != nullptr) {
		int fd = open(argv[1], O_RDONLY);
		if (fd >= 0) {
			struct stat st;
			if (fstat(fd, &st) == 0 && st.st_size > 0) {
				char* buf = (char*)malloc(st.st_size + 1);
				if (buf) {
					stdsint read_bytes = read(fd, buf, st.st_size);
					if (read_bytes > 0) {
						buf[read_bytes] = '\0';
						textbox.text = buf;
					}
					free(buf);
				}
			}
			close(fd);
		}
	}

	textbox.InitializeSheet(form.getLayerManager(), Point(10, 10), Size2(378, 261));
	form.getLayerManager().Append(&textbox);
	textbox.Start();

	// Initial render to draw controls onto framebuffer and synchronize to kernel
	textbox.doshow(nullptr);
	sys_update_form(form.getFormId(), nullptr);

	// Enter message event loop
	SheetMessage smsg;
	while (sys_fetch_msg(form.getFormId(), true, &smsg)) {
		// Intercept close button onClick event
		if (smsg.event == SheetEvent::onClick && smsg.args[3] == 1 && !(smsg.args[2] & 0x10)) {
			ploginfo("Notepad: Close button clicked, exiting.");
			break;
		}

		// Intercept Alt+F4 keyboard event
		if (smsg.event == SheetEvent::onKeybd) {
			auto* key_event = (keyboard_event_t*)smsg.args;
			if (key_event->keycode == _UKEY_F4 && (key_event->mod.l_alt || key_event->mod.r_alt)) {
				ploginfo("Notepad: Alt+F4 pressed, exiting.");
				break;
			}
		}

		// Automatically translate coordinates and dispatch event to user-space LayerManager
		form.HandleEvent(smsg);
	}

	return 0;
}
