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
