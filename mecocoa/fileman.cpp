// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// ModuTitle: [Service] File Manage - ELF32-C++ x86 Bare-Metal
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"
#include <c/format/filesys/FAT.h>

extern void Consman_InitializeFreeType();

#define FSBUF_SIZE 0x4000
struct FDescData {
	FileDescriptor* table = nullptr;
	stduint count = 0;
};
SpinlockBlock<FDescData> f_desc_info;

static bool CloseFileSlotUnlocked(ProcFiles& files, int fd) {
	if (fd < 0 || fd >= (stdsint)files.pfiles.Count() || !files.pfiles[fd]) {
		return false;
	}
	FileDescriptor* pfd = files.pfiles[fd];
	vfs_file* file = pfd->vfile;
	if (file) {
		if (file->f_inode)
			file->f_inode->ref_count--;
		Filesys::Close(file);
		{
			auto f_desc = f_desc_info.Lock();
			pfd->fd_mode = 0;
			pfd->fd_pos = 0;
			pfd->vfile = nullptr;
		}
	}
	files.pfiles[fd] = nullptr;
	return true;
}

//// ---- ---- SYSCALL ---- ---- ////

//{TODO} relative path
stdsint ProcessBlock::Open(rostr pathname, int flags) {
	auto files = this->fileman.Lock();
	int fd = -1;
	for (stduint i = 0; i < files->pfiles.Count(); i++) {
		if (!files->pfiles[i]) {
			fd = i;// find a available fileslot
			break;
		}
	}
	if (fd == -1 && files->pfiles.Count() < DEFAULT_FILES_LIMIT) {
		fd = files->pfiles.Count();
		files->pfiles.Append(nullptr);
	}
	if (fd == -1) {
		plogwarn("no file slot available");
		return -1;
	}
	// find a free slot in f_desc_table (reuse empty slots)
	FileDescriptor* pfd = NULL;
	{
		auto f_desc = f_desc_info.Lock();
		for (stduint i = 0; i < 0x1000 / sizeof(FileDescriptor); i++) {
			if (f_desc->table[i].vfile == nullptr) {
				pfd = &f_desc->table[i];
				if (i >= f_desc->count) f_desc->count = i + 1;
				pfd->vfile = (vfs_file*)1; // reserve
				break;
			}
		}
	}
	if (!pfd) {
		plogwarn("no file desc slot available");
		return -1;
	}
	
	vfs_file* file = nullptr;
	int ret = Filesys::Open(pathname, flags, &file, files->cwd);
	
	if (ret < 0 || !file) {
		pfd->vfile = nullptr; // release reservation
		// plogwarn("pathname %s failed to %s", pathname, (flags & O_RDWR) ? "open" : "create");
		return -1;
	}
	
	files->pfiles[fd] = pfd;
	pfd->fd_mode = flags;
	pfd->fd_pos = 0;
	if (file->f_inode) {
		file->f_inode->ref_count++;
	}
	pfd->vfile = file;
	return fd;
}

