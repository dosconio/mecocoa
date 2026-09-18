// ASCII C/C++ TAB4 CRLF
// Docutitle: Video Player Application (playmov)
// Attribute: Mecocoa Sub-Application
// Copyright: Dosconio Mecocoa, UNISYM

#include "aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <c/ustring.h>

#include <c/format/video/AVI.h>
#include <cpp/System/Videosys.hpp>
#include "../include/syscall.hpp"
#include "../include/devsman.com.hpp"
#include "../include/taskman.com.hpp"

using namespace uni;

// USB-HID keycodes mapping
const byte kKEsc      = 0x29; // Escape key
const byte kKF4       = 0x3D; // F4 key
const byte kKSpc      = 0x2C; // Space key
const byte kKQ        = 0x14; // Q key
const byte kKP        = 0x13; // P key
const byte kKM        = 0x10; // M key
const byte kKL        = 0x0F; // L key
const byte kKMinus    = 0x2D; // - / _ key
const byte kKEqual    = 0x2E; // = / + key
const byte kKLBracket = 0x2F; // [ / { key
const byte kKRBracket = 0x30; // ] / } key
const byte kK1        = 0x1E; // 1 key
const byte kK9        = 0x26; // 9 key
const byte kK0        = 0x27; // 0 key



static void PrintUsage(const char* prog_name) {
	outsfmt("Mecocoa Video Player (playmov)\n\r\n\r");
	outsfmt("Usage: %s [options] <video_file.avi | video_file.mpg>\n\r\n\r", prog_name ? prog_name : "playmov");
	outsfmt("Interactive Controls (during playback):\n\r");
	outsfmt("  [Space] / [P]   : Pause or Resume playback\n\r");
	outsfmt("  [+]     / [-]   : Increase / Decrease Volume (+/- 5%%)\n\r");
	outsfmt("  [M]             : Mute or Unmute audio\n\r");
	outsfmt("  [[]     / []]   : Seek backward / forward 5s\n\r");
	outsfmt("  [{]     / [}]   : Seek backward / forward 30s\n\r");
	outsfmt("  [0] - [9]       : Jump to 0%% - 90%% of video\n\r");
	outsfmt("  [L]             : Toggle single video loop mode\n\r");
	outsfmt("  [Q]     / [Esc] : Stop playback and Exit\n\r\n\r");
	outsfmt("Options:\n\r");
	outsfmt("  -h, --help      : Display this help message\n\r");
	outsfmt("  -l, --loop      : Enable loop playback mode\n\r\n\r");
	outsfmt("Supported Formats:\n\r");
	outsfmt("  AVI:   Motion JPEG (MJPEG), Raw RGB/DIB\n\r");
	outsfmt("  MPEG:  MPEG-1 / MPEG-2 Video & Program Stream (.mpg, .mpeg, .m1v, .m2v, .vob, .dat)\n\r");
}


