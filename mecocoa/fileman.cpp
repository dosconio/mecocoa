// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] File Manage - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST
#undef _DEBUG
#define _DEBUG
#include <c/consio.h>
#include <c/storage/harddisk.h>

#ifdef _ARC_x86 // x86:
#include "../include/atx-x86-flap32.hpp"
#include "cpp/Device/Storage/HD-DEPEND.h"

#include "../include/filesys.hpp"

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

#define NR_CONSOLES	4

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

Slice Harddisk_PATA_Paged::GetPartEntry(usize device)
{
	stduint to_args[2];
	struct CommMsg msg { .data = { .address = _IMM(to_args), .length = byteof(to_args) } };
	msg.type = 0x05;
	to_args[0] = self.getID();
	to_args[1] = device;
	syscall(syscall_t::COMM, COMM_SEND, Task_Hdd_Serv, &msg);
	Slice part_entry = { 0 };
	CommMsg msg1{ .data = {.address = _IMM(&part_entry), .length = sizeof(part_entry) } };
	syscall(syscall_t::COMM, COMM_RECV, Task_Hdd_Serv, &msg1);
	return part_entry;
}



// e.g. create_file("/hello", 0);
static inode* create_file(rostr path, int flags)
{
	char filename[_TEMP 20];
	inode* dir_inode;
	if (strip_path(filename, path, &dir_inode) != 0) return NULL;


	return _TEMP 0;
}

static stdsint do_open(rostr pathname, int flags, ProcessBlock& process) {
	int fd = -1;


	return _TODO - 1;
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
	Harddisk_PATA_Paged IDE0_1(0x0001);
	while (true) {
		switch (msg.type)
		{
		case 0:// TEST (no-feedback)
			ploginfo("TESTING FILEMAN");
			syscall(syscall_t::COMM, COMM_SEND, Task_Hdd_Serv, &msg);

			// mkfs
			if (0) {
				make_filesys(IDE0_1, buffer);
			}//{} unchk

			ploginfo("TESTING FILEMAN FINISHED");
			break;
		case 1:// HARDRUPT (usercall-forbidden)
			break;
		case 2:// OPEN
		{
			// open a file and return the file descriptor
			ploginfo("[fileman] open %s with %[32H]", 1 + (byte*)to_args, ((byte*)to_args)[0]);
			CommMsg msg_back;
			stduint ret;
			msg_back.data.address = _IMM(&ret);
			msg_back.data.length = sizeof(ret);
			ret = static_cast<stduint>(do_open(
				(rostr)to_args,
				((byte*)to_args)[0],
				*TaskGet(_TODO _TODO _TODO 0)
			));
			syscall(syscall_t::COMM, COMM_SEND, msg.src, &msg_back);
			break;
		}
		case 3:// CLOSE
			//{}
			break;
		case 4:// READ
			//{}
			break;
		case 5:// WRITE
			//{}
			break;
		// ... ...

		default:
			plogerro("Bad TYPE in %s %s", __FILE__, __FUNCIDEN__);
			break;
		}
		syscall(syscall_t::COMM, COMM_RECV, 0, &msg);
	}
}

#endif
