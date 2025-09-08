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
#define	NR_DEFAULT_FILE_SECTS 2048 // 1MB, ROOT Directory Record File

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

static Slice HD_GetPartEntry(Harddisk_PATA_Paged& pata, usize device)
{
	stduint to_args[2];
	struct CommMsg msg { .data = { .address = _IMM(to_args), .length = byteof(to_args) } };
	msg.type = 0x05;
	to_args[0] = pata.getID();
	to_args[1] = device;
	syscall(syscall_t::COMM, COMM_SEND, Task_Hdd_Serv, &msg);
	Slice part_entry = { 0 };
	CommMsg msg1{ .data = {.address = _IMM(&part_entry), .length = sizeof(part_entry) } };
	syscall(syscall_t::COMM, COMM_RECV, Task_Hdd_Serv, &msg1);
	return part_entry;
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

			#if 0
			msg.type = 0x05;
			msg.data.length = 2 * sizeof(stduint);
			to_args[0] = 0x0001;// IDE 0:1
			for0 (i, NR_PRIM_PER_DRIVE + NR_SUB_PER_DRIVE) {
				to_args[1] = i;
				syscall(syscall_t::COMM, COMM_SEND, Task_Hdd_Serv, &msg);
				Slice part_entry = {0};
				CommMsg msg1{ .data = {.address = _IMM(&part_entry), .length = sizeof(part_entry)} };
				syscall(syscall_t::COMM, COMM_RECV, Task_Hdd_Serv, &msg1);
				if (part_entry.length) {
					outsfmt("> %[8H]: %u..%u\n\r",
						i < 5 ? i : 0x10 + ((i - NR_PRIM_PER_DRIVE)),
						part_entry.address, part_entry.address + part_entry.length);
				}
				else if (i >= 5) {
					i += 0x10 - 5;
					i /= 0x10;
					i *= 0x10;
					i += 5 - 1;
				}
			}
			#endif

			// mkfs
			if (1) {
				Harddisk_PATA_Paged IDE0_1(0x0001);
				//
				Slice total = HD_GetPartEntry(IDE0_1, 0);
				// outsfmt("[mkfs] disk0:1: 0x%[32H] began %u secs\n\r", total.address * IDE0_1.Block_Size, total.length);

				int &&bits_per_sect = IDE0_1.Block_Size * _BYTE_BITS_;
				// - Write a super block to sector 1.
				super_block sb;
				sb.entity.magic = MAGIC_V1;
				sb.entity.nr_inodes = bits_per_sect;
				sb.entity.nr_inode_sects = sb.entity.nr_inodes * INODE_SIZE / IDE0_1.Block_Size;
				sb.entity.nr_sects = total.length;
				sb.entity.nr_imap_sects  = 1;
				sb.entity.nr_smap_sects  = sb.entity.nr_sects / bits_per_sect + 1;
				sb.entity.n_1st_sect = 1 + 1 +   /* boot sector & super block */
					sb.entity.nr_imap_sects + sb.entity.nr_smap_sects + sb.entity.nr_inode_sects;
				sb.entity.root_inode = ROOT_INODE;
				sb.entity.inode_size = INODE_SIZE;
				sb.entity.inode_isize_off = offsof(inode, entity.i_size);//(int)&x.i_size - (int)&x; after inode x;
				sb.entity.inode_start_off= offsof(inode, entity.i_start_sect);
				sb.entity.dir_ent_size = DIR_ENTRY_SIZE;
				sb.entity.dir_ent_inode_off = offsof(dir_entry,inode_nr);
				sb.entity.dir_ent_fname_off = offsof(dir_entry, name);
				//
				MemSet(buffer, 0x90, IDE0_1.Block_Size);
				MemCopyN(buffer, &sb, SUPER_BLOCK_SIZE);
				IDE0_1.Write(1, buffer);
				// - Debug show
				if (_TEMP 1) {
					outsfmt("[mkfs] disk0:1: superbloc 0x%[32H]\n\r", (total.address + 1) * IDE0_1.Block_Size);
					outsfmt("[mkfs] disk0:1: inode-map 0x%[32H]\n\r", (total.address + 1 + 1) * IDE0_1.Block_Size);
					outsfmt("[mkfs] disk0:1: sectormap 0x%[32H]\n\r", (total.address + 1 + 1 + sb.entity.nr_imap_sects) * IDE0_1.Block_Size);
					outsfmt("[mkfs] disk0:1:    inodes 0x%[32H]\n\r", (total.address + 1 + 1 + sb.entity.nr_imap_sects + sb.entity.nr_smap_sects) * IDE0_1.Block_Size);
					outsfmt("[mkfs] disk0:1:   sectors 0x%[32H]\n\r", (total.address + sb.entity.n_1st_sect) * IDE0_1.Block_Size);
				}
				// - Create the inode map
				MemSet(buffer, 0x00, IDE0_1.Block_Size);
				buffer[0] = 0b00111111;// LE Style
				// bit 0 reserved
				// bit 1 first inode, for root `/`
				// bit 2 /dev_tty0
				// bit 3 /dev_tty1
				// bit 4 /dev_tty2
				// bit 5 /dev_tty3
				IDE0_1.Write(2, buffer);
				// - Create the sector map
				MemSet(buffer, 0x00, IDE0_1.Block_Size);
				int nr_sects = NR_DEFAULT_FILE_SECTS + 1;// 1 for root `/`
				byte* bufptr = buffer;

				// _ASM("mov %esp, %eax");
				// _ASM("mov %%eax, %0" : "=r"(hhhh[0]));
				// outsfmt("current stack ptr: %[32H]\n\r", hhhh[0]);

				//! nr_sects usually< 512*_BYTE_BITS_
				while (nr_sects >= _BYTE_BITS_) {
					*bufptr++ = ~(byte)0;
					nr_sects -= _BYTE_BITS_;
				}
				while (nr_sects) {
					nr_sects--;
					*bufptr |= _IMM1 << nr_sects;
				}
				usize sec = 2 + sb.entity.nr_imap_sects;
				IDE0_1.Write(sec++, buffer);
				MemSet(buffer, 0x00, IDE0_1.Block_Size);// rest sectors
				while (sec < 2 + sb.entity.nr_smap_sects) {
					IDE0_1.Write(sec++, buffer);
				}
				// - Create the inodes of the files
				// - Create `/', the root directory
				
			}

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


