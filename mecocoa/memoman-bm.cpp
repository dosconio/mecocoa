// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST

#include <c/consio.h>

use crate uni;

#if _MCCA == 0x8632
#include "../include/atx-x86-flap32.hpp"
#include "../prehost/atx-x86-flap32/multiboot2.h"
// for x86, one slice should begin with 0x100000 (above F000:FFFF)
// for x86, only consider single paging method

#elif _MCCA == 0x8664
#include "../include/atx-x64-uefi64.hpp"

#endif

static void update_avail_pointer(BmMemoman& bm, stduint head_pos, stduint last_pos, bool what) {
	// ploginfo("update_avail_pointer(0x%[32H], 0x%[32H], %d)", head_pos, last_pos, what);
	if (what) {
		MIN(bm.avail_pointer, head_pos);
	}
	else if (bm.avail_pointer == head_pos) {
		bm.avail_pointer = last_pos;
		while (!bm.bitof(bm.avail_pointer)) {
			bm.avail_pointer++;
			if (bm.avail_pointer >= (bm.size * _BYTE_BITS_)) {
				plogerro("Memory is all used");
				return;
			}
		}
	}
}

void BmMemoman::add_range(stduint head_pos, stduint last_pos, bool what) {
	// if (!map_ready) ploginfo("add_range(0x%[32H], 0x%[32H])", head_pos, last_pos);
	if (head_pos >= last_pos || head_pos >= 0x100000 || last_pos >= 0x100000) return;
	stduint times = last_pos - head_pos;
	stduint curr_pos = head_pos;
	while (times && (curr_pos & 0b111)) {
		this->setof(curr_pos, what);
		curr_pos++;
		times--;
	}
	while (times >= _BYTE_BITS_) {
		cast<byte*>(this->offs)[curr_pos / _BYTE_BITS_] = what ? 0xFF : 0x00;
		curr_pos += _BYTE_BITS_;
		times -= _BYTE_BITS_;
	}
	while (times) {
		this->setof(curr_pos, what);
		curr_pos++;
		times--;
	}
	update_avail_pointer(self, head_pos, last_pos, what);
}
void BmMemoman::dump_avail_memory()
{
	bool last_stat = false;
	stduint last_index = 0;
	outsfmt("[Memoman] dump avail memory:\n\r");
	for0(i, 0x10000) {
		bool b = this->bitof(i);
		if (b != last_stat) {
			if (!last_stat) {
				last_index = i;
				last_stat = b;
			}
			else {
				outsfmt("- 0x%[x]..0x%[x] \n\r", last_index * 0x1000, i * 0x1000);
				last_stat = b;
			}
		}
	}
	if (last_stat == 1) {
		outsfmt("- 0x%[x]..0x%[x] \n\r", last_index * 0x1000, 0x2000 * 0x1000);
	}
}
