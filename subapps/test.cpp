//#include <c/stdinc.h>
#include "../accmlib/inc/aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <c/ISO_IEC_STD/signal.h>

using namespace uni;

// mnt33 fat16
// mnt34 fat32

static Color test_buffer[318 * 221];
static Color colors[] = { Color::Red, Color::Green, Color::Blue, Color::Yellow, (0xFF00FFFF), (0xFFFF00FF) };
static int color_idx = 0;

static void sigint_handler(int signo) {
	outsfmt("\n\r[TestApp] Received SIGINT (%d)! Interrupted successfully!\n\r", signo);
}

static void sigfpe_handler(int signo) {
	outsfmt("\n\r[TestApp] Received SIGFPE (%d) from divide-by-zero!\n\r", signo);
	_exit(1);
}

int main(int argc, char** argv)
{
	struct _POSIX_sigaction act;
	act.sa_handler = sigint_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_restorer = nullptr;
	sigaction(SIGINT, &act, nullptr);

	struct _POSIX_sigaction act_fpe;
	act_fpe.sa_handler = sigfpe_handler;
	sigemptyset(&act_fpe.sa_mask);
	act_fpe.sa_flags = 0;
	act_fpe.sa_restorer = nullptr;
	sigaction(SIGFPE, &act_fpe, nullptr);

	// Trigger exception test if required:
	if (0) {
		volatile int a = 1;
		volatile int b = 0;
		volatile int c = a / b;
		(void)c;
	}

	unsigned id = getpid();// TEST
	outsfmt("C(%d)\n\r", id);// OUTC

	#if 1

	#if __BITS__ == 32
	rostr filename = "/mnt33/owo.txt";
	#else
	rostr filename = "/md0/owo.txt";
	#endif

	int fd = sysopen(filename);
	if (fd < 0) {
		fd = sys_createfil(filename);
	}

	if (fd >= 0) {
		outsfmt("Open success! FD=%d\n\r", fd);
		const char* msg = "Hello from Mecocoa VFS!\n";
		write(fd, (void*)msg, 24);
		close(fd);

		fd = sysopen(filename);
		if (fd >= 0) {
			char buf[32] = {0};
			read(fd, buf, 24);
			outsfmt("Read result: %s\n\r", buf);
			close(fd);
		}
	} else {
		outsfmt("Open failed with code %d\n\r", fd);
	}

	sys_removefil(filename);
	#endif

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

	#if 1 // MMAP
	// * ((byte*)0x20000000) = 0x55;// should be killed
	// below test user-heap (malloc) :
	outsfmt("[TestApp] Starting user heap test...\n\r");

	// malloc 1: correct allocation and free
	outsfmt("[TestApp] Performing malloc 1 (100 bytes)...\n\r");
	void* ptr1 = malloc(100);
	if (ptr1) {
		outsfmt("[TestApp] malloc 1 succeeded, address: %p\n\r", ptr1);
		// Write to it to trigger page fault (demand paging) and verify zero-filling
		bool zeroed = true;
		for (int i = 0; i < 100; i++) {
			if (((char*)ptr1)[i] != 0) {
				zeroed = false;
			}
		}
		outsfmt("[TestApp] malloc 1 memory zero-fill check: %s\n\r", zeroed ? "PASSED" : "FAILED");
		
		// Write unique pattern
		((char*)ptr1)[0] = 0xA5;
		
		// free 1: correct free
		outsfmt("[TestApp] Freeing malloc 1 with correct address...\n\r");
		free(ptr1);
		outsfmt("[TestApp] Freeing malloc 1 completed.\n\r");
	} else {
		outsfmt("[TestApp] malloc 1 failed!\n\r");
	}

	// free 2: incorrect address (offset from original ptr1 to test invalid deallocation detection)
	if (ptr1) {
		void* bad_ptr = (void*)((stduint)ptr1 + 4);
		outsfmt("[TestApp] Freeing incorrect address %p (should print Mempool error)...\n\r", bad_ptr);
		free(bad_ptr);
		outsfmt("[TestApp] Freeing incorrect address completed.\n\r");
	}

	// malloc 2: allocated and not freed (OS will clean up on exit)
	outsfmt("[TestApp] Performing malloc 2 (200 bytes, will NOT be freed)...\n\r");
	void* ptr2 = malloc(200);
	if (ptr2) {
		outsfmt("[TestApp] malloc 2 succeeded, address: %p\n\r", ptr2);
		// Write to trigger page fault
		((char*)ptr2)[0] = 0x5A;
		outsfmt("[TestApp] malloc 2 written successfully, leaving it for OS cleanup.\n\r");
	} else {
		outsfmt("[TestApp] malloc 2 failed!\n\r");
	}

	// Trigger access to unmapped area to demonstrate abnormal address display
	outsfmt("[TestApp] Accessing unmapped address 0x50000000 to trigger crash...\n\r");
	sysrest(1, 100); // Short delay to ensure output is flushed
	*((volatile char*)0x50000000) = 0xAA;
	#endif

	// loop sysrest(0, 0);
	return 0;
}

