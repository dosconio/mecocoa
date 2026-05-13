// d:\her\mecocoa\subapps\game_breakout.cpp
// ASCII C++ TAB4 LF
// AllAuthor: @ArinaMgk, @dosconio
// ModuTitle: Breakout Game Application
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../accmlib/inc/aaaaa.h"

using namespace uni;

// ---- Canvas & block layout ----
const int kNBX  = 10, kNBY = 5;
const int kBW   = 22, kBH  = 12;
const int kGapX = 10, kGapY = 18;
// canvas client area: 240 x 160
const int kCW   = 2 * kGapX + kNBX * kBW;           // 240
const int kCH   = kGapY + kNBY * kBH + 66 + 6 + 10; // 160
// bar
const int kBarW = 50, kBarH = 6, kBarFlt = 10;
const int kBarY = kCH - kBarFlt - kBarH;             // 144
// ball (square approximation, radius = half-side)
const int kBR   = 5;
// speed (pixels/frame)
const int kBarSpd = 12, kBallSp = 2;
const int kAcc = 3, kFric = 50; // inertia: acceleration and friction
const int kFPS    = 15;

// USB-HID keycodes (derived from key_ps2set1_usb_E0 mapping table)
const byte kKLeft  = 0x50; // Left Arrow  (E0+0x4B -> 0x50)
const byte kKRight = 0x4F; // Right Arrow (E0+0x4D -> 0x4F)
const byte kKSpace = 0x2C; // Space
const byte kKF4    = 0x3D; // F4

// ---- Game state ----
enum class GState : byte { Waiting, Playing, GameOver, Win };

static Color  canvas[kCW * kCH];
static bool   blk[kNBY][kNBX];
static GState gs;
static int    bar_x;
static int    bx, by, bvx, bvy;
static int    move_dir; // target direction: -1 / 0 / +1
static int    bar_vx;   // current horizontal velocity

// ---- Colors (0xAARRGGBB) ----
const Color kCBg   = (Color)(0xFF1A1A2E);
const Color kCBar  = (Color)(0xFFCCCCFF);
const Color kCBall = (Color)(0xFFFFDD00);
const Color kCOver = (Color)(0xFF550000);
const Color kCWin  = (Color)(0xFF003322);
const Color kCBlk[kNBY] = {
	(Color)(0xFFFF4466),
	(Color)(0xFFFF8844),
	(Color)(0xFFFFDD00),
	(Color)(0xFF44CC66),
	(Color)(0xFF4488EE),
};

// ---- Framebuffer fill ----
static inline void fill(int x, int y, int w, int h, Color c) {
	c.a = 0xFF;
	for (int r = y; r < y + h; r++) {
		if (r < 0 || r >= kCH) continue;
		for (int col = x; col < x + w; col++) {
			if (col < 0 || col >= kCW) continue;
			canvas[r * kCW + col] = c;
		}
	}
}

// ---- Helpers ----
static void reset_blocks() {
	for (int r = 0; r < kNBY; r++)
		for (int c = 0; c < kNBX; c++)
			blk[r][c] = true;
}

static void reset_ball() {
	bx  = bar_x + kBarW / 2;
	by  = kBarY - kBR - 1;
	bvx = kBallSp;
	bvy = 0;
}

static void game_init() {
	bar_x    = (kCW - kBarW) / 2;
	bar_vx   = 0;
	move_dir = 0;
	gs       = GState::Waiting;
	reset_blocks();
	reset_ball();
}

static bool all_gone() {
	for (int r = 0; r < kNBY; r++)
		for (int c = 0; c < kNBX; c++)
			if (blk[r][c]) return false;
	return true;
}

// ---- Rendering ----
static void render(stduint form_id) {
	Color bg = (gs == GState::GameOver) ? kCOver :
	           (gs == GState::Win)      ? kCWin  : kCBg;
	fill(0, 0, kCW, kCH, bg);

	// blocks
	for (int r = 0; r < kNBY; r++) {
		int py = kGapY + r * kBH;
		for (int c = 0; c < kNBX; c++) {
			if (!blk[r][c]) continue;
			fill(kGapX + c * kBW + 1, py + 1, kBW - 2, kBH - 2, kCBlk[r]);
		}
	}

	// bar
	fill(bar_x, kBarY, kBarW, kBarH, kCBar);

	// ball (hidden when dead)
	if (gs == GState::Waiting || gs == GState::Playing)
		fill(bx - kBR, by - kBR, kBR * 2, kBR * 2, kCBall);

	// status stripe at bottom: color encodes game state
	Color sc = (gs == GState::Waiting)  ? (Color)(0xFF223366) :
	           (gs == GState::Playing)  ? (Color)(0xFF111122) :
	           (gs == GState::GameOver) ? (Color)(0xFF882222) :
	                                      (Color)(0xFF116633);
	fill(0, kCH - 12, kCW, 2, sc);

	sys_update_form(form_id, nullptr);
}

