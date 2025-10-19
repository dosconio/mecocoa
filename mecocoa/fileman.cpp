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
file_desc* f_desc_table = nil;
stduint f_desc_table_count = 0;

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
	to_args[0] = getLowID();//{} ide00 ide01
	to_args[1] = BlockIden;
	syssend(Task_Hdd_Serv, sliceof(to_args), _IMM(FiledevMsg::READ));
	sysrecv(Task_Hdd_Serv, Dest, Block_Size);
	return true;
}
bool Harddisk_PATA_Paged::Write(stduint BlockIden, const void* Sors) {
	stduint to_args[2];
	to_args[0] = getLowID();//{} ide00 ide01
	to_args[1] = BlockIden;
	syssend(Task_Hdd_Serv, sliceof(to_args), _IMM(FiledevMsg::WRITE));
	syssend(Task_Hdd_Serv, Sors, Block_Size);
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

static stdsint do_open(rostr pathname, int flags, ProcessBlock& process, char* buffer) {
	int fd = -1;
	// find a available fileslot
	for0a(i, process.pfiles) {
		if (!process.pfiles[i]) {
			fd = i;
			break;
		}
	}
	if (fd == -1) {
		plogwarn("no file slot available");
		return -1;
	}
	// find a free slot in f_desc_table
	file_desc* pfd = NULL;
	if (f_desc_table_count >= 0x1000 / sizeof(file_desc)) {
		plogwarn("no file desc slot available");
		return -1;
	}
	pfd = &f_desc_table[f_desc_table_count++];

	int inode_nr = search_file(pathname, buffer);

	struct inode* pin = 0;
	if (flags & O_CREAT) {
		if (inode_nr) {
			ploginfo("file `%s' exists", pathname);
			return -1;
		}
		else {
			pin = create_file(pathname, flags);
		}
	}
	else if (flags & O_RDWR) {
		char filename[MAX_FILENAME_LEN] = {0};
		struct inode * dir_inode;
		if (strip_path(filename, pathname, &dir_inode) != 0)
			return -1;
		///////pin = get_inode(dir_inode->i_dev, inode_nr);
	}
	else {
		plogwarn("bad flags");
		return -1;
	}

	/// ...


	return _TODO - 1;
}

void serv_file_loop()
{
	f_desc_table = (file_desc*)Memory::physical_allocate(0x1000);
	f_desc_table_count = nil;
	buffer = (byte*)Memory::physical_allocate(0x1000);
	ploginfo("%s", __FUNCIDEN__);
	stduint to_args[8];// 8*4=32 bytes
	Harddisk_PATA_Paged IDE0_1(0x0001);
	stduint sig_type = 0, sig_src;

	while (true) {
		switch (sig_type)
		{
		case 0:// TEST (no-feedback)
			// plogtrac("TESTING FILEMAN");
			syssend(Task_Hdd_Serv, &to_args, 0, _IMM(FiledevMsg::TEST));
			if (0)
				make_filesys(IDE0_1, buffer);
			ploginfo("TESTING FILEMAN FINISHED");
			break;
		case 1:// HARDRUPT (usercall-forbidden&meaningless)
			break;
		case 2:// OPEN
		{
			// open a file and return the file descriptor
			ploginfo("[fileman] PID %u open %s with %[32H]", to_args[1], &to_args[2], to_args[0]);
			// BYTE 0~ 3 : flags
			// BYTE 4~ 7 : processid
			// BYTE 8~31 : filename
			stduint ret;
			ret = static_cast<stduint>(do_open(
				(rostr)&to_args[2],
				to_args[0],
				*TaskGet(to_args[1]),
				(char*)buffer
				));
			syssend(sig_src, &ret, sizeof(ret), 0);
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
		sysrecv(ANYPROC, to_args, byteof(to_args), &sig_type, &sig_src);
	}
}

#endif
