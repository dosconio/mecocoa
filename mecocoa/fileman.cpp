// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] File Manage - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <c/storage/harddisk.h>

#ifdef _ARC_x86 // x86:
#include <c/format/filesys.h>
#include <c/format/filesys/FAT.h>
#include "../include/filesys.hpp"

using namespace uni;

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

static bool hd_info_valid = false;

//// ---- ---- Kernel-Internal [R0/R1] ---- ---- ////

stduint do_rdwt(bool wr_type, stduint fid, Slice slice, stduint pid)
{
	ProcessBlock* pb = Taskman::Locate(pid);
	if (!pb->pfiles[fid] || !pb->pfiles[fid]->vfile) return 0;
	if (!(pb->pfiles[fid]->fd_mode & O_RDWR)) return 0;
	
	vfs_file* file = pb->pfiles[fid]->vfile;
	file->f_pos = pb->pfiles[fid]->fd_pos; // sync pos 

	int bytes;
	if (wr_type) {
		bytes = vfs_write(file, (const void*)slice.address, slice.length);
	} else {
		bytes = vfs_read(file, (void*)slice.address, slice.length);
	}
	if (bytes > 0) {
		pb->pfiles[fid]->fd_pos += bytes;
	}
	return bytes > 0 ? bytes : 0;
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
static stduint search_file(rostr path)
{
	vfs_dentry* entry = vfs_namei(path);
	return entry ? (stduint)entry->d_inode->internal_handler : nil;
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
	// find a free slot in f_desc_table (reuse empty slots)
	FileDescriptor* pfd = NULL;
	for (int i = 0; i < 0x1000 / sizeof(FileDescriptor); i++) {
		if (f_desc_table[i].vfile == nullptr) {
			pfd = &f_desc_table[i];
			if (i >= f_desc_table_count) f_desc_table_count = i + 1;
			break;
		}
	}
	if (!pfd) {
		plogwarn("no file desc slot available");
		return -1;
	}
	
	vfs_file* file = nullptr;
	int ret = vfs_open(pathname, flags, &file);
	if (ret < 0 || !file) return -1;
	
	process.pfiles[fd] = pfd;
	pfd->vfile = file;
	pfd->fd_mode = flags;
	pfd->fd_pos = 0;
	return fd;
}

int do_close(ProcessBlock& process, int fid)
{
	int fd = fid;
	if (!process.pfiles[fd]) {
		ploginfo("do_close %d skip", fd);
		return 1;
	}
	vfs_close(process.pfiles[fd]->vfile);
	process.pfiles[fd]->vfile = nullptr; // Mark as free for reuse!
	process.pfiles[fd] = nullptr;
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

stduint serv_file_loop_remove(stduint pid, rostr filename) {
	return vfs_remove(filename) ? 0 : -1;
}
extern bool fileman_hd_ready;
bool flag_ready_fileman = false;


void serv_file_loop()// for IDE 0:0, 0:1
{
	f_desc_table = (FileDescriptor*)Memory::physical_allocate(0x1000);
	f_desc_table_count = nil;
	::buffer = (byte*)Memory::physical_allocate(FSBUF_SIZE);

	//
	stduint to_args[8];// 8*4=32 bytes
	stduint sig_type = 0, sig_src;

	Harddisk_PATA_Paged hdds[] { 0x00, 0x01 };

	// VFS Registrations: FAT12/16/32 only
	static FilesysFAT* pfs_fat0 = nullptr;

	file_system_type fs_fat = { "fat", [](StorageTrait& storage, stduint dev) -> FilesysTrait* {
		DiscPartition part(storage, dev);
		if (part.getSlice().length == 0) return nullptr;
		byte sys_id = part.getSlice().sys_id;
		// Determine FAT sub-type from partition sys_id
		int fat_ver;
		if      (sys_id == FILESYS_FAT12)     fat_ver = 12;
		else if (sys_id == FILESYS_FAT16)     fat_ver = 16;
		else if (sys_id == FILESYS_FAT32_CHS) fat_ver = 32;
		else if (sys_id == FILESYS_FAT32_LBA) fat_ver = 32;
		else return nullptr; // not a FAT partition
		
		DiscPartition* p_part = new DiscPartition(storage, dev);
		p_part->getSlice(); // initialize internal slice
		byte* fat_buf = new byte[0x1000];
		FilesysFAT* fs = new FilesysFAT(fat_ver, *p_part, ::buffer, fat_buf);
		if (fs->loadfs()) {
			if (fat_ver == 32 && !pfs_fat0) pfs_fat0 = fs;
			return fs;
		}
		
		delete fs;
		delete p_part;
		delete[] fat_buf;
		return nullptr;
	}, nullptr };
	register_filesystem(&fs_fat);

	stduint retval[1];

	bool ready = false;
	while (true) {
		switch ((FilemanMsg)sig_type)
		{
		case FilemanMsg::TEST:// (no-feedback)
		{
			while (!fileman_hd_ready);
			
			Console.OutFormat("%s", "[Fileman] Subsystem initializing...\n\r");
			vfs_init();
			devfs_register_and_mount();
			
			// Auto Mount Probe
			extern bool ento_gui;
			for (int dev = 0x10; dev <= MINOR_hd6a + 0x0F; dev++) { // Scan typical devs up to logical drives
				// Probe partition type via MBR sys_id to avoid blind loadfs() attempts
				DiscPartition part(hdds[DRV_OF_DEV(dev)], dev);
				byte sys_id = part.getSlice().sys_id;
				if (sys_id == 0x00) continue; // empty/unpartitioned, skip

				char mnt_path[16];
				String(mnt_path, sizeof(mnt_path)).Format("/mnt%d", dev);

				if (vfs_mount(hdds[DRV_OF_DEV(dev)], dev, mnt_path)) {
					Console.OutFormat("[Fileman] Mounted dev %d (sys_id=0x%x)\n\r", dev, sys_id);
				}
			}

			vfs_tree();

			// Function to load and execute an ELF task from a VFS path
			auto load_vfs_app = [&](const char* path, const char* label) {
				vfs_dentry* d = vfs_namei(path);
				if (d && d->d_inode && d->d_inode->i_sb && d->d_inode->i_sb->fs) {
					FileBlockBridge loop_device(d->d_inode->i_sb->fs, d->d_inode->internal_handler, d->d_inode->i_size, 512);
					if (auto task = Taskman::CreateELF(&loop_device, 3)) {
						task->focus_tty = vttys[ento_gui ? 1 : 0];
						printlog(_LOG_INFO, "Loaded %s from %s", label, path);
					}
					else plogerro("%s: ELF Load Fail", label);
				}
				else plogwarn("%s: Not found at %s", label, path);
				};

			if (pfs_fat0) {
				FAT_FileHandle han_buf;
				FilesysSearchArgs args = { &han_buf, nullptr, nullptr, nullptr };
				void* han = pfs_fat0->search("/", &args);

				// appinit (legacy method)
				if (han = pfs_fat0->search("init", &args)) {
					FileBlockBridge loop_device(pfs_fat0, han, ((FAT_FileHandle*)han)->size, 512);
					if (auto task = Taskman::CreateELF(&loop_device, 3))
						task->focus_tty = vttys[ento_gui ? 1 : 0];
					else plogerro("Init: Fail to load");
				}
				else plogerro("Init: Not found");

			 // subappc (VFS absolute path method)
				load_vfs_app("/mnt34/apps/c", "Subappc");
			}
			else {
				plogwarn("No FAT filesystem found to load default apps.");
			}

			// Subapps loading logic handled here if they were registered in VFS
			// ... 

			flag_ready_fileman = true;
			break;
		}
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
				*Taskman::Locate(to_args[1]),
				(rostr)&to_args[2],
				to_args[0]
			));
			syssend(sig_src, &ret, sizeof(ret), 0);
			break;
		}
		case FilemanMsg::CLOSE:// -> 0 for success
		{
			stduint ret;
			ret = do_close(*Taskman::Locate(to_args[1]), to_args[0]);
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
