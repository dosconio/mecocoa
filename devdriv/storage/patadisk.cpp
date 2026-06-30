// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: Disk - PATA
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

// unchk: IDE1:0, IDE1:1

#include "../../include/mecocoa.hpp"
#include <c/storage/harddisk.h>
#include <c/format/filesys.h>

_ESYM_C void R_HDD_INIT();

#if _MCCA == 0x8632
#if 1
__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_HDD{
	.init = R_HDD_INIT,
	.name = "DISK-PATA",
};
#endif

Harddisk_PATA* disks[MAX_DRIVES];// referenced
static char hdd_buf[byteof(**disks) * numsof(disks)];
static byte lock = 1;
static char* single_sector = NULL;// file-hd used buffer

static void register_pata_storage_nodes() {
	auto* pata_node = Devsman::FindPCIDeviceByClass(0x01u, 0x01u, 0xFFu);
	if (!pata_node) return;
	for0(i, MAX_DRIVES) {
		if (!disks[i]) continue;
		const bool is_cdrom = disks[i]->Block_Size == 2048;
		String node_name;
		node_name.Format(is_cdrom ? "pata-cd@%u:%u" : "pata-disk@%u:%u",
			(stduint)disks[i]->getHigID(),
			(stduint)disks[i]->getLowID());
		Devsman::RegisterStorageDevice(
			pata_node,
			node_name.reference(),
			DeviceBusType::PCI,
			is_cdrom ? "pata-cdrom" : "pata-disk",
			disks[i]);
	}
}

_ESYM_C void Handint_HDD1_Entry();

void Handint_HDD1()
{
	if (disks[2]) disks[2]->getStatus(); // Clear Secondary IDE Channel interrupt
	if (lock) {
		IC.SendEOI(IRQ_ATA_DISK1);
		return;
	}
	lock = 1;
	rupt_proc(Task_Hdd_Serv, IRQ_ATA_DISK1);
	IC.SendEOI(IRQ_ATA_DISK1);
}

void R_HDD_INIT() {
	IC[IRQ_ATA_DISK0].setRange(mglb(Handint_HDD_Entry), SegCo32);
	register_interrupt_handler(IRQ_ATA_DISK0, Handint_HDD);
	IC[IRQ_ATA_DISK1].setRange(mglb(Handint_HDD1_Entry), SegCo32);
	register_interrupt_handler(IRQ_ATA_DISK1, Handint_HDD1);
	
	for0(i, MAX_DRIVES) {
		Harddisk_PATA probe(i);
		byte dev_type = probe.ProbeDevice();
		if (dev_type == 1) {
			disks[i] = new (hdd_buf + i * byteof(**disks)) Harddisk_PATA(i);
		} else if (dev_type == 2) {
			disks[i] = new (hdd_buf + i * byteof(**disks)) xCD_ATAPI(i);
		} else {
			disks[i] = nullptr;
		}
	}
	if (disks[0] || disks[1]) (disks[0] ? disks[0] : disks[1])->setInterrupt(NULL);
	if (disks[2] || disks[3]) (disks[2] ? disks[2] : disks[3])->setInterrupt(NULL); // Enable secondary channel interrupt
	if (!single_sector) single_sector = new char[0x1000];
	register_pata_storage_nodes();
}

void Handint_HDD()// HDD Master
{
	if (disks[0]) disks[0]->getStatus();// innpb(REG_STATUS);
	if (lock) {
		IC.SendEOI(IRQ_ATA_DISK0);
		return;
	}
	lock = 1;
	rupt_proc(Task_Hdd_Serv, IRQ_ATA_DISK0);
	IC.SendEOI(IRQ_ATA_DISK0); // Acknowledge interrupt
}

//
typedef HD_Info hd_info_t[4];// 0:0 0:1 1:0 1:1
static hd_info_t* hd_info = nullptr;
static bool hd_info_valid[4] = { 0 };

#define	STATUS_BSY	0x80
#define	STATUS_DRQ	0x08
#define	HD_TIMEOUT		10000	/* in millisec */
#define	HD_WAITFOR_TIMEOUT	(HD_TIMEOUT / 1000)

