// ASCII g++ TAB4 LF
// Attribute: 
// LastCheck: 20240218
// AllAuthor: @dosconio
// ModuTitle: Demonstration - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST

#include "../include/memoman.hpp"
#include <c/consio.h>

#ifdef _ARC_x86

use crate uni;

Paging kernel_paging;
extern stduint tmp;

_TEMP void page_init() {
	Paging::page_directory = (PageDirectory*)Memory::physical_allocate(0x1000);
	kernel_paging.Reset();
	PageDirectory& pdt = *Paging::page_directory;
	//
	for0(i, 0x400)// kernel linearea 0x00000000 ~ 0x00400000
		pdt[0][i].setMode(true, true, false, i << 12);
	pdt[0x3FF][0x3FF].setMode(true, true, false, _IMM(Paging::page_directory));// make loop PDT and do not unisym's
	for0(i, 0x400)// global linearea 0x80000000 ~ 0x80400000
		pdt[0x200][i].setMode(true, true, true, i << 12);
	//
	tmp = _IMM(Paging::page_directory);
	__asm("movl tmp, %eax\n");
	__asm("movl %eax, %cr3\n");
	__asm("movl %cr0, %eax\n");
	__asm("or   $0x80000000, %eax\n");// enable paging
	__asm("movl %eax, %cr0\n");
	rostr test_page = (rostr)"\xFF\x70[ Paging ]\xFF\x02 Test OK!\xFF\x07" + 0x80000000;
	Console.FormatShow("%s\n\r", test_page);
}


#endif
