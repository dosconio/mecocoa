// UTF-8 g++ TAB4 LF
// ModuTitle: Music Player Application (playmzk)
// Description: Streams audio files to Audio System Service using HostMusic.

#include "aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <stdio.h>
#include <stdlib.h>
#include <c/format/audio/WAV.h>
#include <c/format/audio/MP3.h>
#include <c/format/audio/FLAC.h>
#include <c/format/audio/OGG.h>
#include <c/format/audio/MIDI.h>
#include <cpp/System/Audiosys.hpp>
#include <cpp/System/Audiosys/Lyrics.hpp>
#include "../include/syscall.hpp"

using namespace uni;

#define outsfmt(...) printf(__VA_ARGS__)

static void PrintUsage(const char* prog_name) {
	outsfmt("Mecocoa Audio Music Player\n\r\n\r");
	outsfmt("Usage: %s [options] <file.wav | file.mp3 | file.flac | file.ogg | file.mid>\n\r\n\r", prog_name ? prog_name : "playmzk");
	outsfmt("Interactive Controls (during playback):\n\r");
	outsfmt("  [Space] / [P]   : Pause or Resume playback\n\r");
	outsfmt("  [+]     / [-]   : Increase / Decrease Volume (+/- 5%%)\n\r");
	outsfmt("  [M]             : Mute or Unmute audio\n\r");
	outsfmt("  [[]     / []]   : Seek backward / forward 5s\n\r");
	outsfmt("  [{]     / [}]   : Seek backward / forward 30s\n\r");
	outsfmt("  [0] - [9]       : Jump to 0%% - 90%% of track\n\r");
	outsfmt("  [L]             : Toggle single track loop mode\n\r");
	outsfmt("  [Q]     / [Esc] : Stop playback and Exit\n\r\n\r");
	outsfmt("Options:\n\r");
	outsfmt("  -h, --help           : Display this help message\n\r");
	outsfmt("  -l, --loop           : Enable loop playback mode\n\r");
	outsfmt("  -c, --lyric <file>   : Specify LRC lyrics file\n\r\n\r");
	outsfmt("Supported Formats:\n\r");
	outsfmt("  WAV:  PCM (8/16/24/32-bit), IEEE Float, ADPCM (MS/IMA), A-law, mu-law\n\r");
	outsfmt("  MP3:  MPEG-1/2/2.5 Layer III / II / I\n\r");
	outsfmt("  FLAC: Free Lossless Audio Codec (1..8ch, 8..32-bit, Constant/Verbatim/Fixed/LPC)\n\r");
	outsfmt("  OGG:  Ogg Vorbis (1..8ch, 8000..192000Hz, Floor 1, Residue 0/1/2)\n\r");
	outsfmt("  MIDI: Standard MIDI File (SMF 0/1, 16ch GM Software Synthesizer, 64 Polyphony)\n\r");
}

static int TryGetKeyboardChar() {
	stduint ret = syscall(syscall_t::INNC, 0, 0, 0);
	if (ret == (stduint)-1 || ret == 0) return -1;
	return (int)(ret & 0xFF);
}

static uint32 GetCurrentTimeMs() {
	return (uint32)syscall(syscall_t::TIME, 1, 0, 0);
}

static void BuildLrcPath(const char* wav_path, char* lrc_path, uint32 max_len) {
	if (!wav_path || !lrc_path || max_len < 5) return;
	uint32 len = StrLength(wav_path);
	if (len >= max_len) len = max_len - 1;
	MemCopyN(lrc_path, wav_path, len);
	lrc_path[len] = '\0';
	int dot_idx = -1;
	for (int i = (int)len - 1; i >= 0; --i) {
		if (lrc_path[i] == '.') { dot_idx = i; break; }
		if (lrc_path[i] == '/' || lrc_path[i] == '\\') break;
	}
	if (dot_idx >= 0 && dot_idx + 4 < (int)max_len) {
		lrc_path[dot_idx] = '.';
		lrc_path[dot_idx + 1] = 'l';
		lrc_path[dot_idx + 2] = 'r';
		lrc_path[dot_idx + 3] = 'c';
		lrc_path[dot_idx + 4] = '\0';
	} else if (len + 4 < max_len) {
		lrc_path[len] = '.';
		lrc_path[len + 1] = 'l';
		lrc_path[len + 2] = 'r';
		lrc_path[len + 3] = 'c';
		lrc_path[len + 4] = '\0';
	}
}