// ---- Game tick (called on onTimer when Playing) ----
static void game_tick(stduint form_id) {
	// move bar with inertia
	if (move_dir > 0) {
		bar_vx += kAcc;
		if (bar_vx > kBarSpd) bar_vx = kBarSpd;
	} else if (move_dir < 0) {
		bar_vx -= kAcc;
		if (bar_vx < -kBarSpd) bar_vx = -kBarSpd;
	} else {
		if (bar_vx > 0) {
			bar_vx -= kFric;
			if (bar_vx < 0) bar_vx = 0;
		} else if (bar_vx < 0) {
			bar_vx += kFric;
			if (bar_vx > 0) bar_vx = 0;
		}
	}

	bar_x += bar_vx;
	if (bar_x < 0) {
		bar_x = 0;
		bar_vx = 0;
	}
	if (bar_x + kBarW > kCW) {
		bar_x = kCW - kBarW;
		bar_vx = 0;
	}

	// move ball
	int nx = bx + bvx;
	int ny = by + bvy;

	// side walls
	if (nx - kBR < 0)      { nx = kBR;          bvx = -bvx; }
	if (nx + kBR >= kCW)   { nx = kCW - kBR - 1; bvx = -bvx; }
	// ceiling
	if (ny - kBR < 0)      { ny = kBR;           bvy = -bvy; }

	// bar bounce (Robust AABB check)
	if (bvy > 0 && 
	    nx + kBR >= bar_x && nx - kBR <= bar_x + kBarW &&
	    ny + kBR >= kBarY && ny - kBR <= kBarY + kBarH) {
		int rel = nx - (bar_x + kBarW / 2);
		bvy = -kBallSp;
		if      (rel < -kBarW / 4) bvx = -kBallSp;
		else if (rel >  kBarW / 4) bvx =  kBallSp;
		ny = kBarY - kBR - 1; // Snap to top of bar
	}

	// fall off bottom
	if (ny - kBR >= kCH) {
		gs = GState::GameOver;
		bvx = bvy = 0;
		return;
	}

	// block collision (AABB, stop after first hit)
	for (int r = 0; r < kNBY; r++) {
		int py0 = kGapY + r * kBH, py1 = py0 + kBH;
		for (int c = 0; c < kNBX; c++) {
			if (!blk[r][c]) continue;
			int px0 = kGapX + c * kBW, px1 = px0 + kBW;
			if (nx + kBR <= px0 || nx - kBR >= px1) continue;
			if (ny + kBR <= py0 || ny - kBR >= py1) continue;
			blk[r][c] = false;
			// bounce: prefer vertical if entering top/bottom face
			bool from_top    = (by + kBR <= py0);
			bool from_bottom = (by - kBR >= py1);
			if (from_top || from_bottom) bvy = -bvy;
			else                          bvx = -bvx;
			goto blk_hit;
		}
	}
blk_hit:

	bx = nx; by = ny;

	if (all_gone()) {
		gs  = GState::Win;
		bvx = bvy = 0;
	}
}

// ---- Entry point ----
int main(int argc, char **argv) {
	_preprocess();
	
	Rectangle rect{ Point(120, 80), Size2(kCW + 2, kCH + 19) };
	stdsint form_id = sys_create_form(-1, &rect);
	if (form_id < 0) return -1;

	for (int i = 0; i < kCW * kCH; i++) canvas[i] = kCBg;
	sys_set_form_buffer(form_id, canvas);
	sys_update_form(form_id, nullptr);

	game_init();
	sys_set_timer(form_id, 1000 / kFPS);

	SheetMessage smsg;
	while (sys_fetch_msg(form_id, 1, &smsg) == 1) {
		switch (smsg.event) {

		case SheetEvent::onTimer:
			if (gs == GState::Playing) game_tick(form_id);
			render(form_id);
			break;

		case SheetEvent::onKeybd: {
			auto* kev = (keyboard_event_t*)smsg.args;
			bool down = (kev->method == keyboard_event_t::method_t::keydown ||
			             kev->method == keyboard_event_t::method_t::keyrepeat);
			if (kev->keycode == kKF4 && (kev->mod.l_alt || kev->mod.r_alt)) {
				sys_close_form(form_id); return 0;
			}
			if (kev->keycode == kKRight) {
				if (down) move_dir = 1;
				else if (move_dir == 1) move_dir = 0;
			}
			if (kev->keycode == kKLeft) {
				if (down) move_dir = -1;
				else if (move_dir == -1) move_dir = 0;
			}
			if (kev->keycode == kKSpace && down) {
				if (gs == GState::Waiting) {
					gs = GState::Playing;
					bvx = kBallSp; bvy = -kBallSp;
				} else if (gs == GState::GameOver || gs == GState::Win) {
					game_init();
				}
			}
			break;
		}

		case SheetEvent::onClick:
			if (smsg.args[3] == 1 && !(smsg.args[2] & 0x10)) {
				sys_close_form(form_id); return 0;
			}
			break;

		default:
			break;
		}
	}
	return 0;
}
