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

#elif __BITS__ == 64

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
		if (mouse_cursor_shape[dy][dx] == ' ') {
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


#endif

#endif
