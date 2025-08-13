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

// driver h
// device h[0] h[1~4][a~...], h[5], h[6~9][a~...]

static byte* buffer = nil;

static stduint dev_drv_map[] = {
	nil, // ZERO
	nil, // {} Floppy
	nil, // {} CDROM
	Task_Hdd_Serv, // {} Hard Disk
	nil, // {} TTY
	nil, // {} SCSI Disk
	
};

bool waitfor(int mask, int val, int timeout_second)// return seccess
{
	int t = syscall(syscall_t::TIME);
	while (((syscall(syscall_t::TIME) - t)) < timeout_second)
		if ((innpb(REG_STATUS) & mask) == val)
			return 1;
	return 0;
}
void fileman_hd();
void DEV_Init()
{
	// 1 Initialize dev_ifs
	fileman_hd();
}
/* DEVICE * MSG **
- TEST
- RUPT
*/

static bool hd_info_valid = false;

bool Harddisk_PATA_Paged::Read(stduint BlockIden, void* Dest) {
	stduint to_args[2];
	struct CommMsg msg { .data = { .address = _IMM(to_args), .length = byteof(to_args) } };
	struct CommMsg data_msg { .data = { .address = _IMM(Dest), .length = Block_Size } };
	msg.type = 0x03;// read
	to_args[0] = getLowID();
	to_args[1] = BlockIden;
	syscall(syscall_t::COMM, COMM_SEND, Task_Hdd_Serv, &msg);
	syscall(syscall_t::COMM, COMM_RECV, Task_Hdd_Serv, &data_msg);
	return true;
}
bool Harddisk_PATA_Paged::Write(stduint BlockIden, const void* Sors) {
	stduint to_args[2];
	struct CommMsg msg { .data = { .address = _IMM(to_args), .length = byteof(to_args) } };
	struct CommMsg data_msg { .data = { .address = _IMM(Sors), .length = Block_Size } };
	msg.type = 0x04;// write
	to_args[0] = getLowID();
	to_args[1] = BlockIden;
	syscall(syscall_t::COMM, COMM_SEND, Task_Hdd_Serv, &msg);
	syscall(syscall_t::COMM, COMM_SEND, Task_Hdd_Serv, &data_msg);
	return true;
}

void serv_file_loop()
{
	buffer = (byte*)Memory::physical_allocate(0x1000);
	ploginfo("%s", __FUNCIDEN__);
	struct CommMsg msg;
	struct CommMsg data_msg;
	stduint to_args[4];
	msg.type = 0;
	msg.data.address = _IMM(to_args);
	msg.data.length = 0;
	data_msg.data.address = _IMM(buffer);
	data_msg.data.length = 0x1000;
	while (true) {
		switch (msg.type)
		{
		case 0:// TEST (no-feedback)
			ploginfo("TESTING FILEMAN");
			syscall(syscall_t::COMM, COMM_SEND, Task_Hdd_Serv, &msg);

			#if 0// close
			msg.type = 0x02;// close
			to_args[0] = 0x01;
			msg.data.length = 1;
			syscall(syscall_t::COMM, COMM_SEND, Task_Hdd_Serv, &msg);
			#endif

			#if 0// read & write
			Harddisk_PATA_Paged(1).Read(1, buffer);
			for0(i, 16) outsfmt("%[8H] ", buffer[i]);
			outsfmt("\n\r");
			for0(i, 3) buffer[i]++;
			Harddisk_PATA_Paged(1).Write(1, buffer);
			for0(i, 3) buffer[i] = 0;
			Harddisk_PATA_Paged(1).Read(1, buffer);
			for0(i, 16) outsfmt("%[8H] ", buffer[i]);
			outsfmt("\n\r");
			#endif

			ploginfo("TESTING FILEMAN FINISHED");
			break;
		case 1:// HARDRUPT (usercall-forbidden)
			break;
		default:
			plogerro("Bad TYPE in %s %s", __FILE__, __FUNCIDEN__);
			break;
		}
		syscall(syscall_t::COMM, COMM_RECV, 0, &msg);
	}
}