static char* LoadLrcFile(const char* path, uint32& out_size, trait::Malloc& alloc) {
	out_size = 0;
	if (!path) return nullptr;
	FILE* fp = fopen(path, "rb");
	if (!fp) return nullptr;
	if (fseek(fp, 0, SEEK_END) != 0) {
		fclose(fp);
		return nullptr;
	}
	long sz = ftell(fp);
	if (sz <= 0 || sz > 1024 * 1024) {
		fclose(fp);
		return nullptr;
	}
	fseek(fp, 0, SEEK_SET);
	char* buf = (char*)alloc.allocate((stduint)sz + 1);
	if (!buf) {
		fclose(fp);
		return nullptr;
	}
	size_t rd = fread(buf, 1, (size_t)sz, fp);
	fclose(fp);
	buf[rd] = '\0';
	out_size = (uint32)rd;
	return buf;
}

static bool PlayAudioWithHostMusic(HostMusic& music, const Lyrics* lyrics = nullptr) {
	AudioInfo info{};
	if (!music.getInfo(info)) return false;

	const uint32 total_duration_sec = info.durationMs / 1000;
	uint32 last_printed_sec = (uint32)-1;
	int32 last_lyric_idx = -2;
	bool last_paused_state = false;
	uint32 last_vol_percent = (uint32)-1;
	bool last_mute_state = false;
	bool last_loop_state = music.isLoop();
	uint32 last_pause_toggle_ms = 0;

	music.setVolume(80);

	outsfmt("Controls: [Space] Pause/Resume, [+/-] Volume, [M] Mute, [[/]] Seek, [L] Loop, [Q] Quit\n\r");

	if (!music.Play()) return false;

	while (true) {
		int key = TryGetKeyboardChar();
		while (key > 0) {
			if (key == ' ' || key == 'p' || key == 'P') {
				const uint32 now_ms = GetCurrentTimeMs();
				if (now_ms - last_pause_toggle_ms >= 250) {
					last_pause_toggle_ms = now_ms;
					if (music.isPaused()) {
						music.Resume();
						last_printed_sec = (uint32)-1;
					} else {
						music.Pause();
						last_printed_sec = (uint32)-1;
					}
				}
			} else if (key == '+' || key == '=') {
				uint32 cur_vol = music.getVolume();
				uint32 new_vol = (cur_vol + 5 <= 100) ? cur_vol + 5 : 100;
				music.setVolume(new_vol);
				if (music.isMuted()) music.setMute(false);
				last_printed_sec = (uint32)-1;
			} else if (key == '-' || key == '_') {
				uint32 cur_vol = music.getVolume();
				uint32 new_vol = (cur_vol >= 5) ? cur_vol - 5 : 0;
				music.setVolume(new_vol);
				last_printed_sec = (uint32)-1;
			} else if (key == 'm' || key == 'M') {
				music.setMute(!music.isMuted());
				last_printed_sec = (uint32)-1;
			} else if (key == 'l' || key == 'L') {
				music.setLoop(!music.isLoop());
				last_printed_sec = (uint32)-1;
			} else if (key == '[' || key == ']' || key == '{' || key == '}' ||
				key == '<' || key == '>' || key == ',' || key == '.' ||
				(key >= '0' && key <= '9')) {
				int32 delta_ms = 0;
				bool is_jump_percent = false;
				uint32 jump_percent = 0;

				if (key == '[') delta_ms = -5000;
				else if (key == ']') delta_ms = 5000;
				else if (key == '{') delta_ms = -30000;
				else if (key == '}') delta_ms = 30000;
				else if (key == '<' || key == ',') delta_ms = -10000;
				else if (key == '>' || key == '.') delta_ms = 10000;
				else if (key >= '0' && key <= '9') {
					is_jump_percent = true;
					jump_percent = uint32(key - '0') * 10;
				}

				int32 target_ms = 0;
				if (is_jump_percent) {
					target_ms = int32(((uint64)info.durationMs * jump_percent) / 100);
				} else {
					target_ms = int32(music.getPositionMs()) + delta_ms;
				}
				if (target_ms < 0) target_ms = 0;
				if (target_ms > (int32)info.durationMs) target_ms = (int32)info.durationMs;

				if (music.Seek((uint32)target_ms)) {
					last_printed_sec = (uint32)-1;
					last_lyric_idx = -2;
				}
			} else if (key == 'q' || key == 'Q' || key == 27 || key == 3) {
				music.Stop();
				outsfmt("\n\r[Stopped by user]\n\r");
				return true;
			}
			key = TryGetKeyboardChar();
		}

		if (music.isPaused()) {
			uint32 cur_ms = music.getPositionMs();
			if (lyrics && lyrics->GetLineCount() > 0) {
				int32 cur_lyric_idx = lyrics->FindLineIndex(cur_ms);
				if (cur_lyric_idx != last_lyric_idx) {
					last_lyric_idx = cur_lyric_idx;
					if (cur_lyric_idx >= 0) {
						const auto* cur_line = lyrics->GetLine((uint32)cur_lyric_idx);
						if (cur_line && cur_line->text && cur_line->text[0]) {
							uint32 sec = cur_line->time_ms / 1000;
							outsfmt("\r                                                                               \r♪ [%02u:%02u] %s\n\r",
								sec / 60, sec % 60, cur_line->text);
						}
					}
					last_printed_sec = (uint32)-1;
				}
			}

			uint32 cur_sec = cur_ms / 1000;
			bool cur_paused = music.isPaused();
			uint32 cur_vol = music.getVolume();
			bool cur_mute = music.isMuted();
			bool cur_loop = music.isLoop();

			if (cur_sec != last_printed_sec || cur_paused != last_paused_state ||
				cur_vol != last_vol_percent || cur_mute != last_mute_state ||
				cur_loop != last_loop_state) {
				auto str = "\r[⏸︎ Paused %3u%% | Loop: %s] %02u:%02u / %02u:%02u (Space: Resume, +/-: Vol, M: Mute, Q: Quit) "_ustr;
				outsfmt(str.reference(),
					cur_vol,
					cur_loop ? "ON " : "OFF",
					cur_sec / 60, cur_sec % 60,
					total_duration_sec / 60, total_duration_sec % 60);
				last_printed_sec = cur_sec;
				last_paused_state = cur_paused;
				last_vol_percent = cur_vol;
				last_mute_state = cur_mute;
				last_loop_state = cur_loop;
			}
			sysrest(1, 50);
			continue;
		}

		last_paused_state = music.isPaused();

		bool active = music.Update();
		if (!active && !music.isPlaying()) {
			break;
		}

		uint32 cur_ms = music.getPositionMs();
		if (lyrics && lyrics->GetLineCount() > 0) {
			int32 cur_lyric_idx = lyrics->FindLineIndex(cur_ms);
			if (cur_lyric_idx != last_lyric_idx) {
				last_lyric_idx = cur_lyric_idx;
				if (cur_lyric_idx >= 0) {
					const auto* cur_line = lyrics->GetLine((uint32)cur_lyric_idx);
					if (cur_line && cur_line->text && cur_line->text[0]) {
						uint32 sec = cur_line->time_ms / 1000;
						outsfmt("\r                                                                               \r♪ [%02u:%02u] %s\n\r",
							sec / 60, sec % 60, cur_line->text);
					} else {
						outsfmt("\r                                                                               \r♪ (~ Interlude ~)\n\r");
					}
				}
				last_printed_sec = (uint32)-1;
			}
		}

		uint32 cur_sec = cur_ms / 1000;
		uint32 cur_vol = music.getVolume();
		bool cur_mute = music.isMuted();
		bool cur_loop = music.isLoop();

		if (cur_sec != last_printed_sec || cur_vol != last_vol_percent ||
			cur_mute != last_mute_state || cur_loop != last_loop_state) {
			if (cur_mute) {
				auto str = "\r[▶︎ Muted     | Loop: %s] %02u:%02u / %02u:%02u (Space: Pause, +/-: Vol, M: Mute, Q: Quit) "_ustr;
				outsfmt(str.reference(),
					cur_loop ? "ON " : "OFF",
					cur_sec / 60, cur_sec % 60,
					total_duration_sec / 60, total_duration_sec % 60);
			} else {
				auto str = "\r[▶︎ %3u%% Vol | Loop: %s] %02u:%02u / %02u:%02u (Space: Pause, +/-: Vol, M: Mute, Q: Quit) "_ustr;
				outsfmt(str.reference(),
					cur_vol,
					cur_loop ? "ON " : "OFF",
					cur_sec / 60, cur_sec % 60,
					total_duration_sec / 60, total_duration_sec % 60);
			}
			last_printed_sec = cur_sec;
			last_vol_percent = cur_vol;
			last_mute_state = cur_mute;
			last_loop_state = cur_loop;
		}
	}

	outsfmt("\r[Finished ] %02u:%02u / %02u:%02u                                \n\r",
		total_duration_sec / 60, total_duration_sec % 60,
		total_duration_sec / 60, total_duration_sec % 60);
	return true;
}

