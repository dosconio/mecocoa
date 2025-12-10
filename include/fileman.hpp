#ifndef FILEMAN_HPP_
#define FILEMAN_HPP_

// The version just consider primary IDE

bool waitfor(stduint mask, stduint val, stduint timeout_second);

#define	DRV_OF_DEV(dev) (dev <= MAX_PRIM ? \
	dev / NR_PRIM_PER_DRIVE : \
	(dev - MINOR_hd1a) / NR_SUB_PER_DRIVE)

#define MAX_DRIVES 2 // only for primary IDE
#define NR_PART_PER_DRIVE    4 // 4 primary partitions per drive
#define NR_SUB_PER_PART     16 // 16 logical partitions per primary partition

#define NR_SUB_PER_DRIVE    (NR_SUB_PER_PART * NR_PART_PER_DRIVE)// 64
#define NR_PRIM_PER_DRIVE    (NR_PART_PER_DRIVE + 1) // 5

#define MAX_PRIM        (MAX_DRIVES * NR_PRIM_PER_DRIVE - 1) // 9. prim_dev ranges in hd[0-9] (h[0] h[1~4], h[5], h[6~9])
#define MAX_SUBPARTITIONS    (NR_SUB_PER_DRIVE * MAX_DRIVES)

#define MINOR_hd1a       0x10// should greater than MAX_PRIM
#define MINOR_hd2a       (MINOR_hd1a+NR_SUB_PER_PART)
#define MINOR_hd3a       (MINOR_hd1a+NR_SUB_PER_PART*2)
#define MINOR_hd4a       (MINOR_hd1a+NR_SUB_PER_PART*3)
#define MINOR_hd5a       (MINOR_hd1a+NR_SUB_PER_DRIVE)
#define MINOR_hd6a       (MINOR_hd1a+NR_SUB_PER_DRIVE+NR_SUB_PER_PART*1)

void DEV_Init();


enum MajorDevice {
	DEV_NULL = 0,
	DEV_FLOPPY,
	DEV_CDROM,
	DEV_HDD,
	DEV_TTY,
	DEV_SCSI,
	//
	DEV_MAX
};



struct Harddisk_PATA_Paged : public Harddisk_PATA {
	Harddisk_PATA_Paged(byte _id = 0, HarddiskType type = HarddiskType::ATA) : Harddisk_PATA(_id, type) {}
	virtual bool Read(stduint BlockIden, void* Dest);
	virtual bool Write(stduint BlockIden, const void* Sors);
};
extern Harddisk_PATA* disks[MAX_DRIVES];

struct Partition : public StorageTrait
{
	int device;
	Slice slice = {0};
	//
	Partition(int dev) : device(dev) { Block_Size = _TEMP 512; }
	virtual bool Read(stduint BlockIden, void* Dest) override;
	virtual bool Write(stduint BlockIden, const void* Sors) override;
	virtual stduint getUnits() override {
		if (!slice.address && !slice.length) renew_slice();
		return slice.length;
	}
	Slice getSlice() {
		if (!slice.address && !slice.length) renew_slice();
		return slice;
	}
	virtual int operator[](uint64 bytid) override { _TODO return 0; }
	//
protected:
	
	void renew_slice();
};


// for IDE0:0 and IDE0:1
// Should be done by syscall. But here is linked as one program
inline static Harddisk_PATA* IndexDisk(unsigned dev) {
	unsigned drv_id = dev / NR_SUB_PER_DRIVE;
	if (drv_id >= MAX_DRIVES) return NULL;
	return disks[drv_id];
}

enum class FiledevMsg {
	TEST,
	RUPT,
	CLOSE,
	READ,
	WRITE,
	GETPS,// GetPartitionSlice aka geometry
};// for fileman-hd

enum class FilemanMsg {
	TEST,
	RUPT,
	OPEN,
	CLOSE,
	READ,
	WRITE,
	REMOVE,
};

#define	O_CREAT  0b001
#define	O_RDWR   0b010
#define	O_TRUNC  0b100 // truncate file

struct inode;
struct FileDescriptor {
	int fd_mode; /**< R or W */
	int fd_pos; /**< Current position for R/W. */
	inode* fd_inode; /**< Ptr to the i-node */
};

#define	INVALID_INODE 0
#define	ROOT_INODE    1

#define dev_domain_bits 16
#define	MAKE_DEV(a,b)		((a << dev_domain_bits) | b)
inline static stduint get_drv_pid(u32 dev) {
	// 0b1111 is for
	u32 drv = dev >> dev_domain_bits;
	return drv;
}

bool strip_path(char* filename, const char* pathname, inode** ppinode);

#endif
