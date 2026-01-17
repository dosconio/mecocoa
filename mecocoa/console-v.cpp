// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST

#include <cpp/unisym>
use crate uni;
#include <c/consio.h>
#include <c/driver/mouse.h>
#include <c/driver/keyboard.h>
#include "../include/console.hpp"


#if defined(_MCCA) && ((_MCCA & 0xFF00) == 0x8600)

const Color kDesktopBGColor = 0xFF2D76ED;
const Color kDesktopFGColor = Color::White;

const int kMouseCursorWidth = 15;
const int kMouseCursorHeight = 24;
char mouse_cursor_shape[kMouseCursorHeight][kMouseCursorWidth + 1] = {
	"@              ",
	"@@             ",
	"@.@            ",
	"@..@           ",
	"@...@          ",
	"@....@         ",
	"@.....@        ",
	"@......@       ",
	"@.......@      ",
	"@........@     ",
	"@.........@    ",
	"@..........@   ",
	"@...........@  ",
	"@............@ ",
	"@......@@@@@@@@",
	"@.....@        ",
	"@....@         ",
	"@...@          ",
	"@..@           ",
	"@.@            ",
	"@@             ",
	"@              ",
	"               ",
	"               ",
};

#if __BITS__ == 32

extern ModeInfoBlock* video_info;

uint8& GloScreenRGB888::Locate(const Point& disp) const {
	return *((uint8*)(video_info->PhysBasePtr) +
		(disp.x * video_info->BitsPerPixel >> 3) +
		disp.y * video_info->BytesPerScanLine);// without boundary check
}
void GloScreenRGB888::SetCursor(const Point& disp) const { _TODO; }// MAYBE unused
Point GloScreenRGB888::GetCursor() const { _TODO return {0, 0}; }// MAYBE unused
void GloScreenRGB888::DrawPoint(const Point& disp, Color color) const {
	uint8* p = &Locate(disp);
	*p++ = color.b;
	*p++ = color.g;
	*p++ = color.r;
}
void GloScreenRGB888::DrawRectangle(const Rectangle& rect) const {// RGB 24bpp only
	uint8* p = &Locate(rect.getVertex());
	uint8 comm[3 * 4] = {
		rect.color.b, rect.color.g, rect.color.r,
		rect.color.b, rect.color.g, rect.color.r,
		rect.color.b, rect.color.g, rect.color.r,
		rect.color.b, rect.color.g, rect.color.r,
	};
	uint32 (*com)[3] = (uint32(*)[3])comm;
	for0(y, rect.height) {
		union {
			uint8* pp8;
			uint32* pp32;
		};
		pp8 = p;
		for0r(x, rect.width) {
			if (!(_IMM(pp8) & 0b11) && x >= 12) {
				pp32[0] = treat<uint32>(&comm[4 * 0]);
				pp32[1] = treat<uint32>(&comm[4 * 1]);
				pp32[2] = treat<uint32>(&comm[4 * 2]);
				x -= 3 - 1;
				pp8 += 12;
				continue;
			}
			*pp8++ = rect.color.b;
			*pp8++ = rect.color.g;
			*pp8++ = rect.color.r;
		}
		p += video_info->BytesPerScanLine;
	}
}
void GloScreenRGB888::DrawFont(const Point& disp, const DisplayFont& font) const {
	_TODO;
}
Color GloScreenRGB888::GetColor(Point p) const {
	return cast<Color>(Locate(p));
}

#elif __BITS__ == 64
#include "../prehost/atx-x64-uefi64/atx-x64-uefi64.loader/loader-graph.h"

void DrawMouseCursor(VideoControlInterface* p_vcb, Point position) {
	for0(dy, kMouseCursorHeight) for0(dx, kMouseCursorWidth) {
		if (mouse_cursor_shape[dy][dx] == '@') {
			p_vcb->DrawPoint(Point(position.x + dx, position.y + dy), Color(0xFF000000));
		}
		else if (mouse_cursor_shape[dy][dx] == '.') {
			p_vcb->DrawPoint(Point(position.x + dx, position.y + dy), Color(0xFFFFFFFF));
		}
	}
}

