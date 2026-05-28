// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] File Manage - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <c/format/filesys/FAT.h>

extern "C" void Consman_InitializeFreeType();

static byte* buffer = nil;
#define FSBUF_SIZE 0x8000
FileDescriptor* f_desc_table = nil;
stduint f_desc_table_count = 0;

//// ---- ---- SYSCALL ---- ---- ////

//{TODO} relative path
stdsint ProcessBlock::Open(rostr pathname, int flags) {
	int fd = -1;
	for (stduint i = 0; i < self.pfiles.Count(); i++) {
		if (!self.pfiles[i]) {
			fd = i;// find a available fileslot
			break;
		}
	}
	if (fd == -1 && self.pfiles.Count() < DEFAULT_FILES_LIMIT) {
		fd = self.pfiles.Count();
		self.pfiles.Append(nullptr);
	}
	if (fd == -1) {
		plogwarn("no file slot available");
		return -1;
	}
	// find a free slot in f_desc_table (reuse empty slots)
	FileDescriptor* pfd = NULL;
	for (stduint i = 0; i < 0x1000 / sizeof(FileDescriptor); i++) {
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
	int ret = Filesys::Open(pathname, flags, &file, this->cwd);
	
	if (ret < 0 || !file) {
		// plogwarn("pathname %s failed to %s", pathname, (flags & O_RDWR) ? "open" : "create");
		return -1;
	}
	
	self.pfiles[fd] = pfd;
	pfd->vfile = file;
	pfd->fd_mode = flags;
	pfd->fd_pos = 0;
	if (pfd->vfile->f_inode) {
		pfd->vfile->f_inode->ref_count++;
	}
	return fd;
}

stduint ProcessBlock::Rdwt(bool wr_type, stduint fid, Slice slice)
{
	MutexLocal guard(&this->sys_lock);
	_Comment(const) ProcessBlock* pb = this;
	if (fid >= pb->pfiles.Count()) plogerro("BOOM %d", fid);
	if (!pb || fid >= pb->pfiles.Count() || !pb->pfiles[fid] || !pb->pfiles[fid]->vfile) return 0;
	if (this->state == ProcessBlock::State::Invalid) {
		return 0;
	}
	if (wr_type) {
		int mode = pb->pfiles[fid]->fd_mode & O_ACCMODE;
		if (mode != O_WRONLY && mode != O_RDWR) return 0;
	}
	
	vfs_file* file = pb->pfiles[fid]->vfile;
	if (wr_type && (pb->pfiles[fid]->fd_mode & O_APPEND)) {
		pb->pfiles[fid]->fd_pos = file->f_inode->i_size;
	}
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
			// Redirect Magic TTY (~0) to focused TTY
			if ((stduint)file->f_inode->internal_handler == (stduint)~0 && pb->focus_tty) {
				bytes_processed = global_devfs.writfl(pb->focus_tty, Slice{ 0, (stduint)chunk }, (byte*)::buffer);
			}
			else if (file->f_inode && (file->f_inode->i_mode & I_TYPE_MASK) == I_CHAR_SPECIAL) {
				// Bypass global vfs_lock spinlock for character special devices to avoid deadlocks.
				bytes_processed = file->f_inode->i_sb->fs->writfl(file->f_inode->internal_handler, Slice{ file->f_pos, (stduint)chunk }, (const byte*)::buffer);
			}
			else {
				bytes_processed = Filesys::Write(file, ::buffer, chunk);
			}
		}
		else {
			if ((stduint)file->f_inode->internal_handler == (stduint)~0 && pb->focus_tty) {
				bytes_processed = global_devfs.readfl(pb->focus_tty, Slice{ 0, (stduint)chunk }, (byte*)::buffer);
			}
			else if (file->f_inode && (file->f_inode->i_mode & I_TYPE_MASK) == I_CHAR_SPECIAL) {
				// Bypass global vfs_lock spinlock for character special devices to avoid deadlocks.
				bytes_processed = file->f_inode->i_sb->fs->readfl(file->f_inode->internal_handler, Slice{ file->f_pos, (stduint)chunk }, (byte*)::buffer);
			}
			else {
				bytes_processed = Filesys::Read(file, ::buffer, chunk);
			}
			if (bytes_processed > 0) {
				MemCopyP((void*)curr_addr, pb->paging, ::buffer, kernel_paging, bytes_processed);
			}
			// ploginfo("read: %x, buf: %s", pb->paging_redirect, ::buffer);
		}
		// address from user-space, so use MemCopyP but MccaMemCopyP

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
	MutexLocal guard(&this->sys_lock);
	int fd = fid;
	if (fd < 0 || fd >= (stdsint)self.pfiles.Count() || !self.pfiles[fd]) {
		ploginfo("%s %d skip", __FUNCIDEN__, fd);
		return false;
	}
	if (self.pfiles[fd]->vfile) {
		if (self.pfiles[fd]->vfile->f_inode)
			self.pfiles[fd]->vfile->f_inode->ref_count--;
		Filesys::Close(self.pfiles[fd]->vfile);
		self.pfiles[fd]->vfile = nullptr;
	}
	self.pfiles[fd] = nullptr;
	return true;
}

