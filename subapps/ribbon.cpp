#include "aaaaa.h"
#include "c/consio.h"

using namespace uni;

static constexpr stduint kRibbonClientHeight = 30;
static constexpr stduint kFormChromeHeight = 19;
static constexpr stduint kStartButtonWidth = 54;

static Color RColor(uint32 argb)
{
	return argb;
}

static void FillRect(Color* buffer, stduint pitch, stduint width, stduint height,
	stduint x, stduint y, stduint w, stduint h, Color color)
{
	if (!buffer || x >= width || y >= height) return;
	if (x + w > width) w = width - x;
	if (y + h > height) h = height - y;
	for0(row, h) {
		Color* p = buffer + (y + row) * pitch + x;
		for0(col, w) *p++ = color;
	}
}

static void DrawStartLogo(Color* buffer, stduint pitch, stduint height, stduint x, stduint y)
{
	FillRect(buffer, pitch, pitch, height, x + 0, y + 0, 7, 7, RColor(0xFFEA3323));
	FillRect(buffer, pitch, pitch, height, x + 8, y + 0, 7, 7, RColor(0xFF29A329));
	FillRect(buffer, pitch, pitch, height, x + 0, y + 8, 7, 7, RColor(0xFF2454C6));
	FillRect(buffer, pitch, pitch, height, x + 8, y + 8, 7, 7, RColor(0xFFF6C431));
}

static void DrawRibbon(Color* buffer, stduint width, stduint height)
{
	FillRect(buffer, width, width, height, 0, 0, width, height, RColor(0xFFC0C0C0));
	FillRect(buffer, width, width, height, 0, 0, width, 1, RColor(0xFFFFFFFF));
	FillRect(buffer, width, width, height, 0, 1, width, 1, RColor(0xFFDFDFDF));

	stduint button_y = 3;
	stduint button_h = height > 6 ? height - 6 : height;
	FillRect(buffer, width, width, height, 3, button_y, kStartButtonWidth, button_h, RColor(0xFFC0C0C0));
	FillRect(buffer, width, width, height, 3, button_y, kStartButtonWidth, 1, RColor(0xFFFFFFFF));
	FillRect(buffer, width, width, height, 3, button_y, 1, button_h, RColor(0xFFFFFFFF));
	FillRect(buffer, width, width, height, 4, button_y + 1, kStartButtonWidth - 2, 1, RColor(0xFFDFDFDF));
	FillRect(buffer, width, width, height, 4, button_y + 1, 1, button_h - 2, RColor(0xFFDFDFDF));
	FillRect(buffer, width, width, height, 3 + kStartButtonWidth - 1, button_y, 1, button_h, RColor(0xFF000000));
	FillRect(buffer, width, width, height, 3, button_y + button_h - 1, kStartButtonWidth, 1, RColor(0xFF000000));
	FillRect(buffer, width, width, height, 3 + kStartButtonWidth - 2, button_y + 1, 1, button_h - 2, RColor(0xFF808080));
	FillRect(buffer, width, width, height, 4, button_y + button_h - 2, kStartButtonWidth - 2, 1, RColor(0xFF808080));

	DrawStartLogo(buffer, width, height, 22, button_y + (button_h - 16) / 2);
}

static Size2 GetScreenSize()
{
	Size2 screen = {};
	if (sys_get_screen_size(&screen) != 0 || screen.x < 80 || screen.y < 60) {
		screen = Size2(640, 480);
	}
	return screen;
}

static stdsint CreateRibbonForm(Size2 screen, Color** out_buffer)
{
	stduint form_height = kRibbonClientHeight + kFormChromeHeight;
	Rectangle rect{
		Point(0, screen.y > form_height ? screen.y - form_height : 0),
		Size2(screen.x, form_height)
	};

	stdsint form_id = sys_create_form(~_IMM0, &rect, GraphicFormStyle_Titleless);
	if (form_id < 0) return form_id;

	stduint client_w = rect.width - 2;
	stduint client_h = rect.height - kFormChromeHeight;
	Color* buffer = (Color*)malloc(client_w * client_h * sizeof(Color));
	if (!buffer) {
		sys_close_form(form_id);
		return -1;
	}

	sys_set_form_buffer(form_id, buffer);
	DrawRibbon(buffer, client_w, client_h);
	sys_update_form(form_id, nullptr);
	sys_set_timer(form_id, 10000);

	*out_buffer = buffer;
	return form_id;
}

int main(int argc, char** argv)
{
	#if __BITS__ == 64
	_preprocess();
	#endif

	Size2 screen = GetScreenSize();
	Color* buffer = nullptr;
	stdsint form_id = CreateRibbonForm(screen, &buffer);
	if (form_id < 0) {
		outsfmt("Ribbon: Failed to create form.\r\n");
		return -1;
	}

	SheetMessage smsg;
	while (sys_fetch_msg(form_id, true, &smsg)) {
		if (smsg.event != SheetEvent::onTimer) continue;

		Size2 next_screen = GetScreenSize();
		if (next_screen.x == screen.x && next_screen.y == screen.y) continue;

		screen = next_screen;
		sys_close_form(form_id);
		if (buffer) free(buffer);
		buffer = nullptr;
		form_id = CreateRibbonForm(screen, &buffer);
		if (form_id < 0) {
			outsfmt("Ribbon: Failed to recreate form.\r\n");
			return -1;
		}
	}

	if (buffer) free(buffer);
	return 0;
}
