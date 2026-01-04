// The mature filesys and management are contained by unisym.
//{TODO} make this into FilesysMinix into UNIS
// Current: Orange'S FS v1.0
#define _STYLE_RUST
#define _DEBUG

#ifdef _ARC_x86 // x86:
#include <c/consio.h>
#include <c/storage/harddisk.h>
#include "../include/atx-x86-flap32.hpp"
#include "cpp/Device/Storage/HD-DEPEND.h"

#include "../include/filesys.hpp"

use crate uni;

inode* root_inode;

OrangesFs::OrangesFs(StorageTrait& s, byte* buffer, unsigned dev) {
	new (&self._buf_part) DiscPartition(s, dev);
	storage = (DiscPartition*)&self._buf_part;
	buffer_sector = buffer;
	partid = dev;
}

/*
 * superbloc == total.address + 1) * part.Block_Size
 * inode-map == total.address + 1 + 1) * part.Block_Size
 * sectormap == total.address + 1 + 1 + sb.entity.nr_imap_sects) * part.Block_Size
 *    inodes == total.address + 1 + 1 + sb.entity.nr_imap_sects + sb.entity.nr_smap_sects) * part.Block_Size
 * sectors == total.address + sb.entity.n_1st_sect) * part.Block_Size
*/
bool OrangesFs::makefs(rostr vol_label /* unused */, void* moreinfo /* unused */) {
	//
	bool state = false;
	BlockTrait& part = *storage;
	byte* buffer = (byte*)buffer_sector;
	//
	Console.OutFormat("[Hrddisk] Making OrangesFs on part%u (buffer %[32H])", partid, buffer);

	const int bits_per_sect = part.Block_Size * _BYTE_BITS_;
	// - Write a super block to sector 1.
	super_block sb;
	sb.entity.magic = MAGIC_V1;
	sb.entity.nr_inodes = bits_per_sect;
	sb.entity.nr_inode_sects = sb.entity.nr_inodes * INODE_SIZE / part.Block_Size;
	sb.entity.nr_sects = part.getUnits();
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
	MemSet(buffer, 0x90, part.Block_Size);
	MemCopyN(buffer, &sb, SUPER_BLOCK_SIZE);
	part.Write(1, buffer);
	// - Create the inode map
	MemSet(buffer, 0x00, part.Block_Size);
	buffer[0] = 0b00111111;// LE Style
	// bit 0 reserved
	// bit 1 first inode, for root `/`
	// bit 2 `/dev_tty0`
	// bit 3 `/dev_tty1`
	// bit 4 `/dev_tty2`
	// bit 5 `/dev_tty3`
	part.Write(2, buffer);
	// - Create the sector map
	MemSet(buffer, 0x00, part.Block_Size);
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
	state = part.Write(sec++, buffer);
	MemSet(buffer, 0x00, part.Block_Size);// rest sectors
	byte _dbg_sec_fill_time = 1;
	while (sec < sec_start + sb.entity.nr_smap_sects) {
		// ploginfo("write %u", sec);
		state = part.Write(sec++, buffer);
		_dbg_sec_fill_time++;
		// outsfmt(">");
		if (!state) plogerro("mkfs: failed to RW.");
	}
	outsfmt("\n\r");
	// ploginfo("fill the  sectormap %u times", _dbg_sec_fill_time);
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
	state = part.Write(sec++, buffer);
	// - Create `/', the root directory
	MemSet(buffer, 0x00, part.Block_Size);
	Letvar(pde, dir_entry*, buffer);
	pde->inode_nr = 1;
	StrCopy(pde->name, ".");
	//   (ttyN)
	for0(i, 4) {
		pde++;
		pde->inode_nr = 2 + i;
		String(pde->name, byteof(pde->name)).Format("dev_tty%u", i);// sprintf
	}
	state = part.Write(sb.entity.n_1st_sect, buffer);
	if (!state) plogerro("mkfs: failed to RW.");
	return state;
}

