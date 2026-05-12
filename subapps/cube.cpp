#include "../accmlib/inc/aaaaa.h"
#include "c/arith.h"

using namespace uni;

template <class T>
struct Vector3D {
	T x, y, z;
};

template <class T>
struct Vector2D {
	T x, y;
};

const int kScale = 50;
const int kCanvasWidth = 160;
const int kCanvasHeight = 160;

static Color cube_buffer[kCanvasWidth * kCanvasHeight];

const Vector3D<int> kCube[8] = {
	{ 1,  1,  1}, { 1,  1, -1}, { 1, -1,  1}, { 1, -1, -1},
	{-1,  1,  1}, {-1,  1, -1}, {-1, -1,  1}, {-1, -1, -1}
};

const int kSurface[6][4] = {
	{0,4,6,2}, {1,3,7,5}, {0,2,3,1}, {0,1,5,4}, {4,5,7,6}, {6,7,3,2}
};

const Color kColor[6] = {
	(0xFFFF0000), // Red
	(0xFF00FF00), // Green
	(0xFFFFFF00), // Yellow
	(0xFF0000FF), // Blue
	(0xFFFF00FF), // Magenta
	(0xFF00FFFF)  // Cyan
};

static Vector3D<double> vert[8];
static double centerz4[6];
static Vector2D<int> scr[8];

void DrawSurface(int sur) {
	const int* surface = kSurface[sur];
	int ymin = kCanvasHeight, ymax = -1;
	static int x_min[kCanvasHeight], x_max[kCanvasHeight];

	for (int i = 0; i < kCanvasHeight; i++) {
		x_min[i] = kCanvasWidth;
		x_max[i] = -1;
	}

	for (int i = 0; i < 4; i++) {
		Vector2D<int> p0 = scr[surface[i]];
		Vector2D<int> p1 = scr[surface[(i + 1) % 4]];
		
		int ystart = p0.y, yend = p1.y;
		if (ystart > yend) { int t = ystart; ystart = yend; yend = t; }
		if (ystart < ymin) ymin = ystart;
		if (yend > ymax) ymax = yend;

		if (p0.y == p1.y) {
			int y = p0.y;
			if (y >= 0 && y < kCanvasHeight) {
				if (p0.x < x_min[y]) x_min[y] = p0.x;
				if (p1.x < x_min[y]) x_min[y] = p1.x;
				if (p0.x > x_max[y]) x_max[y] = p0.x;
				if (p1.x > x_max[y]) x_max[y] = p1.x;
			}
			continue;
		}

		double dx_dy = (double)(p1.x - p0.x) / (p1.y - p0.y);
		for (int y = ystart; y <= yend; y++) {
			if (y >= 0 && y < kCanvasHeight) {
				int x = (int)(p0.x + (y - p0.y) * dx_dy);
				if (x < x_min[y]) x_min[y] = x;
				if (x > x_max[y]) x_max[y] = x;
			}
		}
	}

	if (ymin < 0) ymin = 0;
	if (ymax >= kCanvasHeight) ymax = kCanvasHeight - 1;

	Color c = kColor[sur];
	c.a = 0xFF; // Opaque

	for (int y = ymin; y <= ymax; y++) {
		int p0x = x_min[y];
		int p1x = x_max[y];
		if (p0x >= kCanvasWidth || p1x < 0) continue;
		if (p0x < 0) p0x = 0;
		if (p1x >= kCanvasWidth) p1x = kCanvasWidth - 1;
		for (int x = p0x; x <= p1x; x++) {
			cube_buffer[y * kCanvasWidth + x] = c;
		}
	}

}

int main(int argc, char** argv) {
	// asm volatile ("finit");
	_preprocess();

	Rectangle rect = { Point(200, 200), Size2(kCanvasWidth + 2, kCanvasHeight + 19) };
	stdsint form_id = sys_create_form(-1, &rect);
	if (form_id < 0) return -1;

	for0 (i, kCanvasWidth * kCanvasHeight) cube_buffer[i] = (0xFFFFFFFF);
	sys_set_form_buffer(form_id, cube_buffer);
	sys_update_form(form_id, nullptr);
	// sys_set_timer(form_id, 40); // 40ms timer -> ~25fps
	sys_set_timer(form_id, 80); // 80ms timer -> ~12.5fps

	int thx = 0, thy = 0, thz = 0;
	const double to_rad = 3.14159265358979323 / 0x8000;

	while (true) {
		SheetMessage smsg;
		if (sys_fetch_msg(form_id, 1, &smsg) != 1) continue;

		if (smsg.event == SheetEvent::onTimer) {
			// Clear buffer (Black Opaque)
			Color black = Color::Black;
			black.a = 0xFF;
			for (int i = 0; i < kCanvasWidth * kCanvasHeight; i++) cube_buffer[i] = black;

			thx = (thx + 182) & 0xffff;
			thy = (thy + 273) & 0xffff;
			thz = (thz + 364) & 0xffff;
			double xp = dblcos(thx * to_rad);
			double xa = dblsin(thx * to_rad);
			double yp = dblcos(thy * to_rad);
			double ya = dblsin(thy * to_rad);
			double zp = dblcos(thz * to_rad);
			double za = dblsin(thz * to_rad);

			for (int i = 0; i < 8; i++) {
				auto cv = kCube[i];
				double zt = kScale * cv.z * xp + kScale * cv.y * xa;
				double yt = kScale * cv.y * xp - kScale * cv.z * xa;
				double xt = kScale * cv.x * yp + zt * ya;
				vert[i].z = zt * yp - kScale * cv.x * ya;
				vert[i].x = xt * zp - yt * za;
				vert[i].y = yt * zp + xt * za;

				// Perspective projection
				double t = 6.0 * kScale / (vert[i].z + 8.0 * kScale);
				scr[i].x = (int)(vert[i].x * t) + kCanvasWidth / 2;
				scr[i].y = (int)(vert[i].y * t) + kCanvasHeight / 2;
			}

			// Compute center Z for painter's algorithm
			for (int sur = 0; sur < 6; sur++) {
				centerz4[sur] = 0;
				for (int i = 0; i < 4; i++) centerz4[sur] += vert[kSurface[sur][i]].z;
			}

			// Draw back-to-front
			bool draw_mask[6] = { true, true, true, true, true, true };
			for (int count = 0; count < 6; count++) {
				double zmax = -1e30;
				int sur = -1;
				for (int i = 0; i < 6; i++) {
					if (draw_mask[i] && centerz4[i] > zmax) {
						zmax = centerz4[i];
						sur = i;
					}
				}
				if (sur == -1) break;
				draw_mask[sur] = false;

				// Back-face culling (using screen coordinates)
				auto v0 = scr[kSurface[sur][0]], v1 = scr[kSurface[sur][1]], v2 = scr[kSurface[sur][2]];
				double e0x = v1.x - v0.x, e0y = v1.y - v0.y;
				double e1x = v2.x - v1.x, e1y = v2.y - v1.y;
				if (e0x * e1y - e0y * e1x <= 0) DrawSurface(sur);
			}

			sys_update_form(form_id, nullptr);
		}
		else if (smsg.event == SheetEvent::onClick) {
			// args[3] == 1: close button; !(args[2] & 0x10): left button released
			if (smsg.args[3] == 1 && !(smsg.args[2] & 0x10)) {
				sys_close_form(form_id);
				return 0;
			}
		}
	}

	return 0;
}
