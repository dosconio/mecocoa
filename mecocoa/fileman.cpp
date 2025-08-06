// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] File Manage - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#define _DEBUG
#include <new>
#include <c/consio.h>
#include <c/storage/harddisk.h>
#include "../include/atx-x86-flap32.hpp"
#include "cpp/Device/Storage/HD-DEPEND.h"



use crate uni;

static char single_sector[512];

#define	MAKE_DEVICE_REG(lba,drv,lba_highest) (((lba) << 6) | ((drv) << 4) | (lba_highest & 0xF) | 0xA0)

static byte lock = 1;


void Handint_HDD()// HDD Master
{
	innpb(REG_STATUS);
	if (lock) return;
	lock = 1;
	rupt_proc(Task_Hdd_Serv, IRQ_ATA_DISK0);
}

#define ANYPROC (_IMM0)
#define INTRUPT (~_IMM0)


static bool waitfor(int mask, int val, int timeout_second)// return seccess
{
	int t = syscall(syscall_t::TIME);
	while (((syscall(syscall_t::TIME) - t)) < timeout_second)
		if ((innpb(REG_STATUS) & mask) == val)
			return 1;
	return 0;
}

static bool hd_cmd_wait() {
	return waitfor(STATUS_BSY, 0, HD_TIMEOUT / 1000);
}

static void hd_int_wait() {
	CommMsg msg;
	syscall(syscall_t::COMM, 0b10, INTRUPT, &msg);
}

struct iden_info_ascii {
	int idx;
	int len;
	rostr desc;
} iinfo[] = {
	{10, 20, "HD SN"}, // Serial number in ASCII
	{27, 40, "Model"} // Model number in ASCII
};
static void print_identify_info(uint16* hdinfo)
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
		outsfmt("[Hrddisk] %s: %s\n\r", iinfo[k].desc, s);
	}

	int capabilities = hdinfo[49];
	outsfmt("[Hrddisk] LBA  : %s\n\r", (capabilities & 0x0200) ? "Supported" : "No");

	int cmd_set_supported = hdinfo[83];
	outsfmt("[Hrddisk] LBA48: %s\n\r", (cmd_set_supported & 0x0400) ? "Supported" : "No");

	int sectors = ((int)hdinfo[61] << 16) + hdinfo[60];
	outsfmt("[Hrddisk] Size : %dMB\n\r", sectors * 512 / 1000000);
}
static void hd_report(Harddisk_PATA& hd) {
	HdiskCommand cmd;
	cmd.command = ATA_IDENTIFY;
	cmd.device = MAKE_DEVICE_REG(0, hd.getUnits(), 0);
	lock = 0;
	Harddisk_PATA::Hdisk_OUT(&cmd, hd_cmd_wait);
	hd_int_wait();
	IN_wn(REG_DATA, (word*)single_sector, hd.Block_Size);
	print_identify_info((uint16*)single_sector);
}

Harddisk_PATA* disks[2];
static char hdd_buf[byteof(**disks) * numsof(disks)];
void DSK_Init() {
	for0(i, numsof(disks)) {
		disks[i] = new (hdd_buf + i * byteof(**disks)) Harddisk_PATA(i);
	}
	disks[0]->setInterrupt(NULL);
}

#define COMM_RECV 0b10
void serv_file_loop()// Disk (0~255) and File System (256+)
{
	// Console.OutFormat("hdisk_number: 0x%[32H] %u\n\r", &bda->hdisk_number ,bda->hdisk_number);
	if (_IMM(&bda->hdisk_number) != 0x475) {
		plogerro("Invalid BIOS_DataArea");
		while (1);
	}
	Console.OutFormat("[Hrddisk] detect %u disks\n\r", bda->hdisk_number);

	struct CommMsg msg;
	while (true) {
		syscall(syscall_t::COMM, COMM_RECV, 0, &msg);
		// ploginfo("Fileman: type %d", msg.type);
		switch (msg.type)
		{
		case 0:// TEST
			hd_report(*disks[0]);
			break;
		case 1:// HARDRUPT
			break;
		default:
			plogerro("Bad TYPE in fileman %s", __FUNCIDEN__);
			break;
		}
	}
}