static bool waitfor(Harddisk_PATA* hdd, stduint mask, stduint val, stduint timeout_second)// return seccess
{
	int t = syscall(syscall_t::TIME, 0);
	while (((syscall(syscall_t::TIME, 0) - t)) < timeout_second)
		if ((hdd->getStatus() & mask) == val)
			return 1;
	return 0;
}
static bool hd_cmd_wait(Harddisk_PATA* hdd) {
	return waitfor(hdd, STATUS_BSY, 0, HD_WAITFOR_TIMEOUT);
}

static bool hd_int_wait() {
	CommMsg msg;
	syscall(syscall_t::COMM, COMM_RECV, INTRUPT, _IMM(&msg));
	return true;
}
static void hd_rw_foreback() { lock = 0; }

// Use after print_identify_info
stduint uni::Harddisk_PATA::getUnits() {
	return (*hd_info)[getHigID() * 2 + getLowID()].whole_disk.length;
}

struct iden_info_ascii {
	int idx;
	int len;
	rostr desc;
} iinfo[] = {
	{10, 20, "HD SN"}, // Serial number in ASCII
	{27, 40, "Model"}, // Model number in ASCII
};

// Initialize HD_Info
static void print_identify_info(uint16* hdinfo, Harddisk_PATA& hd)
{
	int i, k;
	char s[64];

	for0a(k, iinfo) {
		char* p = (char*)&hdinfo[iinfo[k].idx];
		for (i = 0; i < iinfo[k].len / 2; i++) {
			s[i * 2 + 1] = *p++;
			s[i * 2] = *p++;
		}
		s[i * 2] = 0;
		if (false) outsfmt("[Hrddisk] %s: %s\n\r", iinfo[k].desc, s);
	}

	int capabilities = hdinfo[49];
	int cmd_set_supported = hdinfo[83];
	if (false) outsfmt("[Hrddisk] LBA  : %s\n\r",
		(capabilities & 0x0200) ? (cmd_set_supported & 0x0400) ? "Supported LBA48" : "Supported" : "No");

	int sectors = ((int)hdinfo[61] << 16) + hdinfo[60];
	// outsfmt("[Hrddisk] Size : %lf MB\n\r", (double)sectors * hd.Block_Size / 1024 / 1024);// care for #NM FPU Loss

	(*hd_info)[hd.getHigID() * 2 + hd.getLowID()].whole_disk.address = nil;
	(*hd_info)[hd.getHigID() * 2 + hd.getLowID()].whole_disk.length = sectors;
}

inline static void print_hdinfo(Harddisk_PATA& hd)
{
	HD_Info& hdinfo = (*hd_info)[hd.getHigID() * 2 + hd.getLowID()];
	auto end_lba = [](const uni::PartitionSlice& slice) -> stduint {
		return slice.length ? slice.address + slice.length - 1 : slice.address;
	};
	Console.OutFormat("driver %u:%u\n\r", hd.getHigID(), hd.getLowID());
	Console.OutFormat("whole  %u..%u\n\r",
		hdinfo.whole_disk.address,
		end_lba(hdinfo.whole_disk));
	Console.OutFormat("parts  %u (+%u overflow)\n\r", hdinfo.part_count, hdinfo.part_overflow);
	for0(i, hdinfo.part_count) {
		auto part = hdinfo.parts[i];
		Console.OutFormat("%u      %u..%u type=%x\n\r",
			i + 1,
			part.address,
			end_lba(part),
			part.sys_id);
	}
}

enum { REG_DATA = 0 };
//{TODO} into harddisk.cpp:Open()
static void hd_open(Harddisk_PATA& hd) { // 0x00
	byte low_id = hd.getLowID();
	HdiskCommand cmd = {};
	cmd.command = (hd.Block_Size == 2048) ? ATAPI_CMD_IDENTIFY : ATA_IDENTIFY;
	cmd.device = MAKE_DEVICE_REG(0, low_id, 0);
	lock = 0;
	hd.Hdisk_OUT(&cmd);
	hd_int_wait();
	word io_base = hd.getHigID() == 0 ? PORT_IDE_CommandBlock_0 : PORT_IDE_CommandBlock_1;
	IN_wn(io_base + REG_DATA, (word*)single_sector, 512); // IDENTIFY payload is always 512 bytes
	print_identify_info((uint16*)single_sector, hd);
	if (!hd_info_valid[hd.getHigID() * 2 + low_id]) {
		// if (_TEMP hd.getID() == 0x01) {
		// DiscPartition dpart(hd, NR_PRIM_PER_DRIVE * low_id);
		HD_Info& hdi = (*hd_info)[hd.getHigID() * 2 + low_id];
		if (hd.Block_Size == 512) { // Skip MBR mounting for ATAPI CD-ROMs
			DiscPartition::Partition(hd, hdi, (byte*)single_sector, NR_PRIM_PER_DRIVE * low_id);
		}
		// if (1) print_hdinfo(hd);
		hd_info_valid[hd.getHigID() * 2 + low_id] = true;
	}
}