bool OrangesFs::loadfs(void* moreinfo /* unused */) {
	read_superblock();
	super_block* sb = get_superblock();
	if (sb->entity.magic == MAGIC_V1); else
	{
		plogerro("[OrangesFs] Bad Magic-num Check");
		return false;
	}
	return true;
}

bool OrangesFs::create(rostr fullpath, stduint flags, void* exinfo, rostr linkdest) {
	//[unused] linkdest
	char filename[_TEMP 20] = { 0 };
	inode* dir_inode;
	if (!strip_path(filename, fullpath, &dir_inode)) {
		plogwarn("bad filepath %s", fullpath);
		return false;
	}
	OrangesFs* fs = IndexOFs(dir_inode->i_dev);
	if (!fs) fs = this;
	int inode_nr = fs->alloc_imap_bit();
	if (!inode_nr) {
		return false;
	}
	int free_sect_nr = fs->alloc_smap_bit(NR_DEFAULT_FILE_SECTS);
	struct inode *newino = fs->new_inode(inode_nr, free_sect_nr);
	fs->new_direntry(dir_inode, newino->i_num, filename);
	asserv(cast<stduint*>(exinfo))[0] = (stduint)newino;
	return true;
}

bool OrangesFs::remove(rostr pathname) {
	// 1. reset in inode-map
	// 2. reset in sector-map
	// 3. reset inode in inode_array
	// 4. remove entry in root `/`
	//
	// ploginfo("%s: %s", __FUNCIDEN__, pathname);

	if (StrCompare(pathname , "/") == 0) {
		plogerro("FS:do_unlink():: cannot unlink the root");
		return false;
	}

	stduint inode_nr;
	search(pathname, &inode_nr);
	if (inode_nr == INVALID_INODE) {	/* file not found */
		plogerro("%s: search_file() returns invalid inode: %s", __FUNCIDEN__, pathname);
		return false;
	}

	char filename[_TEMP 20] = { 0 };
	inode * dir_inode;
	if (!strip_path(filename, pathname, &dir_inode))
		return false;

	OrangesFs* fs_dir = IndexOFs(dir_inode->i_dev);
	struct inode* pin = fs_dir->get_inode(inode_nr);
	OrangesFs* fs = IndexOFs(dir_inode->i_dev);

	if (pin->entity.i_mode != I_REGULAR) { /* can only remove regular files */
		plogerro("cannot remove file %s, because it is not a regular file.", pathname);
		return false;
	}
	

	if (pin->i_cnt > 1) {	/* the file was opened */
		plogerro("cannot remove file %s, because pin->i_cnt is %d.", pathname, pin->i_cnt);
		return false;
	}

	struct super_block * sb = fs->get_superblock();

	// free the bit in i-map
	int byte_idx = inode_nr / 8;
	int bit_idx = inode_nr % 8;
	if (byte_idx < fs->storage->Block_Size); else// we have only one i-map sector
	{
		plogerro("PANIC %s:%u", __FILE__, __LINE__);
		return false;	
	}
	// read sector 2 (skip bootsect and superblk):
	fs->read_sector(2);
	if (fs->buffer_sector[byte_idx % fs->storage->Block_Size] & (1 << bit_idx)); else {
		plogerro("PANIC %s:%u", __FILE__, __LINE__);
		return false;
	}
	fs->buffer_sector[byte_idx % fs->storage->Block_Size] &= ~(1 << bit_idx);
	fs->write_sector(2);

	/**************************/
	/* free the bits in s-map */
	/* (from ORANGES)
	 *           bit_idx: bit idx in the entire i-map
	 *     ... ____|____
	 *                  \        .-- byte_cnt: how many bytes between
	 *                   \      |              the first and last byte
	 *        +-+-+-+-+-+-+-+-+ V +-+-+-+-+-+-+-+-+
	 *    ... | | | | | |*|*|*|...|*|*|*|*| | | | |
	 *        +-+-+-+-+-+-+-+-+   +-+-+-+-+-+-+-+-+
	 *         0 1 2 3 4 5 6 7     0 1 2 3 4 5 6 7
	 *  ...__/
	 *      byte_idx: byte idx in the entire i-map
	 */
	bit_idx  = pin->entity.i_start_sect - sb->entity.n_1st_sect + 1;
	byte_idx = bit_idx / 8;
	int bits_left = pin->entity.i_nr_sects;
	int byte_cnt = (bits_left - (8 - (bit_idx % 8))) / 8;

	// current sector nr.
	int s = 2  /* 2: bootsect + superblk */
		+ sb->entity.nr_imap_sects + byte_idx / fs->storage->Block_Size;

	fs->read_sector(s);

	int i;
	/* clear the first byte */
	for (i = bit_idx % 8; (i < 8) && bits_left; i++,bits_left--) {
		if ((fs->buffer_sector[byte_idx % fs->storage->Block_Size] >> i & 1) == 1); else {
			plogerro("PANIC %s:%u", __FILE__, __LINE__);
			return false;
		}
		fs->buffer_sector[byte_idx % fs->storage->Block_Size] &= ~(1 << i);
	}

	/* clear bytes from the second byte to the second to last */
	int k;
	i = (byte_idx % fs->storage->Block_Size) + 1;	/* the second byte */
	for (k = 0; k < byte_cnt; k++,i++,bits_left-=8) {
		if (i == fs->storage->Block_Size) {
			i = 0;
			fs->write_sector(s);
			fs->read_sector(++s);
		}
		if (fs->buffer_sector[i] == 0xFF); else {
			plogerro("PANIC %s:%u fs->buffer_sector[i]=%[8H]", __FILE__, __LINE__,
				fs->buffer_sector[i]);
			return false;
		}
		fs->buffer_sector[i] = 0;
	}

	// clear the last byte
	if (i == fs->storage->Block_Size) {
	i = 0;
		fs->write_sector(s);
		fs->read_sector(++s);
	}
	unsigned char mask = ~((unsigned char)(~0) << bits_left);
	if ((fs->buffer_sector[i] & mask) == mask); else {
		plogerro("PANIC %s:%u", __FILE__, __LINE__);
		return false;
	}
	fs->buffer_sector[i] &= (~0) << bits_left;
	fs->write_sector(s);

	// clear the i-node itself
	pin->entity.i_mode = 0;
	pin->entity.i_size = 0;
	pin->entity.i_start_sect = 0;
	pin->entity.i_nr_sects = 0;
	fs->sync_inode(pin);
	// release slot in inode_table[]
	put_inode(pin);

	// set the inode-nr to 0 in the directory entry
	int dir_blk0_nr = dir_inode->entity.i_start_sect;
	int nr_dir_blks = (dir_inode->entity.i_size + fs->storage->Block_Size) / fs->storage->Block_Size;
	int nr_dir_entries =
		dir_inode->entity.i_size / DIR_ENTRY_SIZE; // including unused slots (the file has been deleted but the slot is still there)
	int m = 0;
	struct dir_entry * pde = 0;
	int flg = 0;
	int dir_size = 0;

	for (i = 0; i < nr_dir_blks; i++) {
		fs_dir->read_sector(dir_blk0_nr + i);

		pde = (dir_entry *)fs_dir->buffer_sector;
		int j;
		for (j = 0; j < fs->storage->Block_Size / DIR_ENTRY_SIZE; j++,pde++) {
			if (++m > nr_dir_entries)
				break;

			if (pde->inode_nr == inode_nr) {
				/* pde->inode_nr = 0; */
				MemSet(pde, 0, DIR_ENTRY_SIZE);
				fs_dir->write_sector(dir_blk0_nr + i);
				flg = 1;
				break;
			}

			if (pde->inode_nr != INVALID_INODE)
				dir_size += DIR_ENTRY_SIZE;
		}

		if (m > nr_dir_entries || /* all entries have been iterated OR */
		    flg) /* file is found */
			break;
	}
	if (flg); else {
		plogerro("PANIC %s:%u", __FILE__, __LINE__);
		return false;
	}
	if (m == nr_dir_entries) { /* the file is the last one in the dir */
		dir_inode->entity.i_size = dir_size;
		sync_inode(dir_inode);
	}
	ploginfo("%s endo", __FUNCIDEN__);
	return true;
}


