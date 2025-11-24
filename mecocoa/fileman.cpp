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
FileDescriptor* f_desc_table = nil;
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

//// ---- ---- Partition & Harddisk_PATA_Paged Agent ---- ---- ////

void Partition::renew_slice() {
	stduint args[1];
	args[0] = self.device;
	syssend(Task_Hdd_Serv, sliceof(args), _IMM(FiledevMsg::GETPS));
	sysrecv(Task_Hdd_Serv, &self.slice, sizeof(self.slice));
}

bool Harddisk_PATA_Paged::Read(stduint BlockIden, void* Dest) {
	stduint to_args[2];
	to_args[0] = getLowID();//{} ide00 ide01
	to_args[1] = BlockIden;
	syssend(Task_Hdd_Serv, sliceof(to_args), _IMM(FiledevMsg::READ));
	sysrecv(Task_Hdd_Serv, Dest, Block_Size);
	return true;
}
bool Partition::Read(stduint BlockIden, void* Dest) {
	if (!slice.address && !slice.length) renew_slice();
	// ONLY for IDE00 and IDE01
	return Harddisk_PATA_Paged(DRV_OF_DEV(self.device)).Read(BlockIden + slice.address, Dest);
}

bool Harddisk_PATA_Paged::Write(stduint BlockIden, const void* Sors) {
	stduint to_args[2];
	to_args[0] = getLowID();//{} ide00 ide01
	to_args[1] = BlockIden;
	syssend(Task_Hdd_Serv, sliceof(to_args), _IMM(FiledevMsg::WRITE));
	syssend(Task_Hdd_Serv, Sors, Block_Size);
	return true;
}
bool Partition::Write(stduint BlockIden, const void* Sors) {
	if (!slice.address && !slice.length) renew_slice();
	// ONLY for IDE00 and IDE01
	return Harddisk_PATA_Paged(DRV_OF_DEV(self.device)).Write(BlockIden + slice.address, Sors);
}

//// ---- ---- fs-irrelavant interface ---- ---- ////

extern inode* root_inode;
extern inode* inode_table;

extern super_block superblocks[NR_SUPER_BLOCK];//{TEMP} in Static segment, should be managed in heap

OrangesFs* pfs; const usize ROOT_DEV = MINOR_hd6a; 

static OrangesFs* IndexOFs(unsigned dev) {
	return ROOT_DEV == dev ? pfs : NULL;
} OrangesFs* (*OrangesFs::IndexOFs)(unsigned) = IndexOFs;

static FilesysTrait* IndexFs(unsigned dev) {
	return ROOT_DEV == dev ? pfs : NULL;
} FilesysTrait* (*OrangesFs::IndexFs)(unsigned) = IndexFs;





// Return success.
// * e.g. After strip_path(filename, "/blah", ppinode):
// *      - filename: "blah"
// *      - *ppinode: root_inode
bool strip_path(char* filename, const char* pathname, inode** ppinode)
{
	const char* s = pathname;
	char* t = filename;
	//{} assert
	if (*s == '/') s++;
	while (*s) {
		if (*s == '/') {
			//{TEMP} at most one `/' preceding a filename
			return false;
		}
		*t++ = *s++;
		if (t - filename >= MAX_FILENAME_LEN - 1) break;
	}
	*t = 0;
	*ppinode = root_inode;
	return true;
}

static inode* create_file(rostr path, stduint flags)
{
	_TEMP;
	inode* ret;
	bool state = pfs->create(path, flags, (stduint*)&ret);
	return state ? ret : NULL;
}

static stduint search_file(rostr path)
{
	_TEMP;
	stduint ret = 0;
	bool state = pfs->search(path, &ret);
	return state ? ret : nil;
}


//// ---- ---- SYSCALL ---- ---- ////