static void hd_close(Harddisk_PATA& hd) { // 0x02
	hd_info_valid[hd.getHigID() * 2 + hd.getLowID()] = false;
}

PartitionSlice Harddisk_PATA::getSlice(stduint dev) {
	if (Block_Size == 2048) {
		PartitionSlice slice = {};
		if (dev != 0) {
			return slice;
		}
		slice.address = 0;
		slice.length = (getID() < numsof(disks) && disks[getID()]) ? disks[getID()]->getUnits() : 0;
		slice.sys_id = 0;
		return slice;
	}
	HD_Info& hdinfo = (*hd_info)[getHigID() * 2 + getLowID()];
	return GetPartitionSlice(hdinfo, dev);
}

struct Harddisk_PATA_Paged : public uni::Harddisk_PATA {
	Harddisk_PATA_Paged(byte _id = 0, HarddiskType type = HarddiskType::ATA) : Harddisk_PATA(_id, type) {}
	virtual bool Read(stduint BlockIden, void* Dest);
	virtual bool Write(stduint BlockIden, const void* Sors);
};
bool Harddisk_PATA_Paged::Read(stduint BlockIden, void* Dest) {
	if (Taskman::CurrentPID() == Task_Hdd_Serv) {
		if (!disks[getID()]) return false;
		return disks[getID()]->Read(BlockIden, Dest);
	}
	stduint to_args[2];
	to_args[0] = getID();
	to_args[1] = BlockIden;
	syssend(Task_Hdd_Serv, sliceof(to_args), _IMM(FiledevMsg::READ));
	// Receive ACK before data transfer
	stduint ack;
	sysrecv(Task_Hdd_Serv, &ack, sizeof(ack));
	if (!ack) return false;
	sysrecv(Task_Hdd_Serv, Dest, Block_Size);
	return true;
}

bool Harddisk_PATA_Paged::Write(stduint BlockIden, const void* Sors) {
	if (Taskman::CurrentPID() == Task_Hdd_Serv) {
		if (!disks[getID()]) return false;
		return disks[getID()]->Write(BlockIden, Sors);
	}
	stduint to_args[2];
	to_args[0] = getID();
	to_args[1] = BlockIden;
	syssend(Task_Hdd_Serv, sliceof(to_args), _IMM(FiledevMsg::WRITE));
	// Receive ACK before data transfer
	stduint ack;
	sysrecv(Task_Hdd_Serv, &ack, sizeof(ack));
	if (!ack) return false;
	syssend(Task_Hdd_Serv, Sors, Block_Size);
	return true;
}

//// ---- ---- SERVICE ---- ---- ////
static stduint args[4];
Harddisk_PATA_Paged* paged_disks[MAX_DRIVES];

