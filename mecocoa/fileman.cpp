// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] File Manage - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <c/format/filesys/FAT.h>

static byte* buffer = nil;
#define FSBUF_SIZE 0x8000
FileDescriptor* f_desc_table = nil;
stduint f_desc_table_count = 0;

//// ---- ---- SYSCALL ---- ---- ////

//{TODO} relative path
stdsint ProcessBlock::Open(rostr pathname, int flags) {
	int fd = -1;
	for0a(i, self.pfiles) if (!self.pfiles[i]) {
		fd = i;// find a available fileslot
		break;
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
	int ret = Filesys::Open(pathname, flags, &file);
	
	if (ret < 0 || !file) {
		// plogwarn("pathname %s failed to %s", pathname, (flags & O_RDWR) ? "open" : "create");
		return -1;
	}
	
	self.pfiles[fd] = pfd;
	pfd->vfile = file;
	pfd->fd_mode = flags;
	pfd->fd_pos = 0;
	return fd;
}

stduint ProcessBlock::Rdwt(bool wr_type, stduint fid, Slice slice)
{
	_Comment(const) ProcessBlock* pb = this;
	if (!pb || !pb->pfiles[fid] || !pb->pfiles[fid]->vfile) return 0;
	if (wr_type) {
		if (!(pb->pfiles[fid]->fd_mode & O_RDWR)) return 0;
	}
	
	vfs_file* file = pb->pfiles[fid]->vfile;
	file->f_pos = pb->pfiles[fid]->fd_pos; // sync pos 

	int total_bytes = 0;
	int bytes_left = slice.length;
	stduint curr_addr = slice.address;

	// Loop to process data in chunks due to kernel buffer size limits
	// and to safely transfer data across different address spaces.
	while (bytes_left > 0) {
		// Calculate the chunk size for this iteration
		int chunk = minof(bytes_left, FSBUF_SIZE);
		int bytes_processed = 0;

		if (wr_type) {
			MemCopyP(::buffer, kernel_paging, (void*)curr_addr, pb->paging, chunk);
			bytes_processed = Filesys::Write(file, ::buffer, chunk);
		}
		else {
			bytes_processed = Filesys::Read(file, ::buffer, chunk);
			if (bytes_processed > 0) {
				MemCopyP((void*)curr_addr, pb->paging, ::buffer, kernel_paging, bytes_processed);
			}
		}

		// Break if error occurred or EOF reached
		if (bytes_processed <= 0) {
			break;
		}

		// Update tracking variables and file descriptors
		total_bytes += bytes_processed;
		curr_addr += bytes_processed;
		bytes_left -= bytes_processed;
		pb->pfiles[fid]->fd_pos += bytes_processed;
		
		// Update the underlying VFS file position 
		file->f_pos = pb->pfiles[fid]->fd_pos;

		// If the processed bytes are less than the chunk requested, we likely hit the end of the file.
		if (bytes_processed < chunk) {
			break;
		}
	}

	return total_bytes > 0 ? total_bytes : 0;
}

bool ProcessBlock::Close(int fid)
{
	int fd = fid;
	if (!self.pfiles[fd]) {
		ploginfo("%s %d skip", __FUNCIDEN__, fd);
		return false;
	}
	Filesys::Close(self.pfiles[fd]->vfile);
	self.pfiles[fd]->vfile->f_inode->ref_count--;
	self.pfiles[fd]->vfile = nullptr; // Mark as free for reuse
	self.pfiles[fd] = nullptr;
	return true;
}

//{} unchk unused
stdsint ProcessBlock::Seek(int fd, stdsint off, int whence) {
	ProcessBlock* const pb = this;

	// Validate ProcessBlock and File Descriptor
	if (!pb || fd < 0 || !pb->pfiles[fd] || !pb->pfiles[fd]->vfile) {
		return -1;
	}

	vfs_file* file = pb->pfiles[fd]->vfile;
	if (!file->f_inode) return -1;

	stdsint pos = pb->pfiles[fd]->fd_pos;
	stdsint f_size = file->f_inode->i_size;

	// SEEK_SET: 0, SEEK_CUR: 1, SEEK_END: 2
	switch (whence) {
	case 0: // SEEK_SET
		pos = off;
		break;
	case 1: // SEEK_CUR
		pos += off;
		break;
	case 2: // SEEK_END
		pos = f_size + off;
		break;
	default:
		return -1; // Invalid whence
	}

	// POSIX allows seeking past the end of a file (creates a hole on next write),
	// but seeking before the beginning of the file is strictly an error.
	if (pos < 0) {
		return -1;
	}

	// strictly prevent seeking past EOF in your OS
	MIN(pos, f_size);

	// Update position in both the process file descriptor and the VFS file object
	pb->pfiles[fd]->fd_pos = pos;
	file->f_pos = pos;

	return pos;
}

//// ---- ---- SERVICE ---- ---- ////

#ifdef _ARC_x86 // x86:
extern String* plab;
#endif

#if 1
void serv_file_loop()// for IDE 0:0, 0:1
{
	f_desc_table = (FileDescriptor*)new byte[0x1000];
	f_desc_table_count = nil;
	::buffer = new byte[FSBUF_SIZE];
	//
	stduint to_args[8];// 8*4=32 bytes
	stduint sig_type = 0, sig_src;
	stduint retval[1];

	while (true) {
		switch ((FilemanMsg)sig_type)
		{
		case FilemanMsg::TEST:// (no-feedback)
		{
			#ifdef _ARC_x86
			syssend(Task_Hdd_Serv, &retval, sizeof(retval[0]), _IMM(FiledevMsg::RUPT));// while (!fileman_hd_ready);
			if (plab) {
				extern bool ento_gui;
				ProcessBlock* p;
				p = Taskman::CreateFile((*plab + "/init").reference(), RING_U, Task_Kernel);
				p->focus_tty = vttys[ento_gui ? 1 : 0];
				Taskman::Append(p);
				Taskman::AppendThread(p->main_thread);
			}
			else plogerro("No fs for INIT");

			#elif (_MCCA & 0xFF00) == 0x1000
			syssend(Task_Memdisk_Serv, &retval, sizeof(retval[0]), _IMM(FiledevMsg::RUPT));
			ProcessBlock* p;
			p = Taskman::CreateFile(("/md0/lpa.elf"), RING_U, Task_Kernel);
			Taskman::Append(p);
			Taskman::AppendThread(p->main_thread);
			#endif
			break;
		}
		case FilemanMsg::RUPT:// (usercall-forbidden&meaningless)
			break;
		#ifdef _ARC_x86
		case FilemanMsg::OPEN:// open a file and return the file descriptor
		{
			// ploginfo("[fileman] PID %u open %s with %[32H]", to_args[1], &to_args[2], to_args[0]);
			// BYTE 0~ 3 : flags
			// BYTE 4~ 7 : processid
			// BYTE 8~31 : filename
			stduint retval = static_cast<stduint>(Taskman::Locate(to_args[1])->Open((rostr)&to_args[2], to_args[0]));
			syssend(sig_src, &retval, sizeof(retval), 0);
			break;
		}
		case FilemanMsg::CLOSE:// -> 0 for success
		{
			stduint ret;
			ret = Taskman::Locate(to_args[1])->Close(to_args[0]);
			syssend(sig_src, &ret, sizeof(ret), 0);
			break;
		}
		case FilemanMsg::READ://
		case FilemanMsg::WRITE://
		{
			// ploginfo("[fileman] rdwt %d %d %d", to_args[0], to_args[1], to_args[2]);
			stduint ret = Taskman::Locate(to_args[3])->Rdwt((FilemanMsg)sig_type == FilemanMsg::WRITE,
				to_args[0], Slice{ to_args[1], to_args[2] });
			syssend(sig_src, &ret, sizeof(ret));
			break;
		}
		case FilemanMsg::REMOVE:// --> (pid, filename[7]...) --> (!success)
		{
			retval[0] = Filesys::Remove((rostr)&to_args[1]);
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
		#endif
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