int main(int argc, char** argv)
{
	#if __BITS__ == 64
	return -1;
	#endif

	const char* file_path = nullptr;
	const char* lyric_file_path = nullptr;
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
		} else if ((StrCompare(argv[i], "-c") == 0 ||
			StrCompare(argv[i], "--lyric") == 0) && i + 1 < argc) {
			lyric_file_path = argv[++i];
		} else if (argv[i][0] != '-') {
			file_path = argv[i];
		}
	}

	if (!file_path) {
		PrintUsage(argv[0]);
		return -1;
	}

	FILE* test_fp = fopen(file_path, "rb");
	if (!test_fp) {
		outsfmt("playmzk: cannot open file '%s' (file not found or unreadable)\n\r", file_path);
		return -1;
	}
	fclose(test_fp);

	HostMusic music;
	if (!music.Open(file_path, loop_mode)) {
		outsfmt("playmzk: failed to parse audio '%s' (unsupported or corrupted format)\n\r", file_path);
		return -1;
	}

	AudioInfo info{};
	music.getInfo(info);

	outsfmt("playmzk: format=%u ch=%u rate=%u bits=%u bytes=%u%s\n\r",
		(stduint)info.format.sample_format,
		(stduint)info.format.channels,
		(stduint)info.format.sample_rate,
		(stduint)info.bitsPerSample,
		(stduint)info.dataByteLength,
		loop_mode ? " [Loop enabled]" : "");

	StdMalloc my_malloc;

	// Try loading LRC lyrics
	Lyrics lyrics;
	char lrc_auto_path[260];
	const char* target_lrc = lyric_file_path;
	if (!target_lrc) {
		BuildLrcPath(file_path, lrc_auto_path, sizeof(lrc_auto_path));
		target_lrc = lrc_auto_path;
	}

	uint32 lrc_size = 0;
	char* lrc_data = LoadLrcFile(target_lrc, lrc_size, my_malloc);
	if (lrc_data && lrc_size > 0) {
		if (lyrics.Parse(lrc_data, lrc_size, my_malloc)) {
			outsfmt("playmzk: loaded lyrics '%s' (%u lines)\n\r", target_lrc, (stduint)lyrics.GetLineCount());
			const auto& meta = lyrics.GetMetadata();
			if (meta.title[0]) outsfmt("  Title:  %s\n\r", meta.title);
			if (meta.artist[0]) outsfmt("  Artist: %s\n\r", meta.artist);
			if (meta.album[0]) outsfmt("  Album:  %s\n\r", meta.album);
		}
		my_malloc.deallocate(lrc_data);
	}

	const bool ok = PlayAudioWithHostMusic(music, lyrics.GetLineCount() > 0 ? &lyrics : nullptr);
	music.Close();

	if (!ok) {
		outsfmt("playmzk: playback failed\n\r");
		return -1;
	}

	outsfmt("playmzk: playback ok\n\r");
	return 0;
}