void* OrangesFs::search(rostr path, void* moreinfo) {
	stduint* retback = (stduint*)moreinfo;
	int i, j;
	char filename[MAX_FILENAME_LEN + 1] = { 0 };
	struct inode * dir_inode;
	if (!strip_path(filename, path, &dir_inode))
		return NULL;
	if (filename[0] == 0)	// path: "/"
	{
		asserv(retback)[0] = dir_inode->i_num;
		return None;
	}
	// ploginfo("%s: dev %u with path %s", __FUNCIDEN__, dir_inode->i_dev, path);
	OrangesFs* fs = IndexOFs(dir_inode->i_dev);
	if (!fs) fs = this;
	// Search the dir for the file.
	int dir_blk0_nr = dir_inode->entity.i_start_sect;
	int nr_dir_blks = alignc(fs->storage->Block_Size, dir_inode->entity.i_size);
	int nr_dir_entries = dir_inode->entity.i_size / DIR_ENTRY_SIZE; // including unused slots (the file has been deleted but the slot is still there)
	int m = 0;
	struct dir_entry* pde;
	for (i = 0; i < nr_dir_blks; i++) {
		fs->read_sector(dir_blk0_nr + i);		
		pde = (struct dir_entry*)buffer_sector;
		for (j = 0; j < fs->storage->Block_Size / DIR_ENTRY_SIZE; j++, pde++) {
			if (MemCompare(filename, pde->name, MAX_FILENAME_LEN) == 0)
			{
				asserv(retback)[0] = pde->inode_nr;
				return None;
			}
			if (++m > nr_dir_entries)
				break;
		}
		if (m > nr_dir_entries) // all entries have been iterated
			break;
	}
	return NULL;
}

