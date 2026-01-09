// ASCII g++ TAB4 LF
// Attribute: 
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] File Manage - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _STYLE_RUST

#include <c/consio.h>
#include <c/storage/harddisk.h>

#ifdef _ARC_x86 // x86:
#include "../include/atx-x86-flap32.hpp"
#include "cpp/Device/Storage/HD-DEPEND.h"

#include "../include/filesys.hpp"
#include <c/format/filesys/FAT.h>

use crate uni;

static byte* buffer = nil;
#define FSBUF_SIZE 0x8000
FileDescriptor* f_desc_table = nil;
stduint f_desc_table_count = 0;

static stduint dev_drv_map[] = {
	Task_Hdd_Serv, // ZERO
	nil, // {} Floppy
	nil, // {} CDROM
	Task_Hdd_Serv, // {} Hard Disk
	nil, // {} 4 TTY
	nil, // {} SCSI Disk
	
};


//// ---- ---- fs-irrelavant interface ---- ---- ////

extern inode* root_inode;
extern inode* inode_table;

extern super_block superblocks[NR_SUPER_BLOCK];//{TEMP} in Static segment, should be managed in heap

static OrangesFs* pfs;
static const usize ROOT_DEV = MINOR_hd6a;
static FilesysFAT* pfs_fat0;
static const usize ROOT_DEV_FAT0 = MINOR_hd6a + 2;

//// //// ---- //// ////


bool waitfor(stduint mask, stduint val, stduint timeout_second)// return seccess
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

//{} to use
#define NR_CONSOLES	4

//// ---- ---- Kernel-Internal [R0/R1] ---- ---- ////

stduint do_rdwt(bool wr_type, stduint fid, Slice slice, stduint pid)
{
	// ploginfo("%s user(0x%[32H], %u) fid(%u)", __FUNCIDEN__, slice.address, slice.length, fid);
	//
	ProcessBlock* pb = TaskGet(pid);
	// assert
	if (fid < NR_FILE_DESC && buffer && pfs); else {
		plogerro("PANIC @ %s:%u", __FILE__, __LINE__);
		return 0;
	}
	if (!(pb->pfiles[fid]->fd_mode & O_RDWR))
		return 0;
	int pos = pb->pfiles[fid]->fd_pos;
	struct inode * pin = pb->pfiles[fid]->fd_inode;
	// assert
	if (pin >= &inode_table[0] && pin < &inode_table[NR_INODE]); else {
		plogerro("PANIC @ %s:%u. %[32H]<=%[32H]<%[32H]", __FILE__, __LINE__,
			 &inode_table[0], pin, &inode_table[NR_INODE]);
		return 0;
	}
	int imode = pin->entity.i_mode & I_TYPE_MASK;
	if (imode == I_CHAR_SPECIAL) {
		// ploginfo("do_rdwt: %[32H] %[32H]", slice.address, slice.length);
		stduint msg[4];
		msg[0] = pin->entity.i_start_sect;// dev
		msg[1] = slice.address;
		msg[2] = slice.length;
		msg[3] = pid;
		syssend(Task_Con_Serv, msg, byteof(msg), wr_type ? 2 : 1);
		sysrecv(Task_Con_Serv, &msg[0], byteof(msg[0]));
		return msg[0];
	}
	else {
		Harddisk_PATA_Paged hdd(DRV_OF_DEV(pin->i_dev));
		DiscPartition part(hdd, pin->i_dev);
		const stduint blksize = part.Block_Size;
		// assert
		if (pin->entity.i_mode == I_REGULAR || pin->entity.i_mode == I_DIRECTORY); else {
			plogerro("PANIC @ %s:%u", __FILE__, __LINE__);
			return 0;
		}
		int pos_end;
		if (!wr_type)
			pos_end = minof(pos + slice.length, pin->entity.i_size);
		else // write
			pos_end = minof(pos + slice.length, pin->entity.i_nr_sects * blksize);

		int off = pos % blksize;
		int rw_sect_min = pin->entity.i_start_sect + (pos / blksize);
		int rw_sect_max = pin->entity.i_start_sect + (pos_end / blksize);
		int chunk = minof(rw_sect_max - rw_sect_min + 1,
				FSBUF_SIZE / blksize);

		int bytes_rw = 0;
		int bytes_left = slice.length;
		int i;
		for (i = rw_sect_min; i <= rw_sect_max; i += chunk) {
			/* read/write this amount of bytes every time */
			int bytes = minof(bytes_left, chunk * blksize - off);
			for0(j, chunk) part.Read(i + j, ::buffer);
			if (!wr_type) { // READ
				MemCopyP((void*)(slice.address + bytes_rw), pb->paging,
					::buffer + off, kernel_paging, bytes);
			}
			else { // WRIT (Read first because the write can begin at any position)
				MemCopyP(::buffer + off, kernel_paging,
					(void*)(slice.address + bytes_rw), pb->paging, bytes);
				for0(j, chunk) part.Write(i + j, ::buffer);
			}
			off = 0;
			bytes_rw += bytes;
			pb->pfiles[fid]->fd_pos += bytes;
			bytes_left -= bytes;
		}

		if (pb->pfiles[fid]->fd_pos > pin->entity.i_size) {
			pin->entity.i_size = pb->pfiles[fid]->fd_pos;// update inode::size
			pfs->sync_inode(pin);// write the updated i-node back to disk
		}
		return bytes_rw;
	}
}


