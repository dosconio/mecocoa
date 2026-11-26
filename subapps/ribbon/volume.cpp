#include "ribbon.hpp"
#include "cpp/Witch/Control/Control-TrackBar.hpp"
#include <stdio.h>
#include <stdlib.h>
#include "../../include/syscall.hpp"

#if CONFIG_ENABLE_AUDIOSYS
#include "../../include/devsman.com.hpp"
#endif

static constexpr stduint kVolWidth = 200;
static constexpr stduint kVolHeight = 112;

static stdsint current_volume = 80;
static bool current_muted = false;
static bool has_audio_service = false;
static TrackBar vol_trackbar(80, 0, 100, false);
static Button btn_mute("Mute");

bool HasAudioService() {
	return has_audio_service;
}

static void SyncMuteButtonText() {
	switch (current_lang) {
	case Lango_Japanese:
		btn_mute.text = current_muted ? "解除" : "消音";
		break;
	case Lango_Chinese:
		btn_mute.text = current_muted ? "恢复" : "静音";
		break;
	default:
		btn_mute.text = current_muted ? "Unmute" : "Mute";
		break;
	}
}

void ApplyVolumeLango(Lango lang) {
	(void)lang;
	SyncMuteButtonText();
}

static rostr GetVolumeTitleText() {
	if (current_lang == Lango_Japanese || current_lang == Lango_Chinese) {
		return "音量";
	}
	return "Volume";
}

#if CONFIG_ENABLE_AUDIOSYS
static uint8 EncodeAudioVolume(uint32 percent) {
	#if (_ACCM & 0xFF00) == 0x8600
	return SoundBlasterPercentToRaw(percent);
	#else
	return uint8(percent * 255 / 100);
	#endif
}

static uint32 DecodeAudioVolume(uint8 raw) {
	#if (_ACCM & 0xFF00) == 0x8600
	return SoundBlasterRawToPercent(raw);
	#else
	return uint32(raw) * 100 / 255;
	#endif
}

static stdsint ExchangeAudioMsg(AudioMsg type, const void* payload, stduint length, void* response, stduint capacity) {
	CommMsg message{};
	message.data.address = (stduint)payload;
	message.data.length = length;
	message.type = (stduint)type;
	stdsint status = (stdsint)syscall(syscall_t::COMM, COMM_SEND, Task_Audio_Serv, (stduint)&message);
	if (status) return status < 0 ? status : -status;

	CommMsg reply{};
	reply.data.address = (stduint)response;
	reply.data.length = capacity;
	status = (stdsint)syscall(syscall_t::COMM, COMM_RECV, Task_Audio_Serv, (stduint)&reply);
	if (status) return status < 0 ? status : -status;
	return 0;
}
#endif

void InitVolumeState() {
	has_audio_service = syscall(syscall_t::TEST, 'T', 'F', TestFeature_Audio) == 1;
#if CONFIG_ENABLE_AUDIOSYS
	AudioVolumeRequest req{};
	req.channel = SoundBlasterMixerChannel::MasterVolume;
	AudioVolumeRequest resp{};
	if (has_audio_service && ExchangeAudioMsg(AudioMsg::GET_VOLUME, &req, sizeof(req), &resp, sizeof(resp)) == 0) {
		uint8 pct = uint8(DecodeAudioVolume(resp.left));
		if (pct > 0 || resp.mute) {
			current_volume = (stdsint)pct;
			current_muted = resp.mute && (pct == 0);
		}
	}
#endif
	vol_trackbar.setValue(current_volume);
	SyncMuteButtonText();
}

static void CommitHardwareVolume(stdsint percent, bool muted) {
#if CONFIG_ENABLE_AUDIOSYS
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	uint8 raw = EncodeAudioVolume((uint32)percent);
	AudioVolumeRequest req{};
	req.channel = SoundBlasterMixerChannel::MasterVolume;
	req.left = raw;
	req.right = raw;
	req.mute = muted;
	stdsint result = -1;
	(void)ExchangeAudioMsg(AudioMsg::SET_VOLUME, &req, sizeof(req), &result, sizeof(result));
#else
	(void)percent;
	(void)muted;
#endif
}

// 16x16 inline color palette bitmap for tray and popup speaker icon
static const char kSpeakerBitmap[16][17] = {
	"................",
	"........#.......",
	".......##....2..",
	"......#L#.....2.",
	".....#LY#..1...2",
	"..####YY#...1..2",
	".#WGG#YY#...1..2",
	".#WGG#YY#...1..2",
	".#GGG#YY#...1..2",
	".#GGG#YY#...1..2",
	"..####YY#..1...2",
	".....#GY#.....2.",
	"......#G#....2..",
	".......##.......",
	"........#.......",
	"................",
};

static const char kMuteOverlay[6][7] = {
	"RR..RR",
	".RRRR.",
	"..RR..",
	"..RR..",
	".RRRR.",
	"RR..RR",
};