static stdsint do_open(ProcessBlock& process, rostr pathname, int flags) {
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
	FileDescriptor* pfd = NULL;
	if (f_desc_table_count >= 0x1000 / sizeof(FileDescriptor)) {
		plogwarn("no file desc slot available");
		return -1;
	}
	pfd = &f_desc_table[f_desc_table_count++];
	stduint inode_nr = search_file(pathname);
	inode* pin = 0;
	if (flags & O_CREAT) {
		if (inode_nr) {
			ploginfo("file `%s' exists: %u", pathname, inode_nr);
			return -1;
		}
		else {
			ploginfo("creating file...");
			pin = create_file(pathname, flags);
			ploginfo("creating file finish");
		}
	}
	else if (flags & O_RDWR) {
		char filename[MAX_FILENAME_LEN] = {0};
		struct inode * dir_inode;
		if (!strip_path(filename, pathname, &dir_inode)) {
			plogwarn("bad filepath");
			return -1;
		}
		pin = IndexOFs(dir_inode->i_dev)->get_inode(inode_nr);//{!} NULL-chk
	}
	else {
		plogwarn("bad flags");
		return -1;
	}
	if (pin) {
		// connects proc with FileDescriptorriptor
		process.pfiles[fd] = pfd;
		/* connects FileDescriptorriptor with inode */
		pfd->fd_inode = pin;
		pfd->fd_mode = flags;
		// pfd->fd_cnt = 1;
		pfd->fd_pos = 0;

		int imode = pin->entity.i_mode & I_TYPE_MASK;

		if (imode == I_CHAR_SPECIAL) {
			plogerro("!!! unfinished in %s", __FUNCIDEN__);
			//MESSAGE driver_msg;
			//driver_msg.type = DEV_OPEN;
			//int dev = pin->entity.i_start_sect;
			//driver_msg.DEVICE = MINOR(dev);
			//assert(MAJOR(dev) == 4);
			//assert(dd_map[MAJOR(dev)].driver_nr != INVALID_DRIVER);
			//send_recv(BOTH, dd_map[MAJOR(dev)].driver_nr, &driver_msg);
		}
		else if (imode == I_DIRECTORY) {
			if (pin->i_num == ROOT_INODE); else
				plogerro("%s %d", __FILE__, __LINE__);
		}
		else {
			if (pin->entity.i_mode == I_REGULAR); else
				plogerro("%s %d", __FILE__, __LINE__);
		}
	}
	else {
		plogwarn("!pin");
		return -1;
	}
	ploginfo("do_open %s with fd %d", pathname, fd);
	return fd;
}
int do_close(ProcessBlock& process, int fid)
{
	int fd = fid;
	if (!process.pfiles[fd]) {
		ploginfo("do_close %d skip", fd);
		return 1;
	}
	OrangesFs::put_inode(process.pfiles[fd]->fd_inode);
	process.pfiles[fd]->fd_inode = 0;
	process.pfiles[fd] = 0;
	ploginfo("do_close %d", fd);
	return 0;
}
//int do_lseek()
//{
//	int fd = fs_msg.FD;
//	int off = fs_msg.OFFSET;
//	int whence = fs_msg.WHENCE;
//
//	int pos = pcaller->filp[fd]->fd_pos;
//	int f_size = pcaller->filp[fd]->fd_inode->i_size;
//
//	switch (whence) {
//	case SEEK_SET:
//		pos = off;
//		break;
//	case SEEK_CUR:
//		pos += off;
//		break;
//	case SEEK_END:
//		pos = f_size + off;
//		break;
//	default:
//		return -1;
//		break;
//	}
//	if ((pos > f_size) || (pos < 0)) {
//		return -1;
//	}
//	pcaller->filp[fd]->fd_pos = pos;
//	return pos;
//}

//// ---- ---- SERVICE ---- ---- ////
char _buf_OFs[byteof(OrangesFs)];

void serv_file_loop()
{
	ploginfo("%s", __FUNCIDEN__);
	// Manually Initialize
	{
		OrangesFs::IndexOFs = IndexOFs;
		OrangesFs::IndexFs = IndexFs;
	}
	f_desc_table = (FileDescriptor*)Memory::physical_allocate(0x1000);
	f_desc_table_count = nil;
	buffer = (byte*)Memory::physical_allocate(0x1000);
	//
	stduint&& inode_table_size = vaultAlignHexpow(0x1000, sizeof(inode) * NR_INODE);
	inode_table = (inode*)Memory::physical_allocate(inode_table_size);
	MemSet(inode_table, 0, sizeof(inode) * NR_INODE);
	//
	NR_FILE_DESC;
	//{TODO}
	//
	NR_SUPER_BLOCK;
	for0(i, 8) superblocks[i] = { 0 };//{why} MemSet here will BOOM!
	//
	stduint to_args[8];// 8*4=32 bytes
	stduint sig_type = 0, sig_src;
	super_block* sb;
	pfs = new (_buf_OFs) OrangesFs(ROOT_DEV, (char*)buffer);
	while (true) {
		switch (sig_type)
		{
		case 0:// TEST (no-feedback)
			// plogtrac("TESTING FILEMAN");
			syssend(Task_Hdd_Serv, &to_args, 0, _IMM(FiledevMsg::TEST));
			if (0)
				pfs->makefs();
			pfs->loadfs();
			root_inode = pfs->get_inode(ROOT_INODE);
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
				*TaskGet(to_args[1]),
				(rostr)&to_args[2],
				to_args[0]
			));
			syssend(sig_src, &ret, sizeof(ret), 0);
			break;
		}
		case 3:// CLOSE -> 0 for success
		{
			stduint ret;
			ret = do_close(*TaskGet(to_args[1]), to_args[0]);
			syssend(sig_src, &ret, sizeof(ret), 0);
			break;
		}
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
