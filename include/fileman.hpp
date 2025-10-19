#ifndef FILEMAN_HPP_
#define FILEMAN_HPP_

// drive  0:0=h[0]                0:1=h[5]
// device     h[1~4]                  h[6~9]
// subdev 0~15/16~31/32~47/48~63  64~...

bool waitfor(int mask, int val, int timeout_second);

#define MAX_DRIVES 2 // only for primary IDE
#define NR_PART_PER_DRIVE    4 // 4 primary partitions per drive
#define NR_SUB_PER_PART     16 // 16 logical partitions per primary partition

#define NR_SUB_PER_DRIVE    (NR_SUB_PER_PART * NR_PART_PER_DRIVE)// 64
#define NR_PRIM_PER_DRIVE    (NR_PART_PER_DRIVE + 1) // 5

#define MAX_PRIM        (MAX_DRIVES * NR_PRIM_PER_DRIVE - 1) // 9. prim_dev ranges in hd[0-9] (h[0] h[1~4], h[5], h[6~9])
#define MAX_SUBPARTITIONS    (NR_SUB_PER_DRIVE * MAX_DRIVES)

void DEV_Init();
void serv_dev_hd_loop();
void serv_file_loop();

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
#define DEV_MAJOR_SHIFT     8
#define DEV_MAKE_DEV(a,b)   ((a << DEV_MAJOR_SHIFT) | b)
#define DEV_MAJOR(x)        ((x >> DEV_MAJOR_SHIFT) & 0xFF)
#define DEV_MINOR(x)        (x & 0xFF)
#define    MINOR_hd1a       0x10// should greater than MAX_PRIM
#define    MINOR_hd2a       (MINOR_hd1a+NR_SUB_PER_PART)

struct Harddisk_PATA_Paged : public Harddisk_PATA {
	Harddisk_PATA_Paged(byte _id = 0, HarddiskType type = HarddiskType::ATA) : Harddisk_PATA(_id, type) {}
	virtual bool Read(stduint BlockIden, void* Dest);
	virtual bool Write(stduint BlockIden, const void* Sors);
	Slice GetPartEntry(usize device);
};

extern Harddisk_PATA* disks[MAX_DRIVES];

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
};

#define	O_CREAT 0b01
#define	O_RDWR  0b10

#endif