stduint ProcessBlock::Rdwt(bool wr_type, stduint fid, Slice slice)
{
	auto files = this->fileman.Lock();
	_Comment(const) ProcessBlock* pb = this;
	if (!pb || this->state == ProcessBlock::State::Expiring || this->state == ProcessBlock::State::Invalid) {
		return 0;
	}
	if (fid >= files->pfiles.Count()) {
		plogwarn("ProcessBlock::Rdwt BOOM pid=%u state=%u fd=%u count=%u",
			this->getID(), this->state, fid, files->pfiles.Count());
		return 0;
	}
	if (!pb || fid >= files->pfiles.Count() || !files->pfiles[fid] || !files->pfiles[fid]->vfile) return 0;
	if (wr_type) {
		int mode = files->pfiles[fid]->fd_mode & O_ACCMODE;
		if (mode != O_WRONLY && mode != O_RDWR) return 0;
	}
	
	vfs_file* file = files->pfiles[fid]->vfile;
	FileDescriptor* fd_entry = files->pfiles[fid];
	const bool is_magic_tty = file->f_inode &&
		(stduint)file->f_inode->internal_handler == (stduint)~0;

	// Check if this is a pipe that may block during I/O.
	// Pipe I/O must NOT hold fileman lock, because ReadPipe/WritePipe can
	// block the current thread, and other threads (or process cleanup) need
	// to acquire fileman lock to proceed.
	const bool is_pipe = file->f_inode &&
		(file->f_inode->i_mode & I_TYPE_MASK) == I_NAMED_PIPE;

	if (is_magic_tty) {
		// Task_Console owns the focus_tty path so fileman only forwards the
		// request and commits fd_pos after the synchronous reply.
		stduint total_bytes = 0;
		stduint bytes_left = slice.length;
		stduint curr_addr = slice.address;
		int local_fd_pos = fd_entry->fd_pos;

		files.Unlock();
		while (bytes_left > 0) {
			stduint chunk = minof(bytes_left, (stduint)FSBUF_SIZE);
			FMT_ConsoleMsg_RDWR req = { curr_addr, chunk, pb->getID() };
			stduint ret = 0;
			syssend(Task_Console, &req, sizeof(req), _IMM(wr_type ? ConsoleMsg::WRIT : ConsoleMsg::READ));
			sysrecv(Task_Console, &ret, sizeof(ret));
			if ((stdsint)ret <= 0) {
				break;
			}

			total_bytes += ret;
			curr_addr += ret;
			bytes_left -= ret;
			local_fd_pos += (int)ret;
			if (ret < chunk) {
				break;
			}
		}

		auto files_commit = this->fileman.Lock();
		if (fid < files_commit->pfiles.Count() &&
			files_commit->pfiles[fid] == fd_entry &&
			fd_entry->vfile == file) {
			fd_entry->fd_pos = local_fd_pos;
			file->f_pos = local_fd_pos;
		}
		return total_bytes;
	}

	if (wr_type && (files->pfiles[fid]->fd_mode & O_APPEND)) {
		files->pfiles[fid]->fd_pos = file->f_inode->i_size;
	}
	file->f_pos = files->pfiles[fid]->fd_pos; // sync pos

	// For pipes, release fileman lock before doing I/O that may block.
	// ReadPipe/WritePipe can block the thread, and holding fileman lock
	// during that time prevents other threads from accessing file descriptors
	// (e.g., process exit cleanup, dup2, etc.), causing deadlocks.
	if (is_pipe) {
		files.Unlock();
	}

	int total_bytes = 0;
	int bytes_left = slice.length;
	stduint curr_addr = slice.address;
	byte* buffer = new byte[FSBUF_SIZE];

	// Loop to process data in chunks due to kernel buffer size limits
	// and to safely transfer data across different address spaces.
	while (bytes_left > 0) {
		// Calculate the chunk size for this iteration
		int chunk = minof(bytes_left, FSBUF_SIZE);
		int bytes_processed = 0;

		if (wr_type) {
			MemCopyP(buffer, kernel_paging, (void*)curr_addr, pb->paging, chunk);
			if (file->f_inode && (file->f_inode->i_mode & I_TYPE_MASK) == I_CHAR_SPECIAL) {
				// Bypass global vfs_lock spinlock for character special devices to avoid deadlocks.
				bytes_processed = file->f_inode->i_sb->fs->writfl(file->f_inode->internal_handler, Slice{ file->f_pos, (stduint)chunk }, (const byte*)buffer);
			}
			else if (is_pipe) {
				bytes_processed = Filesys::WritePipe(file, buffer, chunk);
			}
			else {
				bytes_processed = Filesys::Write(file, buffer, chunk);
			}
		}
		else {
			if (file->f_inode && (file->f_inode->i_mode & I_TYPE_MASK) == I_CHAR_SPECIAL) {
				// Bypass global vfs_lock spinlock for character special devices to avoid deadlocks.
				bytes_processed = file->f_inode->i_sb->fs->readfl(file->f_inode->internal_handler, Slice{ file->f_pos, (stduint)chunk }, (byte*)buffer);
			}
			else if (is_pipe) {
				bytes_processed = Filesys::ReadPipe(file, buffer, chunk);
			}
			else {
				bytes_processed = Filesys::Read(file, buffer, chunk);
			}
			if (bytes_processed > 0) {
				MemCopyP((void*)curr_addr, pb->paging, buffer, kernel_paging, bytes_processed);
			}
		}

		// Break if error occurred or EOF reached
		if (bytes_processed <= 0) {
			break;
		}

		// Update tracking variables
		total_bytes += bytes_processed;
		curr_addr += bytes_processed;
		bytes_left -= bytes_processed;

		// For pipes, fd_pos is not meaningful, skip update
		if (!is_pipe) {
			files->pfiles[fid]->fd_pos += bytes_processed;
			file->f_pos = files->pfiles[fid]->fd_pos;
		}

		// If the processed bytes are less than the chunk requested, we likely hit the end of the file.
		if (bytes_processed < chunk) {
			break;
		}
	}

	// Re-acquire fileman lock for pipe path to update fd_pos if needed
	if (is_pipe) {
		auto files_reacq = this->fileman.Lock();
		if (fid < files_reacq->pfiles.Count() && files_reacq->pfiles[fid] == fd_entry) {
			files_reacq->pfiles[fid]->fd_pos += total_bytes;
			file->f_pos = files_reacq->pfiles[fid]->fd_pos;
		}
	}

	delete[] buffer;
	return total_bytes > 0 ? total_bytes : 0;
}