bool OrangesFs::proper(void* handler, stduint cmd, const void* moreinfo) {return _TODO false;}
bool OrangesFs::enumer(void* dir_handler, _tocall_ft _fn) {return _TODO false;}
stduint OrangesFs::readfl(void* fil_handler, Slice file_slice, byte* dst) {return _TODO false;}
stduint OrangesFs::writfl(void* fil_handler, Slice file_slice, const byte* src) {return _TODO false;}


//// ---- ---- SUPERBLOCK ---- ---- ////
super_block superblocks[NR_SUPER_BLOCK];//{TEMP} in Static segment, should be managed in heap

void OrangesFs::read_superblock()
{
	ploginfo("read super block of device %d", partid);
	read_sector(1);
	for0(i, NR_SUPER_BLOCK) {// find a free slot in super_block
		if (superblocks[i].sb_dev == MajorDevice::DEV_NULL)
		{
			Letvar(psb, super_block::super_block_entity*, buffer_sector);
			superblocks[i].entity = *psb;
			superblocks[i].sb_dev = partid;
			return;
		}
	}
	// unexpected reachable
	plogerro("No free super block slot");
}
super_block* OrangesFs::get_superblock() {
	for0(i, NR_SUPER_BLOCK) {
		if (superblocks[i].sb_dev == partid)
			return &superblocks[i];
	}
	// unexpected reachable
	plogerro("superblock of device %d not found.\n", partid);
	return NULL;
}