int main(int argc, char** argv)
{
	#if __BITS__ == 64
	return -1;
	#endif

	const char* file_path = nullptr;
	bool loop_mode = false;

	for (int i = 1; i < argc; ++i) {
		if (!argv[i]) continue;
		if (StrCompare(argv[i], "-h") == 0 ||
			StrCompare(argv[i], "--help") == 0) {
			PrintUsage(argv[0]);
			return 0;
		} else if (StrCompare(argv[i], "-l") == 0 ||
			StrCompare(argv[i], "--loop") == 0) {
			loop_mode = true;
		} else if (argv[i][0] != '-') {
			file_path = argv[i];
		}
	}

	if (!file_path) {
		PrintUsage(argv[0]);
		return -1;
	}

	HostVideo video;
	if (!video.Open(file_path, loop_mode)) {
		outsfmt("playmov: failed to open or parse video '%s'\n\r", file_path);
		return -1;
	}

	VideoInfo vinfo{};
	if (!video.GetInfo(vinfo) || vinfo.videoFormat.width == 0 || vinfo.videoFormat.height == 0) {
		outsfmt("playmov: invalid video format or dimensions\n\r");
		video.Close();
		return -1;
	}

	int width = (int)vinfo.videoFormat.width;
	int height = (int)vinfo.videoFormat.height;
	uint32 total_duration_sec = vinfo.durationMs / 1000;
	uint32 fps = (vinfo.videoFormat.frame_rate_num && vinfo.videoFormat.frame_rate_den)
		? (vinfo.videoFormat.frame_rate_num / vinfo.videoFormat.frame_rate_den)
		: 30;
	if (fps == 0) fps = 30;
	uint32 timer_interval_ms = 1000 / fps;
	if (timer_interval_ms < 10) timer_interval_ms = 10;

	outsfmt("playmov: '%s' %ux%u @ %u fps, duration: %02u:%02u, total frames: %u%s\n\r",
		file_path, width, height, fps,
		total_duration_sec / 60, total_duration_sec % 60,
		vinfo.totalFrames,
		loop_mode ? " [Loop enabled]" : "");

	size_t canvas_size = (size_t)width * (size_t)height * sizeof(uni::Color);
	uni::Color* canvas = (uni::Color*)malloc(canvas_size);
	if (!canvas) {
		outsfmt("playmov: out of memory allocating canvas\n\r");
		video.Close();
		return -1;
	}
	MemSet(canvas, 0, canvas_size);

	// Window dimensions: width + 2 border pixels, height + 19 title bar/border pixels
	Rectangle rect{ Point(120, 80), Size2(width + 2, height + 19) };

	auto form_id = sys_create_form(-_IMM0, &rect);
	if (form_id < 0) {
		outsfmt("playmov: failed to create form window (code %d)\n\r", form_id);
		free(canvas);
		video.Close();
		return -1;
	}

	sys_set_form_title(form_id, file_path);
	sys_set_form_buffer(form_id, canvas);
	sys_update_form(form_id, nullptr);

	video.Play();

	uint32 last_title_sec = (uint32)-1;
	char title_buf[128];

	// Main graphical polling loop
	SheetMessage smsg;
	while (true) {
		// Drain UI and input messages non-blockingly
		while (sys_fetch_msg(form_id, false, &smsg)) {
			switch (smsg.event) {
			case SheetEvent::onClick:
				// Close when left mouse button is released on Close Button (args[3] == 1)
				if (smsg.args[3] == 1 && !(smsg.args[2] & 0x10)) {
					video.Stop();
					video.Close();
					sys_close_form(form_id);
					if (canvas) free(canvas);
					return 0;
				} else if (smsg.args[3] == 3 && !(smsg.args[2] & 0x10)) {
					sys_minimize_form(form_id);
				}
				break;

			case SheetEvent::onKeybd:
				{
					keyboard_event_t* key_event = (keyboard_event_t*)smsg.args;
					bool down = (key_event->method == keyboard_event_t::method_t::keydown ||
								 key_event->method == keyboard_event_t::method_t::keyrepeat);
					if (down) {
						if ((key_event->keycode == kKF4 && (key_event->mod.l_alt || key_event->mod.r_alt)) ||
							(key_event->keycode == kKEsc) || (key_event->keycode == kKQ)) {
							video.Stop();
							video.Close();
							sys_close_form(form_id);
							if (canvas) free(canvas);
							return 0;
						} else if (key_event->keycode == kKSpc || key_event->keycode == kKP) {
							if (video.isPaused()) {
								video.Resume();
							} else {
								video.Pause();
							}
						} else if (key_event->keycode == kKEqual) {
							uint32 v = video.getVolume();
							video.setVolume(v + 5 <= 100 ? v + 5 : 100);
						} else if (key_event->keycode == kKMinus) {
							uint32 v = video.getVolume();
							video.setVolume(v >= 5 ? v - 5 : 0);
						} else if (key_event->keycode == kKM) {
							video.setMute(!video.isMuted());
						} else if (key_event->keycode == kKL) {
							video.setLoop(!video.isLoop());
						} else if (key_event->keycode == kKLBracket) {
							uint32 delta = (key_event->mod.l_shift || key_event->mod.r_shift) ? 30000 : 5000;
							int32 new_pos = (int32)video.GetPositionMs() - (int32)delta;
							video.Seek(new_pos > 0 ? (uint32)new_pos : 0);
						} else if (key_event->keycode == kKRBracket) {
							uint32 delta = (key_event->mod.l_shift || key_event->mod.r_shift) ? 30000 : 5000;
							video.Seek(video.GetPositionMs() + delta);
						} else if (key_event->keycode >= kK1 && key_event->keycode <= kK9) {
							uint32 pct = (uint32)(key_event->keycode - kK1 + 1) * 10;
							uint32 new_pos = (uint32)(((uint64)total_duration_sec * 1000ULL * pct) / 100);
							video.Seek(new_pos);
						} else if (key_event->keycode == kK0) {
							video.Seek(0);
						}
					}

				}
				break;

			default:
				break;
			}
		}

		bool rendered = false;
		if (video.isPlaying()) {
			video.Update();

			uint32 frame_pts = 0;
			if (video.UpdateFrame(canvas, frame_pts)) {
				rendered = true;
				sys_update_form(form_id, nullptr);

				uint32 cur_sec = frame_pts / 1000;
				if (cur_sec != last_title_sec) {
					last_title_sec = cur_sec;
					snprintf(title_buf, sizeof(title_buf), "%s [%02u:%02u / %02u:%02u] (%u fps)",
						file_path, cur_sec / 60, cur_sec % 60,
						total_duration_sec / 60, total_duration_sec % 60, fps);
					sys_set_form_title(form_id, title_buf);
				}
			} else if (video.GetState() == HostVideoState::Stopped) {
				if (!loop_mode) {
					snprintf(title_buf, sizeof(title_buf), "%s [Finished]", file_path);
					sys_set_form_title(form_id, title_buf);
				}
			}
		}

		sysrest(1, rendered ? 2 : 5); // Yield CPU smoothly
	}



	// Fallback cleanup
	video.Stop();
	video.Close();
	sys_close_form(form_id);
	if (canvas) free(canvas);
	return 0;
}
