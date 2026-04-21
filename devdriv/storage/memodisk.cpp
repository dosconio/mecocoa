// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: Disk - Memory
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include <c/format/filesys.h>

static stduint next_id = 0;

static byte _FOLLOW_VHD[] = {
	#if (_MCCA & 0xFF00) == 0x1000
	#if __BITS__ == 32
	#embed "../../prehost/qemuvirt-r32/fatvhd.ignore"
	#elif __BITS__ == 64
	#embed "../../prehost/qemuvirt-r64/fatvhd.ignore"
	#endif
	#else
	0
	#endif
};
// x64uefi is in UEFI Area
static_assert(sizeof(_FOLLOW_VHD) > 0);


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
			delete[](byte*)address;
			address = nullptr;
		}
	}

	virtual PartitionSlice getSlice(stduint dev = 0) override {
		PartitionSlice slice = StorageTrait::getSlice(dev);
		slice.sys_id = part_type;
		return slice;
	}
};

Vector<Memodisk*> mem_disks;


struct Harddisk_Memdisk_Paged : public StorageTrait {
	stduint id;
	Harddisk_Memdisk_Paged(byte _id = 0) : id(_id) {}
	virtual bool Read(stduint BlockIden, void* Dest);
	virtual bool Write(stduint BlockIden, const void* Sors);
};
bool Harddisk_Memdisk_Paged::Read(stduint BlockIden, void* Dest) {
	if (Taskman::CurrentPID() == Task_Memdisk_Serv) {
		Memodisk* mdisk = nullptr;
		for0(i, mem_disks.Count()) {
			if (mem_disks[i] && mem_disks[i]->id == id) {
				mdisk = mem_disks[i];
				break;
			}
		}
		return mdisk ? mdisk->Read(BlockIden, Dest) : false;
	}
	stduint to_args[2];
	to_args[0] = id;
	to_args[1] = BlockIden;
	syssend(Task_Memdisk_Serv, sliceof(to_args), _IMM(FiledevMsg::READ));
	// Receive ACK before data transfer
	stduint ack;
	sysrecv(Task_Memdisk_Serv, &ack, sizeof(ack));
	if (!ack) return false;
	sysrecv(Task_Memdisk_Serv, Dest, Block_Size);
	return true;
}

bool Harddisk_Memdisk_Paged::Write(stduint BlockIden, const void* Sors) {
	if (Taskman::CurrentPID() == Task_Memdisk_Serv) {
		Memodisk* mdisk = nullptr;
		for0(i, mem_disks.Count()) {
			if (mem_disks[i] && mem_disks[i]->id == id) {
				mdisk = mem_disks[i];
				break;
			}
		}
		return mdisk ? mdisk->Write(BlockIden, Sors) : false;
	}
	stduint to_args[2];
	to_args[0] = id;
	to_args[1] = BlockIden;
	syssend(Task_Memdisk_Serv, sliceof(to_args), _IMM(FiledevMsg::WRITE));
	// Receive ACK before data transfer
	stduint ack;
	sysrecv(Task_Memdisk_Serv, &ack, sizeof(ack));
	if (!ack) return false;
	syssend(Task_Memdisk_Serv, Sors, Block_Size);
	return true;
}


#if 1
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

void serv_dev_mem_loop() {
	stduint sig_type = 0, sig_src;
	stduint args[4];
	stduint ret;
	byte* rw_buffer = new byte[512];
	Memodisk* mdisk;
	while (true) {
		switch ((FiledevMsg)sig_type) {
		case FiledevMsg::TEST:// (no-feedback)
			if (1) {
				#if (_MCCA & 0xFF00) == 0x1000
				ploginfo("[Memdisk] Default FATVHD Size: %[x]", sizeof(_FOLLOW_VHD));
				auto dev0 = open(sliceof(_FOLLOW_VHD), FILESYS_FAT32_LBA);
				printlog(dev0 >= 0 ? _LOG_INFO : _LOG_ERROR, "[Memdisk] Created Memdisk %u, trying FAT32", dev0);
				if (auto fs = Filesys::Mount(*locate(0), 0, "/md0")) {//{} "/dev/md0"
					ploginfo("[Memdisk] Loaded %s", fs->name);
				}
				#endif
			}
			break;
		case FiledevMsg::RUPT:// (usercall-forbidden, no feedback)
			break;
		case FiledevMsg::CLOSE:// [diskno]
			// TODO
			break;
		case FiledevMsg::READ:// [diskno, lba]
		{
			mdisk = locate(args[0]);
			stduint ack = (mdisk && mdisk->Read(args[1], rw_buffer)) ? 1 : 0;
			if (sig_src) syssend(sig_src, &ack, sizeof(ack));
			if (ack && sig_src) syssend(sig_src, rw_buffer, mdisk->Block_Size);
			break;
		}
		case FiledevMsg::WRITE:// [diskno, lba]
		{
			mdisk = locate(args[0]);
			stduint ack = mdisk ? 1 : 0;
			if (sig_src) syssend(sig_src, &ack, sizeof(ack));
			if (ack && sig_src) {
				sysrecv(sig_src, rw_buffer, mdisk->Block_Size);
				mdisk->Write(args[1], rw_buffer);
			}
			break;
		}
		case FiledevMsg::GETPS:
			// UNSUPPORTED
			break;
		case FiledevMsg::OPEN://(memaddr, memolen, sectype) | memdisk only params
			ret = open((void*)args[0], args[1], args[2]);
			ploginfo("%u wo Create Memdisk %d", sig_src, ret);
			syssend(sig_src, &ret, sizeof(ret));
			break;

		}
		sysrecv(ANYPROC, sliceof(args), &sig_type, &sig_src);
	}

}

#endif