//// ---- ---- INODE ---- ---- ////
inode* inode_table;// cnt NR_INODE
// 
inode* OrangesFs::get_inode(stduint inod_idx)
{
	if (!inod_idx) return 0;
	struct inode * p;
	struct inode * q = 0;
	for (p = &inode_table[0]; p < &inode_table[NR_INODE]; p++) {
		if (p->i_cnt) {	/* not a free slot */
			if ((p->i_dev == partid) && (p->i_num == inod_idx)) {
				p->i_cnt++;
				return p;// excepted inode
			}
		}
		else { // a free slot
			if (!q) // q hasn't been assigned yet
				q = p; // q <- the 1st free slot
		}
	}

	if (!q) {
		plogerro("the inode table is full");
		return NULL;
	}

	q->i_dev = partid;
	q->i_num = inod_idx;
	q->i_cnt = 1;

	super_block* sb = get_superblock();
	usize&& inods_per_blk = storage->Block_Size / INODE_SIZE;
	int blk_nr = 1 + 1 + sb->entity.nr_imap_sects + sb->entity.nr_smap_sects +
		((inod_idx - 1) / inods_per_blk);
	read_sector(blk_nr);
	inode * pinode = (inode*)(buffer_sector + INODE_SIZE * ((inod_idx - 1 ) % inods_per_blk));
	q->entity.i_mode = pinode->entity.i_mode;
	q->entity.i_size = pinode->entity.i_size;
	q->entity.i_start_sect = pinode->entity.i_start_sect;
	q->entity.i_nr_sects = pinode->entity.i_nr_sects;
	return q;
}

void OrangesFs::put_inode(inode * pinode)
{
	if (pinode->i_cnt > 0); else {
		plogerro("put_inode: pinode->i_cnt <= 0");
		return;
	}
	pinode->i_cnt--;
}

void OrangesFs::sync_inode(inode* p)
{
	inode* pinode;
	OrangesFs* fs = IndexOFs(p->i_dev);
	if (!fs) fs = this;
	super_block* sb = fs->get_superblock();
	usize&& inods_per_blk = fs->storage->Block_Size / INODE_SIZE;

	int blk_nr = 1 + 1 + sb->entity.nr_imap_sects + sb->entity.nr_smap_sects + ((p->i_num - 1) / inods_per_blk);
	fs->read_sector(blk_nr);
	pinode = (inode*)(fs->buffer_sector +
		(((p->i_num - 1) % inods_per_blk) * INODE_SIZE));
	pinode->entity.i_mode = p->entity.i_mode;
	pinode->entity.i_size = p->entity.i_size;
	pinode->entity.i_start_sect = p->entity.i_start_sect;
	pinode->entity.i_nr_sects = p->entity.i_nr_sects;
	fs->write_sector(blk_nr);
}


inode* OrangesFs::new_inode(stduint inode_nr, stduint start_sect)
{
	struct inode* new_inode = get_inode(inode_nr);
	new_inode->entity.i_mode = I_REGULAR;
	new_inode->entity.i_size = 0;
	new_inode->entity.i_start_sect = start_sect;
	new_inode->entity.i_nr_sects = NR_DEFAULT_FILE_SECTS;
	new_inode->i_dev = partid;
	new_inode->i_cnt = 1;
	new_inode->i_num = inode_nr;
	sync_inode(new_inode);// write to the inode array
	return new_inode;
}


