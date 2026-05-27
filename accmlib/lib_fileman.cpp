#include "inc/aaaaa.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>


int open(const char* path, int oflag, ...) {
	// Note: mode is ignored for now as Mecocoa filesystem doesn't support complex permissions yet
	return syscall(syscall_t::OPEN, _IMM(path), oflag, nil);
}

int close(int fd) {
	return syscall(syscall_t::CLOS, fd, nil, nil);
}

stdsint read(int fd, void* buf, stduint nbyte) {
	return syscall(syscall_t::READ, fd, _IMM(buf), nbyte);
}

stdsint write(int fd, const void* buf, size_t nbyte) {
	return syscall(syscall_t::WRIT, fd, _IMM(buf), nbyte);
}

int chdir(const char* path) {
	// Invoke the SETD system call
	return syscall(syscall_t::SETD, _IMM(path), nil, nil);
}

char* getcwd(char* buf, size_t size) {
	// Invoke the GETD system call
	int ret = syscall(syscall_t::GETD, _IMM(buf), size, nil);
	if (ret < 0) {
		return nullptr;
	}
	return buf;
}

#include <sys/stat.h>
#include <dirent.h>

int fstat(int fd, struct stat *buf) {
	if (!buf) return -1;
	file_proper_t prop;
	int ret = syscall(syscall_t::PORP, fd, _IMM(&prop), nil);
	if (ret < 0) return ret;
	buf->st_size = prop.size;
	buf->st_mode = prop.mode;
	return 0;
}

int stat(const char *path, struct stat *buf) {
	int fd = open(path, O_RDONLY);
	if (fd < 0) return fd;
	int ret = fstat(fd, buf);
	close(fd);
	return ret;
}

#define MAX_DIR_STREAMS 8
static DIR g_dir_pool[MAX_DIR_STREAMS];
static bool g_dir_used[MAX_DIR_STREAMS];

DIR *opendir(const char *name) {
	int fd = open(name, O_RDONLY);
	if (fd < 0) return nullptr;

	struct stat st;
	if (fstat(fd, &st) < 0 || !S_ISDIR(st.st_mode)) {
		close(fd);
		return nullptr;
	}

	for (int i = 0; i < MAX_DIR_STREAMS; i++) {
		if (!g_dir_used[i]) {
			g_dir_used[i] = true;
			g_dir_pool[i].fd = fd;
			g_dir_pool[i].current_entry.d_ino = 0;
			g_dir_pool[i].current_entry.d_off = 0;
			g_dir_pool[i].current_entry.d_reclen = sizeof(struct dirent);
			return &g_dir_pool[i];
		}
	}
	close(fd);
	return nullptr;
}

DIR *fdopendir(int fd) {
	struct stat st;
	if (fstat(fd, &st) < 0 || !S_ISDIR(st.st_mode)) {
		return nullptr;
	}

	for (int i = 0; i < MAX_DIR_STREAMS; i++) {
		if (!g_dir_used[i]) {
			g_dir_used[i] = true;
			g_dir_pool[i].fd = fd;
			g_dir_pool[i].current_entry.d_ino = 0;
			g_dir_pool[i].current_entry.d_off = 0;
			g_dir_pool[i].current_entry.d_reclen = sizeof(struct dirent);
			return &g_dir_pool[i];
		}
	}
	return nullptr;
}

struct dirent *readdir(DIR *dirp) {
	if (!dirp || dirp->fd < 0) return nullptr;

	dirent_t kde;
	int ret = syscall(syscall_t::ENUM, dirp->fd, _IMM(&kde), 1);
	if (ret <= 0) {
		return nullptr;
	}

	dirp->current_entry.d_ino = 1;
	dirp->current_entry.d_off = 0;
	dirp->current_entry.d_reclen = sizeof(struct dirent);
	dirp->current_entry.d_type = kde.is_dir ? DT_DIR : DT_REG;

	stduint name_len = 0;
	while (name_len < 255 && kde.name[name_len]) {
		dirp->current_entry.d_name[name_len] = kde.name[name_len];
		name_len++;
	}
	dirp->current_entry.d_name[name_len] = '\0';

	return &dirp->current_entry;
}

int closedir(DIR *dirp) {
	if (!dirp) return -1;
	for (int i = 0; i < MAX_DIR_STREAMS; i++) {
		if (&g_dir_pool[i] == dirp) {
			if (g_dir_used[i]) {
				close(dirp->fd);
				g_dir_used[i] = false;
				return 0;
			}
		}
	}
	return -1;
}

int mkdir(const char *path, mode_t mode) {
	// Create directory using open with O_CREAT and O_DIRECTORY flags
	int fd = open(path, O_CREAT | O_DIRECTORY | O_RDONLY);
	if (fd < 0) return -1;
	close(fd);
	return 0;
}

int unlink(const char* pathname) {
	// Call DELF system call: returns 1 for success, 0 for failure
	int ret = syscall(syscall_t::DELF, _IMM(pathname), nil, nil);
	return ret ? 0 : -1;
}
