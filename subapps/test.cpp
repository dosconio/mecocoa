//#include <c/stdinc.h>
#include "../accmlib/inc/aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <c/ISO_IEC_STD/signal.h>
#include <sys/mman.h>

using namespace uni;

static Color test_buffer[318 * 221];
static Color colors[] = { Color::Red, Color::Green, Color::Blue, Color::Yellow, (0xFF00FFFF), (0xFFFF00FF) };
static int color_idx = 0;

int main(int argc, char** argv)
{


	unsigned id = getpid();// TEST
	outsfmt("C(%d)\n\r", id);// OUTC

	#if 0 // TEST FORM
	Rectangle rect{ Point(100, 80), Size2(320, 240) };
	auto form_id = sys_create_form(-_IMM0, &rect);
	if (form_id >= 0) {
		sys_set_form_buffer(form_id, test_buffer);
		for (int i = 0; i < 318 * 221; i++) {
			test_buffer[i].r = 0xFF;
			test_buffer[i].g = 0xFF;
			test_buffer[i].b = 0xFF;
			test_buffer[i].a = 0xFF;
		}
		sys_update_form(form_id, nullptr);

		sys_draw_default_string(form_id, Point2(50, 50), "Ciallo~", Color::Maroon);
		sys_draw_point(form_id, Point2(100, 100), Color::Red);
		sys_draw_line(form_id, Point2(80, 100), Point2(40, 30), Color::Blue);
		Rectangle rect0{ Point(16, 16), Size2(10, 40), Color::Green };
		sys_draw_rectangle(form_id, &rect0);
		sys_set_timer(form_id, 1000); // 1s timer
	}
	else {
		outsfmt("Create form failed with code %d\n\r", form_id);
	}
	if (0) for0(i, 3) {
		outsfmt("TEST(%d)\n\r", syssecond());// TIME
		sysrest(0 _Comment(s), 1);// REST
	}// delay 3 s
	SheetMessage smsg;
	keyboard_event_t* key_event;
	while (sys_fetch_msg(form_id, true, &smsg)) {
		switch (smsg.event) {
		case SheetEvent::onTimer:
		{
			Color c = colors[color_idx];
			c.a = 0xFF; // Ensure opaque
			for (int i = 0; i < 318 * 221; i++) test_buffer[i] = c;
			color_idx = (color_idx + 1) % 6;
			// sys_update_form(form_id, nullptr);
			// outsfmt("msg: onTimer at %u\n\r", smsg.args[3]);
			break;
		}
		case SheetEvent::onMoved:
			// ploginfo("msg: _moved at (%d, %d)", smsg.args[0], smsg.args[1]);
			break;
		case SheetEvent::onClick:
			if (smsg.args[3] == 1 && !(smsg.args[2] & 0x10)) {
				// Left button release on Close Button
				ploginfo("msg: _close requested via button");
				sys_close_form(form_id);
				return 0;
			} else {
				ploginfo("msg: _click at (%d, %d), comp=%x", smsg.args[0], smsg.args[1], smsg.args[2]);
			}
			break;
		case SheetEvent::onKeybd:
			key_event = (keyboard_event_t*)smsg.args;
			// exit when Alt+F4
			if (key_event->keycode == _UKEY_F4 && (key_event->mod.l_alt || key_event->mod.r_alt)) {
				_exit(0);
			}
			ploginfo("msg: kboard code=%[32H]", *key_event);
			break;
		default:
			ploginfo("msg: typ%u", smsg.event);
			break;
		}
	}
	if (0) {
		if (form_id >= 0) sys_close_form(form_id);
		form_id = -1;
	};



	#endif

	#if 1 // MMAP FILE
	{
		auto tmp = (char*)malloc(0x1001);
		*tmp = 'a';
		free(tmp);

		outsfmt("=== File MMAP Test Start ===\n\r");
		rostr test_file = "/md0/mmap_t.txt";

		// 1. Create and write to the test file
		int fd = sysopen(test_file);
		if (fd < 0) {
			fd = sys_createfil(test_file);
		}
		if (fd < 0) {
			outsfmt("Error: Failed to create test file %s!\n\r", test_file);
			return -1;
		}

		const char* init_content = "Hello Mecocoa File MMAP!";
		int written = write(fd, (void*)init_content, 24);
		outsfmt("Step 1: Write init pattern, fd=%d, bytes=%d\n\r", fd, written);
		close(fd);

		// 2. Re-open and establish mmap mapping
		fd = sysopen(test_file);
		if (fd < 0) {
			outsfmt("Error: Failed to re-open test file!\n\r");
			return -1;
		}

		// Map a 4KB page with flag 7 (present | writable | user)
		void* map_addr = mmap(nullptr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_SHARED, fd, 0);
		outsfmt("Step 2: MMAP completed, map_addr=0x%x\n\r", (stduint)map_addr);
		if (map_addr == MAP_FAILED) {
			outsfmt("Error: MMAP failed!\n\r");
			close(fd);
			return -1;
		}

		// 3. Immediately close original fd to verify lifecycle independence (inode ref count)
		close(fd);
		outsfmt("Step 3: Closed original fd. Attempting to read from map...\n\r");

		// 4. First-time read and verify
		char* ptr = (char*)map_addr;
		char read_buf[32] = {0};
		for (int i = 0; i < 24; i++) {
			read_buf[i] = ptr[i];
		}
		outsfmt("Step 4: Read from memory: '%s'\n\r", read_buf);

		bool match = true;
		for (int i = 0; i < 24; i++) {
			if (read_buf[i] != init_content[i]) {
				match = false;
				break;
			}
		}
		if (match) {
			outsfmt("Success: Initial memory contents match!\n\r");
		} else {
			outsfmt("Error: Memory contents mismatch!\n\r");
		}

		// 5. Modify mapped memory ("Hello" -> "World")
		outsfmt("Step 5: Modifying mapped memory ('Hello' -> 'World')...\n\r");
		ptr[0] = 'W';
		ptr[1] = 'o';
		ptr[2] = 'r';
		ptr[3] = 'l';
		ptr[4] = 'd';

		// 6. Unmap memory (munmap / UMAP)
		outsfmt("Step 6: Unmapping memory...\n\r");
		int umap_res = munmap(map_addr, 4096);
		if (umap_res != 0) {
			outsfmt("Error: UMAP failed with code %d!\n\r", umap_res);
		} else {
			outsfmt("Success: UMAP success!\n\r");
		}

		// 7. Re-open the file to verify data writeback
		fd = sysopen(test_file);
		if (fd < 0) {
			outsfmt("Error: Failed to open test file for writeback verification!\n\r");
			return -1;
		}

		char verify_buf[32] = {0};
		int read_bytes = read(fd, verify_buf, 24);
		outsfmt("Step 7: Read back from file: '%s' (bytes=%d)\n\r", verify_buf, read_bytes);
		close(fd);

		const char* expected_content = "World Mecocoa File MMAP!";
		match = true;
		for (int i = 0; i < 24; i++) {
			if (verify_buf[i] != expected_content[i]) {
				match = false;
				break;
			}
		}
		if (match) {
			outsfmt("Success: File contents correctly written back!\n\r");
			outsfmt("=== File MMAP Test ALL PASSED! ===\n\r");
		} else {
			outsfmt("Error: File contents NOT written back properly!\n\r");
		}

		// 8. Clean up the temporary test file
		sys_removefil(test_file);
		outsfmt("Step 8: Cleaned up test file.\n\r");
	}
	#endif

	// loop sysrest(0, 0);
	return 0;
}