bool ProcessBlock::Close(int fid)
{
	auto files = this->fileman.Lock();
	int fd = fid;
	if (fd < 0 || fd >= (stdsint)files->pfiles.Count() || !files->pfiles[fd]) {
		ploginfo("%s %d skip", __FUNCIDEN__, fd);
		return false;
	}
	return CloseFileSlotUnlocked(*files, fd);
}

//{} unchk unused
stdsint ProcessBlock::Seek(int fd, stdsint off, int whence) {
	auto files = this->fileman.Lock();
	ProcessBlock* const pb = this;

	// Validate ProcessBlock and File Descriptor
	if (!pb || fd < 0 || fd >= (stdsint)files->pfiles.Count() || !files->pfiles[fd] || !files->pfiles[fd]->vfile) {
		return -1;
	}

	vfs_file* file = files->pfiles[fd]->vfile;
	if (!file->f_inode) return -1;

	stdsint pos = files->pfiles[fd]->fd_pos;
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
	files->pfiles[fd]->fd_pos = pos;
	file->f_pos = pos;

	return pos;
}

stdsint ProcessBlock::Dup2(int oldfd, int newfd) {
	auto files = this->fileman.Lock();

	// Validate original file descriptor
	if (oldfd < 0 || oldfd >= (stdsint)files->pfiles.Count() || !files->pfiles[oldfd]) {
		return -1;
	}

	// POSIX standard: doing nothing if oldfd equals newfd
	if (oldfd == newfd) {
		return newfd;
	}

	// Close the target file descriptor first if it is open
	if (newfd >= 0 && newfd < (stdsint)files->pfiles.Count() && files->pfiles[newfd]) {
		CloseFileSlotUnlocked(*files, newfd);
	}

	// Expand the process file descriptors vector size if needed
	while (files->pfiles.Count() <= (stduint)newfd) {
		files->pfiles.Append(nullptr);
	}

	// Duplicate descriptor to the target slot
	files->pfiles[newfd] = FileDescriptor_Clone(files->pfiles[oldfd]);
	if (!files->pfiles[newfd]) {
		return -1;
	}

	return newfd;
}

