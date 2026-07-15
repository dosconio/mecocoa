// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: Disk - Floppy
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include <c/storage/floppy.h>
#include <c/format/filesys.h>

_ESYM_C void R_FLP_INIT();

#if _MCCA == 0x8632
#if 1
__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_FLP{
	.init = R_FLP_INIT,
	.name = "DISK-FLOPPY",
};
#endif


struct FloppyInfo {
	bool has_drive_a;
	bool has_drive_b;
	byte type_a;
	byte type_b;
	int count;
};

FloppyInfo DetectFloppyDrives() {
	FloppyInfo info = {false, false, 0, 0, 0};
	outpb(0x70, 0x10); 
	byte cmos_val = innpb(0x71);
	info.type_a = cmos_val >> 4;
	info.type_b = cmos_val & 0x0F;
	if (info.type_a != 0) {
		info.has_drive_a = true;
		info.count++;
	}
	if (info.type_b != 0) {
		info.has_drive_b = true;
		info.count++;
	}
	return info;
}

using namespace uni;

FloppyDisk* floppies[2] = { nullptr, nullptr };
static char flp_buf[sizeof(FloppyDisk) * 2];
static char* floppy_sector = nullptr;

static byte flp_lock = 1;

void Handint_FLP()
{
	if (flp_lock) {
		IC.SendEOI(IRQ_Floppy);
		return;
	}
	flp_lock = 1;
	rupt_proc(Task_Flp_Serv, IRQ_Floppy);
	IC.SendEOI(IRQ_Floppy);
}

static bool flp_int_wait() {
	CommMsg msg = {};
	sysrecv(INTRUPT, (&msg), 0);
	return true;
}

static void flp_rw_foreback() { flp_lock = 0; }

void uni::FloppyDisk::Reset() {
	// Reset controller via DOR (Digital Output Register)
	outpb(PORT_FDC_DOR, 0x00);
	for (volatile int i = 0; i < 10000; i++) _TEMP;
	flp_lock = 0;
	outpb(PORT_FDC_DOR, 0x0C); // Enable DMA/INT, clear Reset
	motor_state = false;

	if (fn_int_wait) fn_int_wait(); // ignore timeout on reset

	// Sense interrupt status for all 4 drives to clear controller status
	for (int i = 0; i < 4; i++) {
		byte st0, cyl;
		SenseInt(st0, cyl);
	}

	// Configure data rate
	outpb(PORT_FDC_CCR, DATA_RATE);

	// Specify drive timing
	WriteCmd(FDC_CMD_SPECIFY);
	WriteCmd(0xDF); // SRT = 3ms, HUT = 240ms
	WriteCmd(0x02); // HLT = 16ms, ND = 0 (DMA mode)

	flp_lock = 0;
	// Recalibrate drive
	Recalibrate();
}

static void dma_xfer(int channel, stduint phys_addr, stduint length, bool read) {
	byte mode = read ? 0x46 : 0x4A; // 0x46 for read, 0x4A for write
	outpb(0x0A, 4 + channel);       // mask channel
	outpb(0x0C, 0);                 // clear flip-flop
	outpb(0x0B, mode);              // set mode
	outpb(0x04, phys_addr & 0xFF);         // address low
	outpb(0x04, (phys_addr >> 8) & 0xFF);  // address high
	outpb(0x81, (phys_addr >> 16) & 0xFF); // page register
	outpb(0x0C, 0);                 // clear flip-flop again
	stduint count = length - 1;
	outpb(0x05, count & 0xFF);
	outpb(0x05, (count >> 8) & 0xFF);
	outpb(0x0A, channel);           // unmask channel
}

bool uni::FloppyDisk::Read(stduint BlockIden, void* Dest) {
	if (BlockIden >= getUnits()) return false;

	byte cyl, head, sec;
	LBA2CHS(BlockIden, cyl, head, sec);

	Motor(true);
	asserv(fn_feedback)();

	// Configure data transfer rate dynamically based on drive type
	outpb(PORT_FDC_CCR, DATA_RATE);

	// Setup ISA DMA Channel 2
	dma_xfer(2, (stduint)floppy_sector, 512, true);

	WriteCmd(FDC_CMD_READ_DATA);
	WriteCmd((head << 2) | id); 
	WriteCmd(cyl);
	WriteCmd(head);
	WriteCmd(sec);
	WriteCmd(0x02); // 512 Bytes/Sector
	WriteCmd(SECTORS_PER_TRACK);
	WriteCmd(GAP3_LENGTH);
	WriteCmd(0xFF); // DTL

	if (react_type == ReactType::Rupt && fn_int_wait) {
		fn_int_wait();
	} else {
		for (volatile int i = 0; i < 100000; i++) _TEMP;
	}

	// Read 7 Status Bytes after operation completion
	byte st0 = ReadData();
	for (int i = 0; i < 6; i++) ReadData(); 

	Motor(false);

	// Check for errors in ST0
	if ((st0 & 0xC0) != 0x00) return false;

	// Copy data to Dest
	MemCopyN(Dest, floppy_sector, 512);
	return true;
}