void EraseMouseCursor(VideoControlInterface* p_vcb, Point position, Color erase_color) {
	for0(dy, kMouseCursorHeight) for0(dx, kMouseCursorWidth) {
		if (mouse_cursor_shape[dy][dx] != ' ') {
			p_vcb->DrawPoint(Point(position.x + dx, position.y + dy), erase_color);
		}
	}
}

Cursor::Cursor(VideoControlInterface* writer, Color erase_color, Point initial_position)
	: pixel_writer_{ writer }, erase_color_{ erase_color }, position_{ initial_position }
{
	DrawMouseCursor(pixel_writer_, position_);
}

void Cursor::MoveRelative(Size2dif displacement)
{
	// ploginfo("%d %d %d %d", position_.x, position_.y, displacement.x, displacement.y);
	EraseMouseCursor(pixel_writer_, position_, erase_color_);
	stdsint _x = position_.x; _x += displacement.x;
	stdsint _y = position_.y; _y += displacement.y;
	if (displacement.x >= 0) {
		position_.x = minof(800, _x);//{TODO} 800x600
	}
	else {
		position_.x = maxof(0, _x);
	}
	if (displacement.y >= 0) {
		position_.y = minof(600, _y);//{TODO} 800x600
	}
	else {
		position_.y = maxof(0, _y);
	}
	DrawMouseCursor(pixel_writer_, position_);
}


extern FrameBufferConfig config_graph;

uint32& GloScreenARGB8888::Locate(const Point& disp) const {
	return *((uint32*)(config_graph.frame_buffer) + disp.x + disp.y * config_graph.pixels_per_scan_line);
}
uint32& GloScreenABGR8888::Locate(const Point& disp) const {
	return *((uint32*)(config_graph.frame_buffer) + disp.x + disp.y * config_graph.pixels_per_scan_line);
}

void GloScreenARGB8888::SetCursor(const Point& disp) const { _TODO; }// MAYBE unused
Point GloScreenARGB8888::GetCursor() const { _TODO return {0, 0}; }// MAYBE unused
void GloScreenARGB8888::DrawPoint(const Point& disp, Color color) const {
	Locate(disp) = cast<uint32>(color);
}
void GloScreenARGB8888::DrawRectangle(const Rectangle& rect) const {
	uint32* p = &Locate(rect.getVertex());
	for0(y, rect.height) {
		for0(x, rect.width) p[x] = rect.color.val;// cast<uint32>(rect.color);
		p += config_graph.pixels_per_scan_line;
	}
}
void GloScreenARGB8888::DrawFont(const Point& disp, const DisplayFont& font) const {
	_TODO;
}
Color GloScreenARGB8888::GetColor(Point p) const {
	return cast<Color>(Locate(p));
}



void GloScreenABGR8888::SetCursor(const Point& disp) const { _TODO; }// MAYBE unused
Point GloScreenABGR8888::GetCursor() const { _TODO return { 0, 0 }; }// MAYBE unused
void GloScreenABGR8888::DrawPoint(const Point& disp, Color color) const {
	uint32 val = 
		(_IMM(color.r) << 0) |
		(_IMM(color.g) << 8) |
		(_IMM(color.b) << 16) |
		(_IMM(color.a) << 24);
	Locate(disp) = val;
}
void GloScreenABGR8888::DrawRectangle(const Rectangle& rect) const {
	uint32 val;
	val = (_IMM(rect.color.r) << 0)
		| (_IMM(rect.color.g) << 8)
		| (_IMM(rect.color.b) << 16)
		| (_IMM(rect.color.a) << 24);
	uint32* p = &Locate(rect.getVertex());
	for0(y, rect.height) {
		for0(x, rect.width) p[x] = val;
		//cast<byte*>(p) += config.pixels_per_scan_line;
		p += config_graph.pixels_per_scan_line;
	}
}
void GloScreenABGR8888::DrawFont(const Point& disp, const DisplayFont& font) const {
	_TODO;
}
Color GloScreenABGR8888::GetColor(Point p) const {
	Color color;
	uint32 val = Locate(p);
	color.r = (val >> 0) & 0xFF;
	color.g = (val >> 8) & 0xFF;
	color.b = (val >> 16) & 0xFF;
	color.a = (val >> 24) & 0xFF;
	return color;
}


#endif

#endif
