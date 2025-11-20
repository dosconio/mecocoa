//{TODEL} The mature filesys and management will be contained by unisym.
#define _STYLE_RUST
#define _DEBUG

#include <c/consio.h>
#include <c/storage/harddisk.h>
#include "../include/atx-x86-flap32.hpp"
#include "cpp/Device/Storage/HD-DEPEND.h"

#include "../include/filesys.hpp"

use crate uni;

// Current: 

#define	NR_DEFAULT_FILE_SECTS 2048 // 1MB, ROOT Directory Record File



void make_filesys(Harddisk_PATA_Paged& ide, byte* buffer)
{
	bool state = false;
	//
	ploginfo("making FS on IDE%u (buffer %[32H])", ide.getLowID(), buffer);
	Slice total = ide.GetPartEntry(0);
	// outsfmt("[mkfs]  0x%[32H] began %u secs\n\r", total.address * ide.Block_Size, total.length);

	int&& bits_per_sect = ide.Block_Size * _BYTE_BITS_;
	// - Write a super block to sector 1.
	super_block sb;
	sb.entity.magic = MAGIC_V1;
	sb.entity.nr_inodes = bits_per_sect;
	sb.entity.nr_inode_sects = sb.entity.nr_inodes * INODE_SIZE / ide.Block_Size;
	sb.entity.nr_sects = total.length;
	sb.entity.nr_imap_sects = 1;
	sb.entity.nr_smap_sects = sb.entity.nr_sects / bits_per_sect + 1;
	sb.entity.n_1st_sect = 1 + 1 +   /* boot sector & super block */
		sb.entity.nr_imap_sects + sb.entity.nr_smap_sects + sb.entity.nr_inode_sects;
	sb.entity.root_inode = ROOT_INODE;
	sb.entity.inode_size = INODE_SIZE;
	sb.entity.inode_isize_off = offsof(inode, entity.i_size);//(int)&x.i_size - (int)&x; after inode x;
	sb.entity.inode_start_off = offsof(inode, entity.i_start_sect);
	sb.entity.dir_ent_size = DIR_ENTRY_SIZE;
	sb.entity.dir_ent_inode_off = offsof(dir_entry, inode_nr);
	sb.entity.dir_ent_fname_off = offsof(dir_entry, name);
	//

	MemSet(buffer, 0x90, ide.Block_Size);
	MemCopyN(buffer, &sb, SUPER_BLOCK_SIZE);
	ide.Write(1, buffer);
	// - Debug show
	if (_TEMP 1) {
		outsfmt("[mkfs] for IDE%u:%u\n\r", ide.getHigID(), ide.getLowID());
		outsfmt("[mkfs]  superbloc 0x%[32H]\n\r", (total.address + 1) * ide.Block_Size);
		outsfmt("[mkfs]  inode-map 0x%[32H]\n\r", (total.address + 1 + 1) * ide.Block_Size);
		outsfmt("[mkfs]  sectormap 0x%[32H]\n\r", (total.address + 1 + 1 + sb.entity.nr_imap_sects) * ide.Block_Size);
		outsfmt("[mkfs]     inodes 0x%[32H]\n\r", (total.address + 1 + 1 + sb.entity.nr_imap_sects + sb.entity.nr_smap_sects) * ide.Block_Size);
		outsfmt("[mkfs]    sectors 0x%[32H]\n\r", (total.address + sb.entity.n_1st_sect) * ide.Block_Size);
	}
	// - Create the inode map
	MemSet(buffer, 0x00, ide.Block_Size);
	buffer[0] = 0b00111111;// LE Style
	// bit 0 reserved
	// bit 1 first inode, for root `/`
	// bit 2 `/dev_tty0`
	// bit 3 `/dev_tty1`
	// bit 4 `/dev_tty2`
	// bit 5 `/dev_tty3`
	ide.Write(2, buffer);
	// - Create the sector map
	MemSet(buffer, 0x00, ide.Block_Size);
	int nr_sects = NR_DEFAULT_FILE_SECTS + 1;// 1 for root `/`
	byte* bufptr = buffer;

	//! nr_sects usually< 512*_BYTE_BITS_
	while (nr_sects >= _BYTE_BITS_) {
		*bufptr++ = ~(byte)0;
		nr_sects -= _BYTE_BITS_;
	}
	while (nr_sects) {
		nr_sects--;
		*bufptr |= _IMM1 << nr_sects;
	}
	usize&& sec_start = 2 + sb.entity.nr_imap_sects;
	usize sec = sec_start;
	state = ide.Write(sec++, buffer);
	MemSet(buffer, 0x00, ide.Block_Size);// rest sectors
	byte _dbg_sec_fill_time = 1;
	while (sec < sec_start + sb.entity.nr_smap_sects) {
		// ploginfo("write %u", sec);
		state = ide.Write(sec++, buffer);
		_dbg_sec_fill_time++;
		outsfmt(">");
		if (!state) plogerro("mkfs: failed to RW.");
	}
	outsfmt("\n\r");
	ploginfo("fill the  sectormap %u times", _dbg_sec_fill_time);
	// - Create the inodes of the files
	//   (now rw-buffer is empty)
	Letvar(pnod, inode::inode_entity*, buffer);
	// --- inode of root `/`
	pnod->i_mode = I_DIRECTORY;
	pnod->i_size = DIR_ENTRY_SIZE * 5;// ., /dev_tty0, /dev_tty1, /dev_tty2, /dev_tty3
	pnod->i_start_sect = sb.entity.n_1st_sect;
	pnod->i_nr_sects = NR_DEFAULT_FILE_SECTS;
	// --- inode of ttyN
	for0(i, 4) {
		pnod++;
		pnod->i_mode = I_CHAR_SPECIAL;
		pnod->i_size = 0;// ., /dev_tty0, /dev_tty1, /dev_tty2, /dev_tty3
		pnod->i_start_sect = MAKE_DEV(DEV_TTY, i);
		pnod->i_nr_sects = 0;
	}
	state = ide.Write(sec++, buffer);
	// - Create `/', the root directory
	MemSet(buffer, 0x00, ide.Block_Size);
	Letvar(pde, dir_entry*, buffer);
	pde->inode_nr = 1;
	StrCopy(pde->name, ".");
	//   (ttyN)
	for0(i, 4) {
		pde++;
		pde->inode_nr = 2 + i;
		String(pde->name, byteof(pde->name)).Format("dev_tty%u", i);// sprintf
	}
	state = ide.Write(sb.entity.n_1st_sect, buffer);
	if (!state) plogerro("mkfs: failed to RW.");
}

