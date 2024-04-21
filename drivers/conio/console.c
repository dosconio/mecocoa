// ASCII C TAB4 LF
// Attribute: Bits(32)
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Flat-32 Console Text I/O Driver
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
// BaseOn   : */n_video.a
//{TEMP} To be abstracted into unisym.
#include <alice.h>
#include "../../include/console.h"
#include <x86/interface.x86.h>

//
static byte* const _VideoBuf = (unsigned char*)0x800B8000;
const static word _CharsPerLine = 80;
const static word _BytesPerLine = _CharsPerLine * 2;
const static word _LinesPerScreen = 25;
const static word _ScreenSize = _BytesPerLine * _LinesPerScreen;

static char _tab_dec2hex[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

void curset(word posi)
{
	outpb(0x03D4, 0x0E);
	outpb(0x03D5, posi>>8);
	outpb(0x03D4, 0x0F);
	outpb(0x03D5, posi&0xFF);
}

word curget(void)
{
	word ret;
	outpb(0x03D4, 0x0E);
	ret = innpb(0x03D5) << 8;
	outpb(0x03D4, 0x0F);
	ret |= innpb(0x03D5);
	return ret;
}

void scrrol(word lines)
{
	if (!lines) return;
	if (lines > _LinesPerScreen) lines = _LinesPerScreen;
	word *sors = (word *)_VideoBuf + _CharsPerLine * lines;// Add for Pointer!
	word *dest = (word *)_VideoBuf;
	stduint i = 0;
	for (i = 0; i < _ScreenSize - _BytesPerLine * lines; i += 2)
		*dest++ = *sors++;// -TODO> memset
	for (; i < _ScreenSize; i += 2)
		*dest++ = 0x0720;//{TEMP} the new lines are of 'white on black' color
}

void outtxt(const char* str, dword len)
{
	word posi = curget()*2;
	byte chr;
	byte attr = 0;
	byte attr_enable = 0;
	while ((len--) && (chr = (byte)*str++)) {
		switch (chr)
		{
		case (byte)'\xFF':// 20240217-ALICE's FF Method
			attr_enable = 1;
			if (len && (attr = *str++))
				attr_enable = 1, len--;
			else
				chr = 0;
			break;
		case '\r':
			posi -= posi % _BytesPerLine; //= posi / _BytesPerLine * _BytesPerLine;
			break;
		case '\n':
			posi += _BytesPerLine;
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
	curset(posi/2);
}

void outc(const char chr)
{
	outtxt(&chr, 1);
}

//{TEMP} always align to right
void outi8hex(const byte inp)
{
	byte val = inp;
	char buf[2];
	for (stduint i = 0; i < 2; i++)
	{
		buf[1 - i] = _tab_dec2hex[val & 0xF];
		val >>= 4;
	}
	outtxt(buf, 2);
}

// Replacement of DbgEcho16
void outi16hex(const word inp)
{
	word val = inp;
	char buf[4];
	for (stduint i = 0; i < 4; i++)
	{
		buf[3 - i] = _tab_dec2hex[val & 0xF];
		val >>= 4;
	}
	outtxt(buf, 4);
}

// Replacement of DbgEcho32
void outi32hex(const dword inp)
{
	dword val = inp;
	char buf[8];
	for (stduint i = 0; i < 8; i++)
	{
		buf[7 - i] = _tab_dec2hex[val & 0xF];
		val >>= 4;
	}
	outtxt(buf, 8);
}

void outf(const char *str, ...);
