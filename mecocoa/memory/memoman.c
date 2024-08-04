// ASCII NASM0207 TAB4 CRLF
// Attribute: CPU(x86) File(HerELF) Mode(Bits:32)
// LastCheck: 20240501
// AllAuthor: @dosconio
// ModuTitle: Memory Management
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include <c/stdinc.h>

static void* MemUptrAdvance(stduint plus)
{
	dword *const MemUptr = (dword *)0x8000052C;
	dword retv = *MemUptr;
	*MemUptr += plus;
	return (void*)retv;
}

// return the PzikAddr
//{TODO} palloc before effectice return
#undef memalloc
void* memalloc(stduint size)
{
	dword *MemUptr = (dword *)0x8000052C;
	//[1] 00010000~0005FFFF
	//[2] 00100000~00400000
	if (!size || size>(0x300000))
		return 0;	
	if (*MemUptr < 0x00060000)
	{
		if (*MemUptr + size >= 0x00060000)
			*MemUptr = 0x00100000;
		return MemUptrAdvance(size);
	}
	// assert (*MemUptr >= 0x00100000)
	if (*MemUptr + size < 0x00400000)
		return MemUptrAdvance(size);
	else return 0;
}


