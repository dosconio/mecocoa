// ASCII C/C++ TAB4 LF
// LastCheck: 20240219
// AllAuthor: @dosconio
// ModuTitle: Kernel Flap-32
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#ifndef _MCCA_CONST
#define _MCCA_CONST

#define ADDR_IDT32 0x0800
#define PAGE_SIZE  0x1000

#define GDTDptr        0x0504
#define IVTDptr        0x050A
#define G_CountSeconds (*(dword*)0x0524)
#define G_CountMiSeconds (*(word*)0x0528)

#define Linear 0x80000000

#define SegCode (8*2)

#endif