bool uni::FloppyDisk::Write(stduint BlockIden, const void* Sors) {
	if (BlockIden >= getUnits()) return false;

	// Copy data from Sors to DMA buffer
	MemCopyN(floppy_sector, Sors, 512);

	byte cyl, head, sec;
	LBA2CHS(BlockIden, cyl, head, sec);

	Motor(true);
	asserv(fn_feedback)();

	// Configure data transfer rate dynamically
	outpb(PORT_FDC_CCR, DATA_RATE);

	// Setup ISA DMA Channel 2
	dma_xfer(2, (stduint)floppy_sector, 512, false);

	WriteCmd(FDC_CMD_WRITE_DATA);
	WriteCmd((head << 2) | id);
	WriteCmd(cyl);
	WriteCmd(head);
	WriteCmd(sec);
	WriteCmd(0x02); 
	WriteCmd(SECTORS_PER_TRACK);
	WriteCmd(GAP3_LENGTH);
	WriteCmd(0xFF); 

	if (react_type == ReactType::Rupt && fn_int_wait) {
		fn_int_wait();
	} else {
		for (volatile int i = 0; i < 100000; i++) _TEMP;
	}

	// Read 7 Status Bytes
	byte st0 = ReadData();
	for (int i = 0; i < 6; i++) ReadData(); 

	Motor(false);
	
	if ((st0 & 0xC0) != 0x00) return false;
	return true;
}

PartitionSlice uni::FloppyDisk::getSlice(stduint dev) {
	PartitionSlice slice;
	slice.address = 0;
	slice.length = getUnits();
	slice.sys_id = FILESYS_FAT12;
	return slice;
}

struct FloppyDisk_Paged : public uni::FloppyDisk {
	FloppyDisk_Paged(byte _id = 0, FloppyDriveType type = FloppyDriveType::Drive_1_44MB_3_5) : FloppyDisk(_id, type) {}
	virtual bool Read(stduint BlockIden, void* Dest) override;
	virtual bool Write(stduint BlockIden, const void* Sors) override;
};

bool FloppyDisk_Paged::Read(stduint BlockIden, void* Dest) {
	if (Taskman::CurrentPID() == Task_Flp_Serv) {
		// Delegate to the actual physical floppy disk instance to keep motor state in sync
		return floppies[getID()]->Read(BlockIden, Dest);
	}
	stduint to_args[2];
	to_args[0] = getID();
	to_args[1] = BlockIden;
	syssend(Task_Flp_Serv, sliceof(to_args), _IMM(FiledevMsg::READ));
	// Receive ACK before data transfer
	stduint ack;
	sysrecv(Task_Flp_Serv, &ack, sizeof(ack));
	if (!ack) return false;
	sysrecv(Task_Flp_Serv, Dest, Block_Size);
	return true;
}

bool FloppyDisk_Paged::Write(stduint BlockIden, const void* Sors) {
	if (Taskman::CurrentPID() == Task_Flp_Serv) {
		// Delegate to the actual physical floppy disk instance to keep motor state in sync
		return floppies[getID()]->Write(BlockIden, Sors);
	}
	stduint to_args[2];
	to_args[0] = getID();
	to_args[1] = BlockIden;
	syssend(Task_Flp_Serv, sliceof(to_args), _IMM(FiledevMsg::WRITE));
	// Receive ACK before data transfer
	stduint ack;
	sysrecv(Task_Flp_Serv, &ack, sizeof(ack));
	if (!ack) return false;
	syssend(Task_Flp_Serv, Sors, Block_Size);
	return true;
}