static void DrawSpeakerIcon(Color* buffer, stduint pitch, stduint width, stduint height,
	stduint x, stduint y, stdsint volume_pct, bool muted)
{
	if (!buffer) return;
	for (stduint row = 0; row < 16; ++row) {
		if (y + row >= height) break;
		for (stduint col = 0; col < 16; ++col) {
			if (x + col >= width) break;
			char ch = kSpeakerBitmap[row][col];
			Color c = 0;
			bool draw = true;
			switch (ch) {
			case '#': c = RColor(0xFF000000); break;
			case 'Y': c = RColor(0xFFE6C646); break;
			case 'L': c = RColor(0xFFFFF2A6); break;
			case 'G': c = RColor(0xFF808080); break;
			case 'W': c = RColor(0xFFFFFFFF); break;
			case '1':
				if (!muted && volume_pct > 0) c = RColor(0xFF1048A0);
				else draw = false;
				break;
			case '2':
				if (!muted && volume_pct >= 50) c = RColor(0xFF1048A0);
				else draw = false;
				break;
			default:
				draw = false;
				break;
			}
			if (draw) {
				buffer[(y + row) * pitch + (x + col)] = c;
			}
		}
	}

	if (muted) {
		for (stduint mr = 0; mr < 6; ++mr) {
			stduint py = y + 5 + mr;
			if (py >= height) break;
			for (stduint mc = 0; mc < 6; ++mc) {
				stduint px = x + 10 + mc;
				if (px >= width) break;
				if (kMuteOverlay[mr][mc] == 'R') {
					buffer[py * pitch + px] = RColor(0xFFD81818);
				}
			}
		}
	}
}

void DrawSpeakerTrayIcon(Color* buffer, stduint pitch, stduint width, stduint height, stduint x, stduint y) {
	DrawSpeakerIcon(buffer, pitch, width, height, x, y, current_volume, current_muted);
}

static void BlitTrackBar(Color* buffer, stduint buf_w, TrackBar& tb) {
	tb.doshow(nullptr);
	if (!tb.sheet_buffer) return;
	for0(y, tb.sheet_area.height) {
		for0(x, tb.sheet_area.width) {
			buffer[(tb.sheet_area.y + y) * buf_w + tb.sheet_area.x + x] =
				tb.sheet_buffer[y * tb.sheet_area.width + x];
		}
	}
}

static void DrawVolumePopup(Color* buffer, stduint w, stduint h) {
	FillRect(buffer, w, w, h, 0, 0, w, h, RColor(0xFFC6C6C6));
	FillRect(buffer, w, w, h, 0, 0, w, 1, RColor(0xFFFFFFFF));
	FillRect(buffer, w, w, h, 0, 0, 1, h, RColor(0xFFFFFFFF));
	FillRect(buffer, w, w, h, w - 1, 0, 1, h, RColor(0xFF000000));
	FillRect(buffer, w, w, h, 0, h - 1, w, 1, RColor(0xFF000000));

	BufferSheet sheet(buffer, w, h);
	stdsint shown_vol = vol_trackbar.getValue();

	DrawSpeakerIcon(buffer, w, w, h, 12, 10, shown_vol, current_muted);
	DrawPopupText(sheet, 34, 10, GetVolumeTitleText(), Color::Black);

	char pct_buf[16] = {};
	if (current_muted) {
		sprintf(pct_buf, "MUTE");
		DrawPopupText(sheet, 152, 10, pct_buf, RColor(0xFFAA2222));
	} else {
		sprintf(pct_buf, "%3d%%", (int)shown_vol);
		DrawPopupText(sheet, 152, 10, pct_buf, Color::Black);
	}

	vol_trackbar.sheet_area = Rectangle(Point(12, 36), Size2(176, 26));
	BlitTrackBar(buffer, w, vol_trackbar);

	btn_mute.sheet_area = Rectangle(Point(12, 72), Size2(176, 28));
	BlitButton(buffer, w, btn_mute);
}

static void FlushVolumePopup(stdsint vol_id) {
	sys_update_form(vol_id, nullptr);
	FlushPopupTextIfNonAscii(vol_id, 34, 10, GetVolumeTitleText(), Color::Black);
	DrawButtonTextIfNonAscii(vol_id, btn_mute);
}

static void FlushVolumeSliderOnly(stdsint vol_id) {
	Rectangle icon_dirty(Point(12, 10), Size2(16, 16));
	Rectangle pct_dirty(Point(148, 10), Size2(40, 16));
	sys_update_form(vol_id, &icon_dirty);
	sys_update_form(vol_id, &pct_dirty);
	sys_update_form(vol_id, &vol_trackbar.sheet_area);
}