inode* root_inode;

// In Orange'S FS v1.0
//{make sense of this}
int search_file(rostr path, char* const buffer_sector)
{
	int i, j;
	char filename[MAX_FILENAME_LEN + 1] = { 0 };
	struct inode * dir_inode;
	if (strip_path(filename, path, &dir_inode) != 0)
		return 0;
	if (filename[0] == 0)	// path: "/"
		return dir_inode->i_num;

	ploginfo("search_file: disk %u with path %s", dir_inode->i_dev, path);
	StorageTrait* disk = IndexDisk(dir_inode->i_dev);
	if (!disk) {
		return 0;// not found
	}
	// Search the dir for the file.
	int dir_blk0_nr = dir_inode->entity.i_start_sect;
	int nr_dir_blks = (dir_inode->entity.i_size + disk->Block_Size - 1) / disk->Block_Size;
	int nr_dir_entries = dir_inode->entity.i_size / DIR_ENTRY_SIZE; // including unused slots (the file has been deleted but the slot is still there)
	int m = 0;
	struct dir_entry* pde;
	for (i = 0; i < nr_dir_blks; i++) {
		disk->Read(dir_blk0_nr + i, buffer_sector);
		pde = (struct dir_entry *)buffer_sector;
		for (j = 0; j < disk->Block_Size / DIR_ENTRY_SIZE; j++,pde++) {
			if (MemCompare(filename, pde->name, MAX_FILENAME_LEN) == 0)
				return pde->inode_nr;
			if (++m > nr_dir_entries)
				break;
		}
		if (m > nr_dir_entries) // all entries have been iterated
			break;
	}
	return 0;// not found
}

// In Orange'S FS v1.0
// * e.g. After stip_path(filename, "/blah", ppinode) finishes, we get:
// *      - filename: "blah"
// *      - *ppinode: root_inode
// *      - ret val:  0 (successful)
// Currently an acceptable pathname should begin with at most one `/' preceding a filename.
bool strip_path(char* filename, const char* pathname, struct inode** ppinode)
{
	const char* s = pathname;
	char* t = filename;
	//{} assert
	if (*s == '/') s++;
	while (*s) {
		if (*s == '/') {
		    return false;
		}
		*t++ = *s++;
		if (t - filename >= MAX_FILENAME_LEN - 1) break;
	}
	*t = 0;
	*ppinode = root_inode;
	return true;
}