void R_FLP_INIT() {
	FloppyInfo info = DetectFloppyDrives();
	if (info.count == 0) return;

	IC[IRQ_Floppy].setRange(mglb(Handint_FLP_Entry), SegCo32);
	register_interrupt_handler(IRQ_Floppy, Handint_FLP);

	if (info.has_drive_a) {
		floppies[0] = new (flp_buf) FloppyDisk(0, static_cast<FloppyDriveType>(info.type_a));
		floppies[0]->setInterrupt(NULL);
	}
	if (info.has_drive_b) {
		floppies[1] = new (flp_buf + sizeof(FloppyDisk)) FloppyDisk(1, static_cast<FloppyDriveType>(info.type_b));
		floppies[1]->setInterrupt(NULL);
	}

	// Allocate a 4KB aligned physical page for floppy DMA buffer
	if (!floppy_sector) {
		floppy_sector = (char*)mempool.allocate(4096, PAGESIZE_4KB);
	}
}

static stduint args[4];
FloppyDisk_Paged* paged_floppies[2] = { nullptr, nullptr };
static char paged_flp_buf[sizeof(FloppyDisk_Paged) * 2];

const char* drive_type_names[] = {
	"None",
	"360KB 5.25\"",
	"1.2MB 5.25\"",
	"720KB 3.5\"",
	"1.44MB 3.5\"",
	"2.88MB 3.5\""
};

void serv_dev_fl_loop()
{
	FloppyInfo info = DetectFloppyDrives();
	if (info.count == 0) {
		stduint sig_type = 0, sig_src;
		while (true) {
			sysrecv(ANYPROC, sliceof(args), &sig_type, &sig_src);
		}
	}

	for0a(i, floppies) {
		if (floppies[i]) {
			floppies[i]->fn_int_wait = flp_int_wait;
			floppies[i]->fn_feedback = flp_rw_foreback;
			floppies[i]->react_type = FloppyDisk::ReactType::Rupt;
		}
	}
	// Initialize the paged proxy floppies
	if (info.has_drive_a) {
		paged_floppies[0] = new (paged_flp_buf) FloppyDisk_Paged(0, static_cast<FloppyDriveType>(info.type_a));
	}
	if (info.has_drive_b) {
		paged_floppies[1] = new (paged_flp_buf + sizeof(FloppyDisk_Paged)) FloppyDisk_Paged(1, static_cast<FloppyDriveType>(info.type_b));
	}
	
	stduint sig_type = 0, sig_src;
	String lab;
	while (true) {
		switch ((FiledevMsg)sig_type)
		{
		case FiledevMsg::TEST:// (no-feedback)
			for0(i, 2) {
				if (floppies[i]) {
					floppies[i]->Reset();
					if (floppies[i]->IsMediaPresent()) {
						ploginfo("[Floppy] Detect Floppy on Drive %c: : %u KB (%s)", 
							'A' + i, 
							_IMM(floppies[i]->getUnits() * floppies[i]->Block_Size) / 1024,
							drive_type_names[static_cast<byte>(floppies[i]->getType())]);
						lab = String::newFormat("/mnt/fl%d", i);
						if (auto fs = Filesys::Mount(*paged_floppies[i], 0, lab.reference())) {
							ploginfo("[Floppy] Mounted on %s successfully", lab.reference());
						}
					} else {
						ploginfo("[Floppy] Drive %c: No Media Present", 'A' + i);
					}
				}
			}
			break;
		case FiledevMsg::RUPT:// (usercall-forbidden, no feedback)
			break;
		case FiledevMsg::CLOSE:// [diskno]
			break;
		case FiledevMsg::READ:// [diskno, lba]
		{
			stduint ack = (args[0] < 2 && floppies[args[0]] && floppies[args[0]]->Read(args[1], floppy_sector)) ? 1 : 0;
			if (sig_src) syssend(sig_src, &ack, sizeof(ack));
			if (ack && sig_src) syssend(sig_src, floppy_sector, floppies[args[0]]->Block_Size);
			break;
		}
		case FiledevMsg::WRITE:// [diskno, lba]
		{
			stduint ack = (args[0] < 2 && floppies[args[0]]) ? 1 : 0;
			if (sig_src) syssend(sig_src, &ack, sizeof(ack));
			if (ack && sig_src) {
				sysrecv(sig_src, floppy_sector, floppies[args[0]]->Block_Size);
				floppies[args[0]]->Write(args[1], floppy_sector);
			}
			break;
		}
		case FiledevMsg::GETPS:
			if (args[0] < 2 && floppies[args[0]] && sig_src) {
				PartitionSlice slice = floppies[args[0]]->getSlice(0);
				syssend(sig_src, &slice, sizeof(slice));
			}
			break;
		default:
			plogerro("Bad TYPE in %s %s", __FILE__, __FUNCIDEN__);
			break;
		}
		sysrecv(ANYPROC, sliceof(args), &sig_type, &sig_src);
	}
}
#endif