static stdsint CreateVolumeForm(Size2 screen, stduint anchor_x, Color** out_buffer) {
	stduint form_height = kRibbonClientHeight;
	stduint px = anchor_x > kVolWidth / 2 ? anchor_x - kVolWidth / 2 : 0;
	if (screen.x > kVolWidth && px + kVolWidth > screen.x) {
		px = screen.x - kVolWidth;
	}
	stduint py = screen.y > form_height + kVolHeight ? screen.y - form_height - kVolHeight : 0;
	Rectangle rect{ Point(px, py), Size2(kVolWidth, kVolHeight) };

	stdsint form_id = sys_create_form(~_IMM0, &rect, GraphicFormStyle_Titleless | GraphicFormStyle_Dock);
	if (form_id < 0) return form_id;

	Color* buffer = (Color*)malloc(kVolWidth * kVolHeight * sizeof(Color));
	if (!buffer) {
		sys_close_form(form_id);
		return -1;
	}

	vol_trackbar.setValue(current_volume);
	vol_trackbar.dragging = false;
	vol_trackbar.committed = false;
	SyncMuteButtonText();

	sys_set_form_buffer(form_id, buffer);
	DrawVolumePopup(buffer, kVolWidth, kVolHeight);
	FlushVolumePopup(form_id);

	*out_buffer = buffer;
	return form_id;
}

bool RunVolumePopup(Size2 screen, stduint anchor_x) {
	if (!has_audio_service) return false;
	stdsint orig_vol = current_volume;
	bool orig_muted = current_muted;

	Color* vol_buf = nullptr;
	stdsint vol_id = CreateVolumeForm(screen, anchor_x, &vol_buf);
	if (vol_id < 0) return false;

	bool vol_open = true;
	while (vol_open) {
		SheetMessage vmsg;
		if (sys_fetch_msg(vol_id, true, &vmsg)) {
			if (vmsg.event == SheetEvent::onLeave) {
				if (vol_trackbar.dragging) {
					vol_trackbar.dragging = false;
					current_volume = vol_trackbar.getValue();
					if (current_volume > 0 && current_muted) {
						current_muted = false;
						SyncMuteButtonText();
					}
					CommitHardwareVolume(current_volume, current_muted);
				}
				vol_open = false;
				break;
			}

			if (vmsg.event == SheetEvent::onClick || vmsg.event == SheetEvent::onMoved) {
				Point vrel(vmsg.args[0], vmsg.args[1]);
				stduint vpara1 = vmsg.args[2];

				stdsint prev_val = vol_trackbar.getValue();
				bool prev_drag = vol_trackbar.dragging;
				bool prev_muted = current_muted;

				if (vol_trackbar.dragging || vol_trackbar.sheet_area.ifContain(vrel)) {
					stdsint rx = (stdsint)vrel.x - (stdsint)vol_trackbar.sheet_area.x;
					stdsint ry = (stdsint)vrel.y - (stdsint)vol_trackbar.sheet_area.y;
					if (rx < 0) rx = 0;
					if (ry < 0) ry = 0;
					vol_trackbar.onrupt(vmsg.event, Point((stduint)rx, (stduint)ry), vpara1);
				}

				bool mute_was = btn_mute.pressed;
				if (!vol_trackbar.dragging && btn_mute.sheet_area.ifContain(vrel)) {
					btn_mute.onrupt(vmsg.event, vrel - btn_mute.sheet_area.getVertex(), vpara1);
				} else {
					btn_mute.onrupt(SheetEvent::onLeave, vrel, 1);
				}

				bool slider_changed = (prev_val != vol_trackbar.getValue() || prev_drag != vol_trackbar.dragging);
				bool mute_btn_changed = (mute_was != btn_mute.pressed);

				// Send SET_VOLUME only upon mouse release on TrackBar
				if (vol_trackbar.TakeCommitted()) {
					current_volume = vol_trackbar.getValue();
					if (current_volume > 0 && current_muted) {
						current_muted = false;
						SyncMuteButtonText();
					}
					CommitHardwareVolume(current_volume, current_muted);
					slider_changed = true;
				}

				if (mute_was && !btn_mute.pressed && vmsg.event == SheetEvent::onClick && btn_mute.sheet_area.ifContain(vrel)) {
					current_muted = !current_muted;
					SyncMuteButtonText();
					CommitHardwareVolume(current_volume, current_muted);
					mute_btn_changed = true;
				}

				if (mute_btn_changed || prev_muted != current_muted) {
					DrawVolumePopup(vol_buf, kVolWidth, kVolHeight);
					FlushVolumePopup(vol_id);
				} else if (slider_changed) {
					DrawVolumePopup(vol_buf, kVolWidth, kVolHeight);
					FlushVolumeSliderOnly(vol_id);
				}
			}
		}
	}

	sys_close_form(vol_id);
	free(vol_buf);
	btn_mute.pressed = false;
	vol_trackbar.dragging = false;
	return (current_volume != orig_vol) || (current_muted != orig_muted);
}