//// ---- ---- Partition & Harddisk_PATA_Paged Agent ---- ---- ////

void DiscPartition::renew_slice() {
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
bool Harddisk_PATA_Paged::Write(stduint BlockIden, const void* Sors) {
	stduint to_args[2];
	to_args[0] = getLowID();//{} ide00 ide01
	to_args[1] = BlockIden;
	syssend(Task_Hdd_Serv, sliceof(to_args), _IMM(FiledevMsg::WRITE));
	syssend(Task_Hdd_Serv, Sors, Block_Size);
	return true;
}


//// ---- ---- fs-irrelavant interface ---- ---- ////

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
	bool state = pfs->create(path, flags, &ret);
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
			pin = create_file(pathname, flags);
		}
	}
	else if (flags & O_RDWR) { // file exists
		if ((flags & O_CREAT) && (!(flags & O_TRUNC))) {
			// assert(flags == (O_RDWR | O_CREAT));
			ploginfo("{FS} file exists: %s\n", pathname);
			return -1;
		}
		//
		char filename[MAX_FILENAME_LEN] = { 0 };
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
	//
	if (flags & O_TRUNC) {
		if(pin); else plogerro("%s %d", __FILE__, __LINE__);
		pin->entity.i_size = 0;
		pfs->sync_inode(pin);
	}
	//
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
			// dev = pin->entity.i_start_sect
			// send_recv(BOTH, dd_map[MAJOR(dev)].driver_nr, &driver_msg{OPEN, MINOR(dev)});
			//{TODO} check dev and optional activate
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
	// ploginfo("do_open %s with fd %d", pathname, fd);
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
	// ploginfo("do_close %d", fd);
	return 0;
}
stdsint do_lseek()
{
	stdsint pos;
//	int fd = fs_msg.FD;
//	int off = fs_msg.OFFSET;
//	int whence = fs_msg.WHENCE;
//
//	int pos = pb->pfiles[fd]->fd_pos;
//	int f_size = pb->pfiles[fd]->fd_inode->i_size;
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
//	pb->pfiles[fd]->fd_pos = pos;
	return pos;
}

//// ---- ---- SERVICE ---- ---- ////
char _buf_OFs[byteof(OrangesFs)];
char _buf_FATs[byteof(FilesysFAT)];

stduint serv_file_loop_remove(stduint pid, rostr filename) {
	_TEMP;
	pid;
	return pfs->remove(filename) ? 0 : -1;
}
extern bool fileman_hd_ready;
bool flag_ready_fileman = false;