int fat_time = 0;
void serv_dev_hd_loop()
{
	if (_IMM(&bda->hdisk_number) != 0x475) {
		plogerro("Invalid BIOS_DataArea");
		while (1);
	}
	hd_info = new HD_Info[1][4];

	for0a(i, disks) {
		if (disks[i]) {
			disks[i]->fn_cmd_wait = hd_cmd_wait;
			disks[i]->fn_int_wait = hd_int_wait;
			disks[i]->fn_lup_wait = waitfor;
			disks[i]->fn_feedback = hd_rw_foreback;
			disks[i]->react_type = Harddisk_PATA::ReactType::Rupt;
		}
	}
	// Initialize the paged proxy disks
	for0a(i, paged_disks) {
		paged_disks[i] = new Harddisk_PATA_Paged(i);
		if (disks[i]) {
			paged_disks[i]->Block_Size = disks[i]->Block_Size;
		}
	}
	
	// Console.OutFormat("[Hrddisk] detect %u disks\n\r", bda->hdisk_number);
	stduint sig_type = 0, sig_src;
	String lab_fat;
	String lab;
	while (true) {
		switch ((FiledevMsg)sig_type)
		{
		case FiledevMsg::TEST:// (no-feedback)
			for0(i, MAX_DRIVES) {
				if (disks[i]) {
					hd_open(*disks[i]);
					stduint total_units = 0;
					if (disks[i]->Block_Size == 2048 && paged_disks[i]) {
						total_units = paged_disks[i]->getSlice(0).length;
					} else {
						total_units = disks[i]->getUnits();
					}
					Console.OutFormat("[Hrddisk] Detect %s on IDE%u:%u : %u MB\n\r",
						(disks[i]->Block_Size == 2048) ? "CD-ROM" : "Disk",
						i / 2, i % 2,
						_IMM(total_units * disks[i]->Block_Size) >> 20);
				}
			}

			for0(i, MAX_DRIVES) {
				if (!disks[i] || disks[i]->Block_Size != 2048) continue;
				lab = String::newFormat("/mnt/ide%u.0", i);
				ploginfo("[Hrddisk] probe cdrom%u whole-disk", i);
				if (auto fs = Filesys::Mount(*paged_disks[i], 0, lab.reference())) {
					ploginfo("[Hrddisk] mount %s on %s", fs->name, lab.reference());
				} else {
					ploginfo("[Hrddisk] no filesystem recognized on cdrom%u", i);
				}
			}

			for0(i, MAX_DRIVES) {
				if (!disks[i] || disks[i]->Block_Size != 512) continue;
				HD_Info& hdinfo = (*hd_info)[i];
				for (unsigned part_dev = 1; part_dev <= hdinfo.part_count; part_dev++) {
					uni::PartitionSlice slice = GetPartitionSlice(hdinfo, part_dev);
					byte sys_id = slice.sys_id;
					if (sys_id == 0x00) continue; // empty/unpartitioned, skip
					else if (sys_id == FILESYS_EXT) continue;
					lab = String::newFormat("/mnt/ide%u.%u", i, part_dev);
					ploginfo("[Hrddisk] probe disk%u part%u: %x", i, part_dev, sys_id);
					if (auto fs = Filesys::Mount(*paged_disks[i], part_dev, lab.reference())) {
						if (!StrCompare(fs->name, "fat")) {
							fat_time++;
						}
					}
				}
			}
			break;
		case FiledevMsg::RUPT:// (usercall-forbidden, no feedback)
			break;
		case FiledevMsg::CLOSE:// [diskno]
			ploginfo("[Hrddisk] close %u", (unsigned)(byte)args[0]);
			hd_close(*disks[(byte)args[0]]);// assert msg.data.length == 0
			break;
		case FiledevMsg::READ:// [diskno, lba]
		{
			// ploginfo("[Hrddisk] device %u: read %u", args[0], args[1]);
			stduint ack = (args[0] < numsof(disks) && disks[args[0]] && disks[args[0]]->Read(args[1], single_sector)) ? 1 : 0;
			if (sig_src) syssend(sig_src, &ack, sizeof(ack));
			if (ack && sig_src) syssend(sig_src, single_sector, disks[args[0]]->Block_Size);
			break;
		}
		case FiledevMsg::WRITE:// [diskno, lba]
		{
			// ploginfo("[Hrddisk] device %u: write %u", (unsigned)(byte)args[0], slice.address);
			stduint ack = (args[0] < numsof(disks) && disks[args[0]]) ? 1 : 0;
			if (sig_src) syssend(sig_src, &ack, sizeof(ack));
			if (ack && sig_src) {
				sysrecv(sig_src, single_sector, disks[args[0]]->Block_Size);
				disks[args[0]]->Write(args[1], single_sector);
			}
			break;
		}
		// OPEN: not support for fixed sockets

		default:
			plogerro("Bad TYPE in %s %s", __FILE__, __FUNCIDEN__);
			break;
		}
		sysrecv(ANYPROC, sliceof(args), &sig_type, &sig_src);
	}
}


#endif
