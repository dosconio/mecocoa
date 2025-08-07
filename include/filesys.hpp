// minix-like

#include "cpp/trait/StorageTrait.hpp"
#include "cpp/trait/FilesysTrait.hpp"

using namespace uni;

class FilesysMinix : public FilesysTrait
{
	

public:
	FilesysMinix(StorageTrait& s) {
		storage = &s;
	}

	
};