stdsint ProcessBlock::Pipe(int pipefd[2]) {
	auto files = this->fileman.Lock();
	vfs_file* reader = nullptr;
	vfs_file* writer = nullptr;

	if (Filesys::CreatePipe(&reader, &writer) < 0) {
		return -1;
	}

	// Helper logic to allocate fd and bind FileDescriptor in fileman
	auto alloc_fd = [&files](vfs_file* file, int mode) -> int {
		int fd = -1;
		for (stduint i = 0; i < files->pfiles.Count(); i++) {
			if (!files->pfiles[i]) {
				fd = i;
				break;
			}
		}
		if (fd == -1 && files->pfiles.Count() < DEFAULT_FILES_LIMIT) {
			fd = files->pfiles.Count();
			files->pfiles.Append(nullptr);
		}
		if (fd == -1) return -1;

		FileDescriptor* pfd = nullptr;
		{
			auto f_desc = f_desc_info.Lock();
			for (stduint i = 0; i < 0x1000 / sizeof(FileDescriptor); i++) {
				if (f_desc->table[i].vfile == nullptr) {
					pfd = &f_desc->table[i];
					if (i >= f_desc->count) f_desc->count = i + 1;
					pfd->vfile = file; // mark occupied
					break;
				}
			}
		}
		if (!pfd) return -1;

		files->pfiles[fd] = pfd;
		pfd->fd_mode = mode;
		pfd->fd_pos = 0;
		return fd;
	};

	int fd_r = alloc_fd(reader, O_RDONLY);
	int fd_w = alloc_fd(writer, O_WRONLY);

	if (fd_r < 0 || fd_w < 0) {
		if (fd_r >= 0) CloseFileSlotUnlocked(*files, fd_r);
		if (fd_w >= 0) CloseFileSlotUnlocked(*files, fd_w);
		return -1;
	}

	// Safely copy fd values to user space array
	MccaMemCopyP(pipefd, this, &fd_r, nullptr, sizeof(int));
	MccaMemCopyP(pipefd + 1, this, &fd_w, nullptr, sizeof(int));

	return 0;
}