//{} unchk unused
stdsint ProcessBlock::Seek(int fd, stdsint off, int whence) {
	ProcessBlock* const pb = this;

	// Validate ProcessBlock and File Descriptor
	if (!pb || fd < 0 || fd >= (stdsint)pb->pfiles.Count() || !pb->pfiles[fd] || !pb->pfiles[fd]->vfile) {
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

stdsint ProcessBlock::Dup2(int oldfd, int newfd) {
	MutexLocal guard(&this->sys_lock);

	// Validate original file descriptor
	if (oldfd < 0 || oldfd >= (stdsint)self.pfiles.Count() || !self.pfiles[oldfd]) {
		return -1;
	}

	// POSIX standard: doing nothing if oldfd equals newfd
	if (oldfd == newfd) {
		return newfd;
	}

	// Close the target file descriptor first if it is open
	if (newfd >= 0 && newfd < (stdsint)self.pfiles.Count() && self.pfiles[newfd]) {
		if (self.pfiles[newfd]->vfile) {
			if (self.pfiles[newfd]->vfile->f_inode) {
				self.pfiles[newfd]->vfile->f_inode->ref_count--;
			}
			Filesys::Close(self.pfiles[newfd]->vfile);
			self.pfiles[newfd]->vfile = nullptr;
		}
		self.pfiles[newfd] = nullptr;
	}

	// Expand the process file descriptors vector size if needed
	while (self.pfiles.Count() <= (stduint)newfd) {
		self.pfiles.Append(nullptr);
	}

	// Duplicate descriptor to the target slot
	self.pfiles[newfd] = FileDescriptor_Clone(self.pfiles[oldfd]);
	if (!self.pfiles[newfd]) {
		return -1;
	}

	return newfd;
}

stdsint ProcessBlock::Pipe(int pipefd[2]) {
	MutexLocal guard(&this->sys_lock);
	vfs_file* reader = nullptr;
	vfs_file* writer = nullptr;

	if (Filesys::CreatePipe(&reader, &writer) < 0) {
		return -1;
	}

	// Helper logic to allocate fd and bind FileDescriptor in fileman
	auto alloc_fd = [this](vfs_file* file, int mode) -> int {
		int fd = -1;
		for (stduint i = 0; i < self.pfiles.Count(); i++) {
			if (!self.pfiles[i]) {
				fd = i;
				break;
			}
		}
		if (fd == -1 && self.pfiles.Count() < DEFAULT_FILES_LIMIT) {
			fd = self.pfiles.Count();
			self.pfiles.Append(nullptr);
		}
		if (fd == -1) return -1;

		FileDescriptor* pfd = nullptr;
		for (stduint i = 0; i < 0x1000 / sizeof(FileDescriptor); i++) {
			if (f_desc_table[i].vfile == nullptr) {
				pfd = &f_desc_table[i];
				if (i >= f_desc_table_count) f_desc_table_count = i + 1;
				break;
			}
		}
		if (!pfd) return -1;

		self.pfiles[fd] = pfd;
		pfd->vfile = file;
		pfd->fd_mode = mode;
		pfd->fd_pos = 0;
		return fd;
	};

	int fd_r = alloc_fd(reader, O_RDONLY);
	int fd_w = alloc_fd(writer, O_WRONLY);

	if (fd_r < 0 || fd_w < 0) {
		if (fd_r >= 0) Close(fd_r);
		if (fd_w >= 0) Close(fd_w);
		return -1;
	}

	// Safely copy fd values to user space array
	MccaMemCopyP(pipefd, this, &fd_r, nullptr, sizeof(int));
	MccaMemCopyP(pipefd + 1, this, &fd_w, nullptr, sizeof(int));

	return 0;
}

FileDescriptor* FileDescriptor_Clone(FileDescriptor* src) {
	if (!src || !src->vfile) return nullptr;
	for (stduint i = 0; i < 0x1000 / sizeof(FileDescriptor); i++) {
		if (f_desc_table[i].vfile == nullptr) {
			FileDescriptor* pfd = &f_desc_table[i];
			*pfd = *src;
			// Clone the vfs_file object to avoid shared destruction
			vfs_file* nvfile = (vfs_file*)malc(sizeof(vfs_file));
			if (!nvfile) return nullptr;
			*nvfile = *(src->vfile);
			pfd->vfile = nvfile;
			// Increment ref count of the inode
			if (pfd->vfile->f_inode) {
				pfd->vfile->f_inode->ref_count++;
				if ((pfd->vfile->f_inode->i_mode & I_TYPE_MASK) == I_NAMED_PIPE) {
					PipeChannel* chan = (PipeChannel*)pfd->vfile->f_inode->internal_handler;
					if (chan) {
						chan->lock.Acquire();
						if ((pfd->vfile->f_mode & O_ACCMODE) == O_RDONLY) {
							chan->reader_count++;
						} else if ((pfd->vfile->f_mode & O_ACCMODE) == O_WRONLY) {
							chan->writer_count++;
						}
						chan->lock.Release();
					}
				}
			}
			return pfd;
		}
	}
	return nullptr;
}

//// ---- ---- SERVICE ---- ---- ////

#if 1
void serv_file_loop()// for IDE 0:0, 0:1
{
	f_desc_table = (FileDescriptor*)new byte[0x1000];
	f_desc_table_count = nil;
	::buffer = new byte[FSBUF_SIZE];
	constexpr stduint pathbuf_size = 512;
	byte* const pathbuf = new byte[pathbuf_size];
	//
	stduint to_args[8];// 8*4=32 bytes
	stduint sig_type = 0, sig_src = 0;
	stduint retval[1];
	::ThreadBlock* tb;

	while (true) {
		switch ((FilemanMsg)sig_type)
		{
		case FilemanMsg::TEST:// (no-feedback)
		{
			// Init
			#if (_MCCA & 0xFF00) == 0x8600
			syssend(Task_Memdisk_Serv, &retval, sizeof(retval[0]), _IMM(FiledevMsg::RUPT));
			#if _MCCA == 0x8632
			syssend(Task_Hdd_Serv, &retval, sizeof(retval[0]), _IMM(FiledevMsg::RUPT));// while (!fileman_hd_ready);
			syssend(Task_Flp_Serv, &retval, sizeof(retval[0]), _IMM(FiledevMsg::RUPT));
			#endif
			Filesys::Tree();
			Consman_InitializeFreeType();
			ProcessBlock* init_p = Taskman::CreateFile(("/md0/init"), RING_U, Task_Kernel);
			if (init_p) {
				init_p->focus_tty = vttys[0];
				Taskman::Append(init_p);
				Taskman::AppendThread(init_p->main_thread);
			}
			else {
				plogerro("Failed to load /md0/init");
			}
			#endif

			// Shell
			#if (_MCCA & 0xFF00) == 0x8600
			#if _GUI_ENABLE
			ploginfo("Loading first Shell...");
			ProcessBlock* shell_p = Taskman::Create((void*)&serv_shell_process, RING_M);
			ploginfo("Create new shell-form: pid%u", shell_p ? shell_p->pid : 0);
			#else
			ProcessBlock* p;
			p = Taskman::CreateFile(("/md0/cot"), RING_U, Task_Kernel);
			p->focus_tty = vttys[0];
			if (p->focus_tty) {
				p->Open("/dev/tty", O_RDWR); // stdin
				p->Open("/dev/tty", O_RDWR); // stdout
				p->Open("/dev/tty", O_RDWR); // stderr
			}
			Taskman::Append(p);
			Taskman::AppendThread(p->main_thread);
			#endif

			#elif (_MCCA & 0xFF00) == 0x1000
			syssend(Task_Memdisk_Serv, &retval, sizeof(retval[0]), _IMM(FiledevMsg::RUPT));
			ProcessBlock* p;
			p = Taskman::CreateFile(("/md0/lpa.elf"), RING_U, Task_Kernel);
			p->focus_tty = vttys[0];
			Taskman::Append(p);
			Taskman::AppendThread(p->main_thread);
			#endif
			break;
		}
		case FilemanMsg::RUPT:// (usercall-forbidden&meaningless)
			break;
		case FilemanMsg::OPEN://(flags, thid, usr_filename) | open a file and return the file descriptor
		{
			stduint retval = ~_IMM0;
			ProcessBlock* safe_pb = ProcessBlock::Acquire(to_args[1]);
			if (safe_pb) {
				auto len = StrCopyP((char*)pathbuf, kernel_paging, (rostr)to_args[2], safe_pb->paging, pathbuf_size);
				// ploginfo("[fileman] TID %u open %s with %[32H]", to_args[1], pathbuf, to_args[0]);
				retval = static_cast<stduint>(safe_pb->Open((rostr)pathbuf, to_args[0]));
				ProcessBlock::Release(safe_pb);
			}
			syssend(sig_src, &retval, sizeof(retval), 0);
			break;
		}
		case FilemanMsg::CLOSE:// -> 0 for success
		{
			stdsint ret = -1;
			ProcessBlock* safe_pb = ProcessBlock::AcquireByPID(to_args[1]);
			if (safe_pb) {
				ret = safe_pb->Close(to_args[0]) ? 0 : -1;// POSIX Close
				ProcessBlock::Release(safe_pb);
			}
			syssend(sig_src, &ret, sizeof(ret), 0);
			break;
		}
		case FilemanMsg::READ://
		case FilemanMsg::WRITE://
		{

			ProcessBlock* safe_pb = ProcessBlock::Acquire(to_args[3]);
			if (safe_pb) {
				stduint ret = safe_pb->Rdwt(
					(FilemanMsg)sig_type == FilemanMsg::WRITE,
					to_args[0], Slice{ to_args[1], to_args[2] }
				);
				syssend(sig_src, &ret, sizeof(ret));
				ProcessBlock::Release(safe_pb);
			}
			else {
				stduint err_ret = 0;
				if (to_args[3] != sig_src) syssend(sig_src, &err_ret, sizeof(err_ret));
			}
			// ploginfo("[fileman] rdwt %d %d %d", to_args[0], to_args[1], to_args[2]);
			break;
		}
		case FilemanMsg::REMOVE:// --> (pid, filename[7]...) --> (!success)
		{
			retval[0] = ~_IMM0;
			ProcessBlock* safe_pb = ProcessBlock::Acquire(to_args[1]);
			if (safe_pb) {
				auto len = StrCopyP((char*)pathbuf, kernel_paging, (rostr)to_args[0], safe_pb->paging, pathbuf_size);
				if (len > 0) {
					retval[0] = Filesys::Remove((rostr)pathbuf, safe_pb->cwd);
				}
				ProcessBlock::Release(safe_pb);
			}
			syssend(sig_src, &retval, sizeof(retval[0]));
			break;
		}
		case FilemanMsg::ENUMER:
		{
			ProcessBlock* safe_pb = ProcessBlock::Acquire(to_args[3]);

			stduint ret = 0;
			if (safe_pb) {
				int fd = to_args[0];
				if (fd >= 0 && fd < (stdsint)safe_pb->pfiles.Count() && safe_pb->pfiles[fd] && safe_pb->pfiles[fd]->vfile) {
					ret = Filesys::Enumer(safe_pb->pfiles[fd]->vfile, (void*)to_args[1], to_args[2], safe_pb);
				}
				syssend(sig_src, &ret, sizeof(ret));
				ProcessBlock::Release(safe_pb);
			}
			else {
				if (to_args[3] != sig_src) syssend(sig_src, &ret, sizeof(ret));
			}
			break;
		}


		case FilemanMsg::SETD:
		{
			stdsint err_ret = -1;
			ProcessBlock* safe_pb = ProcessBlock::Acquire(to_args[1]);
			if (!safe_pb) {
				syssend(sig_src, &err_ret, sizeof(err_ret));
				break;
			}
			auto len = StrCopyP((char*)pathbuf, kernel_paging, (rostr)to_args[0], safe_pb->paging, pathbuf_size);
			if (len == 0) {
				syssend(sig_src, &err_ret, sizeof(err_ret));
				ProcessBlock::Release(safe_pb);
				break;
			}
			vfs_dentry* target_dentry = Filesys::Index((const char*)pathbuf, safe_pb->cwd);
			if (!target_dentry) {
				syssend(sig_src, &err_ret, sizeof(err_ret));
				ProcessBlock::Release(safe_pb);
				break;
			}
			if (!target_dentry->d_inode || (target_dentry->d_inode->i_mode & I_TYPE_MASK) != I_DIRECTORY) {
				stdsint type_err = -2;
				syssend(sig_src, &type_err, sizeof(type_err));
				ProcessBlock::Release(safe_pb);
				break;
			}
			safe_pb->cwd = target_dentry;
			stdsint ok_ret = 0;
			syssend(sig_src, &ok_ret, sizeof(ok_ret));
			ProcessBlock::Release(safe_pb);
			break;
		}

		case FilemanMsg::GETD:
		{
			stdsint err_ret = -1;
			ProcessBlock* safe_pb = ProcessBlock::Acquire(to_args[2]);
			if (!safe_pb) {
				syssend(sig_src, &err_ret, sizeof(err_ret));
				break;
			}
			stduint usr_buf = to_args[0];
			stduint size = to_args[1];
			if (!usr_buf || size == 0) {
				syssend(sig_src, &err_ret, sizeof(err_ret));
				ProcessBlock::Release(safe_pb);
				break;
			}
			String abs_path = Filesys::getAbsolutePath(safe_pb->cwd);
			stduint path_len = StrLength(abs_path.reference());
			if (size < path_len + 1) {
				stdsint size_err = -2;
				syssend(sig_src, &size_err, sizeof(size_err));
				ProcessBlock::Release(safe_pb);
				break;
			}
			stduint copied = MemCopyP((void*)usr_buf, safe_pb->paging, abs_path.reference(), kernel_paging, path_len + 1);
			stdsint get_ret = (copied < path_len + 1) ? -3 : (stdsint)path_len;
			syssend(sig_src, &get_ret, sizeof(get_ret));
			ProcessBlock::Release(safe_pb);
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
