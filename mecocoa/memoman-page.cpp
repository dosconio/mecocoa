// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#if CONFIG_ENABLE_MMU

bool Memory::MapPages(Paging& paging, stduint linear_address,
	stduint physical_address, stduint length,
	PageSizeShift page_size, stduint properties) {
	return paging.Map(linear_address, physical_address, length,
		page_size, properties);
}

bool Memory::UnmapPages(Paging& paging, stduint linear_address,
	stduint length) {
	return paging.Unmap(linear_address, length);
}

#endif