FileDescriptor* FileDescriptor_Clone(FileDescriptor* src) {
	if (!src || !src->vfile) return nullptr;
	FileDescriptor* pfd = nullptr;
	{
		auto f_desc = f_desc_info.Lock();
		for (stduint i = 0; i < 0x1000 / sizeof(FileDescriptor); i++) {
			if (f_desc->table[i].vfile == nullptr) {
				pfd = &f_desc->table[i];
				pfd->vfile = (vfs_file*)1; // reserve
				break;
			}
		}
	}
	if (!pfd) return nullptr;

	*pfd = *src;
	// Clone the vfs_file object to avoid shared destruction
	vfs_file* nvfile = (vfs_file*)malc(sizeof(vfs_file));
	if (!nvfile) {
		pfd->vfile = nullptr; // release reservation
		return nullptr;
	}
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

//// ---- ---- SERVICE ---- ---- ////

#if 1
void serv_file_loop()// for IDE 0:0, 0:1
{
	{
		auto f_desc = f_desc_info.Lock();
		f_desc->table = (FileDescriptor*)new byte[0x1000];
		f_desc->count = 0;
	}
	constexpr stduint pathbuf_size = 512;
	byte* const pathbuf = new byte[pathbuf_size];
	//
	stduint to_args[8];// 8*4=32 bytes
	stduint sig_type = 0, sig_src = 0;
	stduint retval[1];
	bool bootstrapped = false;
	::ThreadBlock* tb;

	while (true) {
		switch ((FilemanMsg)sig_type)
		{
		case FilemanMsg::TEST:// (no-feedback)
		{
			if (bootstrapped) {
				plogerro("Fileman TEST after bootstrapped: recv_ret=%d src=%u type=%u args=%[x] %[x] %[x] %[x]",
					retval[0],
					sig_src,
					sig_type,
					to_args[0],
					to_args[1],
					to_args[2],
					to_args[3]);
				plogerro(">>> %x", Taskman::LocateThread(sig_src)->parent_process->paging.root_level_page);
				plogerro("Fileman: TEST message received after bootstrapped");
				break;
			}
			bootstrapped = true;
			// Init
			#if (_MCCA & 0xFF00) == 0x8600
			syssend(Task_Memdisk_Serv, &retval, sizeof(retval[0]), _IMM(FiledevMsg::RUPT));
			#if _MCCA == 0x8632
			syssend(Task_Hdd_Serv, &retval, sizeof(retval[0]), _IMM(FiledevMsg::RUPT));// while (!fileman_hd_ready);
			// syssend(Task_Flp_Serv, &retval, sizeof(retval[0]), _IMM(FiledevMsg::RUPT));
			#endif
			// Filesys::Tree(Console, true);
			Consman_InitializeFreeType();
			ProcessBlock* init_p = Taskman::CreateFile(("/md0/init"), RING_U, Task_Kernel);
			if (init_p) {
				init_p->main_thread->name = "init";
				{
					auto focus_tty = init_p->focus_tty.Lock();
					*focus_tty = vttys[0];
					if (*focus_tty) {
						init_p->Open("/dev/tty", O_RDWR); // stdin
						init_p->Open("/dev/tty", O_RDWR); // stdout
						init_p->Open("/dev/tty", O_RDWR); // stderr
					}
				}
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
			if (shell_p) shell_p->main_thread->name = "shell";
			ploginfo("Create new shell-form: pid%u", shell_p ? shell_p->pid : 0);
			#else
			ProcessBlock* p;
			p = Taskman::CreateFile(("/md0/cot"), RING_U, Task_Kernel);
			{
				auto focus_tty = p->focus_tty.Lock();
				*focus_tty = vttys[0];
				if (*focus_tty) {
					p->Open("/dev/tty", O_RDWR); // stdin
					p->Open("/dev/tty", O_RDWR); // stdout
					p->Open("/dev/tty", O_RDWR); // stderr
				}
			}
			Taskman::Append(p);
			Taskman::AppendThread(p->main_thread);
			#endif

			#elif (_MCCA & 0xFF00) == 0x1000
			syssend(Task_Memdisk_Serv, &retval, sizeof(retval[0]), _IMM(FiledevMsg::RUPT));
			ProcessBlock* p;
			p = Taskman::CreateFile(("/md0/lpa.elf"), RING_U, Task_Kernel);
			*p->focus_tty.Lock() = vttys[0];
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
			ProcessBlock* safe_pb = ProcessBlock::AcquireActive(to_args[1]);
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
			ProcessBlock* safe_pb = ProcessBlock::AcquireActiveByPID(to_args[1]);
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

			ProcessBlock* safe_pb = ProcessBlock::AcquireActive(to_args[3]);
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
			ProcessBlock* safe_pb = ProcessBlock::AcquireActive(to_args[1]);
			if (safe_pb) {
				{
					auto files = safe_pb->fileman.Lock();
					auto len = StrCopyP((char*)pathbuf, kernel_paging, (rostr)to_args[0], safe_pb->paging, pathbuf_size);
					if (len > 0) {
						retval[0] = Filesys::Remove((rostr)pathbuf, files->cwd);
					}
				}
				ProcessBlock::Release(safe_pb);
			}
			syssend(sig_src, &retval, sizeof(retval[0]));
			break;
		}
		case FilemanMsg::ENUMER:
		{
			ProcessBlock* safe_pb = ProcessBlock::AcquireActive(to_args[3]);

			stduint ret = 0;
			if (safe_pb) {
				{
					auto files = safe_pb->fileman.Lock();
					int fd = to_args[0];
					if (fd >= 0 && fd < (stdsint)files->pfiles.Count() && files->pfiles[fd] && files->pfiles[fd]->vfile) {
						ret = Filesys::Enumer(files->pfiles[fd]->vfile, (void*)to_args[1], to_args[2], safe_pb);
					}
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
			stdsint reply = 0;
			ProcessBlock* safe_pb = ProcessBlock::AcquireActive(to_args[1]);
			if (!safe_pb) {
				syssend(sig_src, &err_ret, sizeof(err_ret));
				break;
			}
			{
				auto files = safe_pb->fileman.Lock();
				auto len = StrCopyP((char*)pathbuf, kernel_paging, (rostr)to_args[0], safe_pb->paging, pathbuf_size);
				if (len == 0) {
					reply = err_ret;
				}
				else {
					vfs_dentry* target_dentry = Filesys::Index((const char*)pathbuf, files->cwd);
					if (!target_dentry) {
						reply = err_ret;
					}
					else if (!target_dentry->d_inode || (target_dentry->d_inode->i_mode & I_TYPE_MASK) != I_DIRECTORY) {
						reply = -2;
					}
					else {
						files->cwd = target_dentry;
					}
				}
			}
			syssend(sig_src, &reply, sizeof(reply));
			ProcessBlock::Release(safe_pb);
			break;
		}

		case FilemanMsg::GETD:
		{
			stdsint err_ret = -1;
			ProcessBlock* safe_pb = ProcessBlock::AcquireActive(to_args[2]);
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
			String abs_path;
			{
				auto files = safe_pb->fileman.Lock();
				abs_path = Filesys::getAbsolutePath(files->cwd);
			}
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
