// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: [Service] Console - Video and Mouse
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <c/driver/mouse.h>
#include <c/driver/keyboard.h>
#include "../include/console.hpp"



#if (_MCCA & 0xFF00) == 0x8600

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

void Cursor::doshow(void* _) {
	auto p = _ ? (Color*)_ : sheet_buffer;
	if (!p) return;
	for0(dy, kMouseCursorHeight) for0(dx, kMouseCursorWidth) {
		if (mouse_cursor_shape[dy][dx] == '@') {
			*p++ = 0xFF000000;// Point(position.x + dx, position.y + dy)
		}
		else if (mouse_cursor_shape[dy][dx] == '.') {
			*p++ = 0x7FFFFFFF;
		}
		else {
			*p++ = 0x00FFFFFF;
		}
	}
}

void Cursor::setSheet(LayerManager& layman, const Point& vertex) {
	sheet_buffer = (Color*)mem.allocate(kMouseCursorWidth * kMouseCursorHeight * sizeof(Color));
	doshow(sheet_buffer);
	InitializeSheet(layman, vertex, { kMouseCursorWidth,kMouseCursorHeight }, sheet_buffer);
	layman.Append(this);
}



// hand_mouse
void hand_mouse(MouseMessage mmsg) {
	// ploginfo("hand_mouse (%d, %d)", displacement_x, displacement_y);
	byte change_btns = 0;// 0RML0RML
	if (Cursor::mouse_btnl_dn != mmsg.BtnLeft) change_btns |= 0b001;
	if (Cursor::mouse_btnm_dn != mmsg.BtnMiddle) change_btns |= 0b010;
	if (Cursor::mouse_btnr_dn != mmsg.BtnRight) change_btns |= 0b100;
	if (mmsg.BtnLeft) change_btns |= 0b00010000;
	if (mmsg.BtnMiddle) change_btns |= 0b00100000;
	if (mmsg.BtnRight) change_btns |= 0b01000000;

	if ((change_btns & 0b1) && !mmsg.BtnLeft) {
		Cursor::moving_sheet = nullptr;
	}
	
	Cursor::mouse_btnl_dn = mmsg.BtnLeft;
	Cursor::mouse_btnm_dn = mmsg.BtnMiddle;
	Cursor::mouse_btnr_dn = mmsg.BtnRight;
	Point cursor_p = Cursor::global_cursor->sheet_area.getVertex();

	// normal layers
	SheetTrait* sheet = nullptr;
	if ((change_btns & 0b111) && (sheet = global_layman.getTop(cursor_p, 1))) {
		cursor_p -= sheet->sheet_area.getVertex();
		sheet->onrupt(SheetEvent::onClick, cursor_p, change_btns);
	}

	// cursor layer
	if ((mmsg.X || mmsg.Y))
		global_layman.Domove(Cursor::global_cursor, { mmsg.X,mmsg.Y });
	if (Cursor::moving_sheet) {
		global_layman.Domove(Cursor::moving_sheet, { mmsg.X,mmsg.Y });
	}
}



// ---- ---- ---- ---- . ---- ---- ---- ----

void LayerManager::Dorupt(SheetTrait* who, SheetEvent event, Point rel_p, para_list args) {
	if (event == SheetEvent::onClick) {
		byte state = para_next(args, unsigned);
		if ((state & 0b10001) == 0b10001) {
			Cursor::moving_sheet = who;
		}
	}
}

// ---- ---- ---- ---- . ---- ---- ---- ----

// GloScreen
#if 0// GloScreenRGB888

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

#elif (_MCCA & 0xFF00) == 0x8600


inline static stduint VCI_LimitX() { return global_layman.window.width; }
uint32& GloScreenARGB8888::Locate(const Point& disp) const {
	return *((uint32*)(global_layman.video_memory) + disp.x + disp.y * VCI_LimitX());
}
uint32& GloScreenABGR8888::Locate(const Point& disp) const {
	return *((uint32*)(global_layman.video_memory) + disp.x + disp.y * VCI_LimitX());
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
		p += VCI_LimitX();
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
		p += VCI_LimitX();
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