void OrangesFs::new_direntry(inode* dir_inode, int inode_nr, char* filename)
{
	/* write the dir_entry */
	int dir_blk0_nr = dir_inode->entity.i_start_sect;
	int nr_dir_blks = 1 + dir_inode->entity.i_size / storage->Block_Size;
	int nr_dir_entries = dir_inode->entity.i_size / DIR_ENTRY_SIZE;
	// including unused slots (the file has been deleted but the slot is still there)
	int m = 0;
	struct dir_entry * pde;
	struct dir_entry * new_de = 0;

	int i, j;
	for (i = 0; i < nr_dir_blks; i++) {
		pde = (dir_entry *)read_sector(dir_blk0_nr + i);
		for (j = 0; j < storage->Block_Size / DIR_ENTRY_SIZE; j++,pde++) {
			if (++m > nr_dir_entries)
				break;
			if (pde->inode_nr == 0) { /* it's a free slot */
				new_de = pde;
				break;
			}
		}
		if (m > nr_dir_entries ||/* all entries have been iterated or */
		    new_de)              /* free slot is found */
			break;
	}
	if (!new_de) { /* reached the end of the dir */
		new_de = pde;
		dir_inode->entity.i_size += DIR_ENTRY_SIZE;
	}
	new_de->inode_nr = inode_nr;
	StrCopy(new_de->name, filename);
	/* write dir block -- ROOT dir block */
	write_sector(dir_blk0_nr + i);
	sync_inode(dir_inode);// update dir inode
}


//// ---- ---- SEC-MAP ---- ---- ////

stduint OrangesFs::alloc_imap_bit()
{
	int inode_nr = 0;
	int i, j, k;
	int imap_blk0_nr = 1 + 1; // 1 boot sector & 1 super block
	byte* bufp = (byte*)buffer_sector;
	super_block* sb = get_superblock();
	for (i = 0; i < sb->entity.nr_imap_sects; i++) {
		read_sector(imap_blk0_nr + i);
		for (j = 0; j < storage->Block_Size; j++) {
			// skip `11111111' bytes
			if (bufp[j] == 0xFF)
				continue;
			// skip `1' bits
			for (k = 0; ((bufp[j] >> k) & 1) != 0; k++) {}
			// i: sector index; j: byte index; k: bit index
			inode_nr = (i * storage->Block_Size + j) * 8 + k;
			bufp[j] |= (1 << k);
			// write the bit to imap
			write_sector(imap_blk0_nr + i);
			break;
		}
		return inode_nr;
	}
	/* no free bit in imap */
	plogerro("inode-map is probably full (dev %d)", partid);
	return 0;
}

int OrangesFs::alloc_smap_bit(int nr_sects_to_alloc)
{
	/* int nr_sects_to_alloc = NR_DEFAULT_FILE_SECTS; */
	int i; /* sector index */
	int j; /* byte index */
	int k; /* bit index */
	super_block* sb = get_superblock();
	int smap_blk0_nr = 1 + 1 + sb->entity.nr_imap_sects;
	int free_sect_nr = 0;
	for (i = 0; i < sb->entity.nr_smap_sects; i++) {
		// smap_blk0_nr + i : current sect nr.
		read_sector(smap_blk0_nr + i);
		/* byte offset in current sect */
		for (j = 0; j < storage->Block_Size && nr_sects_to_alloc > 0; j++) {
			byte* bufp = (byte*)buffer_sector;
			k = 0;
			if (!free_sect_nr) {
				/* loop until a free bit is found */
				if (bufp[j] == 0xFF) continue;
				for (; ((bufp[j] >> k) & 1) != 0; k++) { }
				free_sect_nr = (i * storage->Block_Size + j) * 8 +
					k - 1 + sb->entity.n_1st_sect;
			}
			for (; k < 8; k++) {
				// repeat till enough bits are set
				if (((bufp[j] >> k) & 1) == 0);
				else plogerro("%s : %d", __FILE__, __LINE__);
				bufp[j] |= (1 << k);
				if (--nr_sects_to_alloc == 0)
					break;
			}
		}
		if (free_sect_nr) /* free bit found, write the bits to smap */
			write_sector(smap_blk0_nr + i);
		if (nr_sects_to_alloc == 0)
			break;
	}
	if (nr_sects_to_alloc == 0);
	else plogerro("%s : %d", __FILE__, __LINE__);
	return free_sect_nr;
}

//// ---- ---- . ---- ---- ////

#endif
