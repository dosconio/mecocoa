// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Desktop Layer Implementation
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "desktop.hpp"
#include <cpp/System/Picture.hpp>

namespace uni {

	Desktop* global_desktop = nullptr;

	Desktop::Desktop(Color color) : SheetTrait(), bg_color(color) {}

	Desktop::~Desktop() {
		if (sheet_buffer) {
			delete[] sheet_buffer;
			sheet_buffer = nullptr;
		}
	}

	void Desktop::doshow(void* buf) {
		Color* p = buf ? (Color*)buf : sheet_buffer;
		if (!p) return;
		stduint total_pixels = sheet_area.getArea();
		for0(i, total_pixels) {
			p[i] = bg_color;
		}
	}

	void Desktop::Initialize(LayerManager& layman, const Rectangle& area, Color color) {
		bg_color = color;
		stduint total_pixels = area.getArea();
		sheet_buffer = new Color[total_pixels];
		InitializeSheet(layman, area.getVertex(), area.getSize(), sheet_buffer);
		doshow(sheet_buffer);
	}

	void Desktop::Reconfigure(LayerManager& layman, const Rectangle& area) {
		if (sheet_buffer) {
			delete[] sheet_buffer;
			sheet_buffer = nullptr;
		}
		stduint total_pixels = area.getArea();
		sheet_buffer = new Color[total_pixels];
		InitializeSheet(layman, area.getVertex(), area.getSize(), sheet_buffer);
		doshow(sheet_buffer);
	}

	void Desktop::onrupt(SheetEvent event, Point rel_p, ...) {
		(void)event;
		(void)rel_p;
		// Events are ignored for now (pinned to bottom, pure background)
	}

	void Desktop::SetBackgroundColor(Color color) {
		bg_color = color;
		doshow(sheet_buffer);
		if (sheet_parent) {
			sheet_parent->Update(this, sheet_area);
		}
	}

	bool Desktop::SetWallpaper(const Color* pixels, Size2 size) {
		if (!pixels || size.x == 0 || size.y == 0) return false;
		if (!sheet_buffer) return false;

		stduint screen_w = sheet_area.width;
		stduint screen_h = sheet_area.height;
		stduint img_w = size.x;
		stduint img_h = size.y;

		// 1. Calculate aspect-fit scaled dimensions using unisym PictureOperation::FitAspect
		stduint dst_w = 0;
		stduint dst_h = 0;
		PictureOperation::FitAspect(img_w, img_h, screen_w, screen_h, dst_w, dst_h);

		stduint offset_x = (screen_w > dst_w) ? (screen_w - dst_w) / 2 : 0;
		stduint offset_y = (screen_h > dst_h) ? (screen_h - dst_h) / 2 : 0;

		// 2. Fill background with classic color
		stduint total_pixels = sheet_area.getArea();
		for0(i, total_pixels) {
			sheet_buffer[i] = bg_color;
		}

		// 3. Aspect-fit scaled sampling
		for0(dy, dst_h) {
			stduint sy = dy * img_h / dst_h;
			if (sy >= img_h) sy = img_h - 1;
			Color* dst_row = sheet_buffer + (offset_y + dy) * screen_w + offset_x;
			const Color* src_row = pixels + sy * img_w;
			for0(dx, dst_w) {
				stduint sx = dx * img_w / dst_w;
				if (sx >= img_w) sx = img_w - 1;
				dst_row[dx] = src_row[sx];
			}
		}

		// 4. Trigger update
		if (sheet_parent) {
			sheet_parent->Update(this, sheet_area);
		}
		return true;
	}

} // namespace uni
