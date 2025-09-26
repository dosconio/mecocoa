// ASCII g++ TAB4 CRLF
// Attribute: Arch(AMD64)
// AllAuthor: @ArinaMgk
// ModuTitle: Kernel
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#include <cpp/unisym>
#include <cpp/Device/_Video.hpp>


using namespace uni;
#include "atx-x64-uefi64.loader/loader-graph.h"
#include "../../include/atx-x64-uefi64.hpp"


byte _b_vcb[byteof(VideoControlBlock)];

extern "C" byte BSS_ENTO, BSS_ENDO;
FrameBufferConfig config_graph;

extern "C" //__attribute__((ms_abi))
void _entry(const UefiData& uefi_data)
{
	MemSet(&BSS_ENTO, &BSS_ENDO - &BSS_ENTO, 0);

	config_graph = uefi_data;

	GloScreenARGB8888 vga_ARGB8888;
	GloScreenABGR8888 vga_ABGR8888;
	VideoControlInterface* screen;

	auto ppp = ((uint32*)uefi_data.frame_buffer);
	ppp += uefi_data.pixels_per_scan_line * 16;
	if (uefi_data.pixel_format == PixelFormat::ARGB8888) for0(i, uefi_data.pixels_per_scan_line) {
		ppp[i] = 0xFFFFFFFF;
	}

	switch (uefi_data.pixel_format) {
	case PixelFormat::ARGB8888:
		screen = (&vga_ARGB8888);
		break;
	case PixelFormat::ABGR8888:
		screen = &vga_ABGR8888;
		break;
	default:
		loop _ASM("hlt");
	}

	VideoControlBlock* p_vcb = new (_b_vcb) VideoControlBlock\
		((pureptr_t)uefi_data.frame_buffer, *screen);

	p_vcb->Draw(Rectangle(
		Point(0, 0),
		Size2(uefi_data.horizontal_resolution, uefi_data.vertical_resolution),
		Color::White
	));

	p_vcb->setMode(uefi_data.pixel_format,
		uefi_data.horizontal_resolution,
		uefi_data.vertical_resolution
	);

	p_vcb->Draw(Rectangle(Point(100, 100), Size2(600, 100), Color::Red));
	loop _ASM("hlt");
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
		//cast<byte*>(p) += config.pixels_per_scan_line;
	}
}
void GloScreenARGB8888::DrawFont(const Point& disp, const DisplayFont& font) const {
	_TODO;
}
Color GloScreenARGB8888::GetColor(Point p) const {
	//return cast<Color>(Locate(p));
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


void operator delete(void*) {}
void operator delete(void* ptr, unsigned long size) noexcept { _TODO }
_ESYM_C void __cxa_pure_virtual(void) {}
_ESYM_C void* memset(void* dest, int val, size_t count) {
	auto d = (byte*)dest;
	for0(i, count) d[i] = (byte)val;
	return dest;
}

