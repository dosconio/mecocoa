// ASCII C TAB4 LF
// Attribute: Bits(32)
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Flat-32 Console Text I/O Driver
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// BaseOn   : */n_video.a
//{TEMP} To be abstracted into unisym.

#include "../include/console.h"
#include <c/stdinc.h>
#include <c/board/IBM.h>

static byte* const _VideoBuf = (byte*)_VIDEO_ADDR_BUFFER + 0x80000000;
const static word _CharsPerLine = 80;
const static word _BytesPerLine = _CharsPerLine * 2;
const static word _LinesPerScreen = 25;
const static word _ScreenSize = _BytesPerLine * _LinesPerScreen;

void scrrol(word lines)
{
	if (!lines) return;
	MIN(lines, _LinesPerScreen);
	word *sors = (word *)_VideoBuf + _CharsPerLine * lines;// Add for Pointer!
	word *dest = (word *)_VideoBuf;
	forp (dest, _CharsPerLine * (_LinesPerScreen - lines)) *dest = *sors++;
	forp (dest, _CharsPerLine * lines) 
		*dest = 0x0720;//{TEMP} the new lines are of 'white on black' color
}

void outtxt(const char* str, dword len)
{
	static byte attr = 0;
	static byte attr_enable = 0;

	word posi = curget()*2;
	byte chr;

	for0(i, len) {
		chr = (byte)*str++;
		switch (chr)
		{
		case (byte)'\xFF':// 20240217-ALICE's FF Method
			if (len)
				attr = *str++, attr_enable = (attr != (byte)'\xFF'), len--;
			break;
		case '\r':
			posi -= posi % _BytesPerLine; //= posi / _BytesPerLine * _BytesPerLine;
			break;
		case '\n':// down
			posi += _BytesPerLine;
			break;
		case '\b':// left
			posi -= 2;
			break;
		case '\x01':// next
			posi += 2;
			break;
		case '\x02':// up
			posi -= _BytesPerLine;
			break;
		default:
			_VideoBuf[posi++] = chr;
			if (attr_enable)
				_VideoBuf[posi++] = attr;
			else
				posi++;
			break;
		}
		if (!chr) break;
		while (posi >= _ScreenSize) {
			scrrol(1);
			posi -= _BytesPerLine;
		}
	}
	curset(posi / 2);
	_crt_out_cnt += len;
}

