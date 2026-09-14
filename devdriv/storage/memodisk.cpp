// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: Disk - Memory
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include <c/format/filesys.h>

_ESYM_C void R_MEMDISK_INIT();

#if 1
__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_MEMDISK{
	.init = R_MEMDISK_INIT,
	.name = "DISK-MEM",
};
#endif

static stduint next_id = 0;

__attribute__((section(".extdata")))
static byte _FOLLOW_VHD[] = {
	#if   (_MCCA & 0xFF00) == 0x1000
	#if __BITS__ == 32
	#embed "../../prehost/qemuvirt-r32/fatvhd.ignore"
	#elif __BITS__ == 64
	#embed "../../prehost/qemuvirt-r64/fatvhd.ignore"
	#endif
	#elif _MCCA == 0x8632
	#embed "../../prehost/atx-x86-flap32/fatvhd.ignore"
	#else
	0
	#endif
};
// x64uefi is in UEFI Area
static_assert(sizeof(_FOLLOW_VHD) > 0);
#if _MCCA == 0x8664 && defined(_UEFI)
extern UefiData uefi_data;
#endif

class Memodisk : public MemoryBlockDevice {
private:
	bool auto_allocated;
	stduint part_type;
public:
	stduint id;

public:
	// Constructor 1: Automatically allocate memory for the RAM disk
	Memodisk(stduint parttype, stduint sectors, stduint block_size = 512)
		: MemoryBlockDevice({ (stduint)new byte[sectors * block_size], sectors * block_size }, nullptr, block_size), auto_allocated(true), part_type(parttype)
	{
		// Zero out the allocated memory to avoid garbage data
		byte* ptr = (byte*)address;
		MemSet(ptr, 0, sectors * block_size);
	}
	// Constructor 2: Use an existing memory buffer (e.g., loaded by bootloader/Initrd)
	Memodisk(stduint parttype, void* memaddr, stduint sectors, stduint block_size = 512)
		: MemoryBlockDevice({ (stduint)memaddr, sectors * block_size }, nullptr, block_size), auto_allocated(false), part_type(parttype)
	{
	}

	virtual ~Memodisk() {
		if (auto_allocated && address) {
			mfree(address);
		}
	}

	virtual PartitionSlice getSlice(stduint dev = 0) override {
		PartitionSlice slice = StorageTrait::getSlice(dev);
		slice.sys_id = part_type;
		return slice;
	}
};

Vector<Memodisk*> mem_disks;


static Memodisk* locate(stduint diskno) {
	for0(i, mem_disks.Count()) {
		if (mem_disks[i] && mem_disks[i]->id == diskno) {
			return mem_disks[i];
		}
	}
	return nullptr;
}
static stdsint open(void* memaddr, stduint memolen, stduint sectype) {// memdisk only params
	auto mdisk = new Memodisk(sectype, memaddr, memolen / 512);
	if (!mdisk) return -1;
	mdisk->id = next_id++;
	mem_disks.Append(mdisk);
	return mdisk->id;
}

static bool read(stduint diskno, stduint lba, void* buffer) {
	Memodisk* mdisk = locate(diskno);
	return mdisk && mdisk->Read(lba, buffer);
}

void R_MEMDISK_INIT() {
	#if (_MCCA & 0xFF00) == 0x1000 || _MCCA == 0x8632
	ploginfo("[Memdisk] Default FATVHD Size: %[x]", sizeof(_FOLLOW_VHD));
	stduint sectype = FILESYS_FAT32_LBA;
	auto dev0 = open(sliceof(_FOLLOW_VHD), sectype);
	printlog(dev0 >= 0 ? _LOG_INFO : _LOG_ERROR, "[Memdisk] Created Memdisk %u, trying FAT", dev0);
	if (auto fs = Filesys::Mount(*locate(0), 0, "/md0")) {
		ploginfo("[Memdisk] Loaded %s", fs->name);
	}
	#elif _MCCA == 0x8664 && defined(_UEFI)
	auto dev0 = open(uefi_data.fatvhd_addr, 32 * 1024 * 1024, FILESYS_FAT32_LBA);
	printlog(dev0 >= 0 ? _LOG_INFO : _LOG_ERROR, "[Memdisk] Created Memdisk %u, trying FAT32", dev0);
	if (auto fs = Filesys::Mount(*locate(0), 0, "/md0")) {
		ploginfo("[Memdisk] Loaded %s", fs->name);
	}
	#endif
}