static FAT_FileHandle filhan;
// static FAT_DirInfo filinf;
void serv_file_loop()
{
	// Manually Initialize
	{
		OrangesFs::IndexOFs = IndexOFs;
		OrangesFs::IndexFs = IndexFs;
	}
	f_desc_table = (FileDescriptor*)Memory::physical_allocate(0x1000);
	f_desc_table_count = nil;
	::buffer = (byte*)Memory::physical_allocate(FSBUF_SIZE);
	stduint&& bufsize = 512 * 64;// 32K
	void* load_buffer = Memory::physical_allocate(bufsize);
	// ploginfo("%s, buffer %[32H]", __FUNCIDEN__, ::buffer);
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

	Harddisk_PATA_Paged hdd(0x01);

	pfs = new (_buf_OFs) OrangesFs(hdd, ::buffer, ROOT_DEV);
	DiscPartition part_fat0(hdd, ROOT_DEV_FAT0);
	pfs_fat0 = new (_buf_FATs) FilesysFAT(32, part_fat0, ::buffer, ROOT_DEV_FAT0);
	pfs_fat0->buffer_fatable = (byte*)Memory::physical_allocate(0x1000);

	stduint retval[1];


	FAT_FileHandle* han;
	stduint a[2] = { _IMM(&filhan)/*, _IMM(&filinf) */ };

	bool ready = false;
	while (true) {
		switch ((FilemanMsg)sig_type)
		{
		case FilemanMsg::TEST:// (no-feedback)
			while (!fileman_hd_ready);
			
			if (1) pfs->makefs(NULL);
			ready = pfs->loadfs();
			root_inode = pfs->get_inode(ROOT_INODE);

			ready = pfs_fat0->loadfs();
			if (ready) {
				Console.OutFormat("%s", "[Fileman] FAT file system is ready.\n\r");
			}
			else Console.OutFormat("[Fileman] FAT file system is not ready (%u).\n\r", pfs_fat0->error_number);

			han = (FAT_FileHandle*)pfs_fat0->search("/", &a);
			pfs_fat0->enumer(han, NULL);
			// appinit
			printlog(_LOG_INFO, "Loading Appinit -> %x", load_buffer);
			if (han = (FAT_FileHandle*)pfs_fat0->search("init", &a)) {
				if (pfs_fat0->readfl(han, Slice{ 0,han->size }, (byte*)load_buffer))
					TaskLoad(NULL _TEMP, load_buffer, 3)->focus_tty_id = 0;
				else plogerro("Init: Fail to load");
			}
			else plogerro("Init: Not found");

			// subappc
			printlog(_LOG_INFO, "Loading Subappc");
			if (han = (FAT_FileHandle*)pfs_fat0->search("c", &a)) {
				if (pfs_fat0->readfl(han, Slice{ 0,han->size }, (byte*)load_buffer))
					TaskLoad(NULL _TEMP, load_buffer, 3)->focus_tty_id = 0;
				else plogerro("C: Fail to load");
			}
			else plogerro("C: Not found");

			if (ready) Console.OutFormat("%s", "[Fileman] File system is ready.\n\r");
			flag_ready_fileman = true;
			break;
		case FilemanMsg::RUPT:// (usercall-forbidden&meaningless)
			break;
		case FilemanMsg::OPEN://
		{
			// open a file and return the file descriptor
			// ploginfo("[fileman] PID %u open %s with %[32H]", to_args[1], &to_args[2], to_args[0]);
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
		case FilemanMsg::CLOSE:// -> 0 for success
		{
			stduint ret;
			ret = do_close(*TaskGet(to_args[1]), to_args[0]);
			syssend(sig_src, &ret, sizeof(ret), 0);
			break;
		}
		case FilemanMsg::READ://
		case FilemanMsg::WRITE://
		{
			stduint ret = do_rdwt((FilemanMsg)sig_type == FilemanMsg::WRITE,
				to_args[0], Slice{ to_args[1], to_args[2] }, to_args[3]);
			syssend(sig_src, &ret, sizeof(ret));
			break;
		}
		case FilemanMsg::REMOVE:// --> (pid, filename[7]...) --> (!success)
		{
			retval[0] = serv_file_loop_remove(to_args[0], (rostr)&to_args[1]);
			syssend(sig_src, &retval, sizeof(retval[0]));
			break;
		}
		// ... ...
		
		case FilemanMsg::TEMP:
		{
			ploginfo("- %s", to_args[0]);
			syssend(sig_src, &retval, sizeof(retval[0]));
			break;
		}

		// ... ...
		default:
			plogerro("Bad TYPE in %s %s", __FILE__, __FUNCIDEN__);
			break;
		}
		sysrecv(ANYPROC, to_args, byteof(to_args), &sig_type, &sig_src);
		// plogwarn("Fileman: get from %d", sig_src);
	}
}

#endif
