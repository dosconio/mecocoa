#include "inc/aaaaa.h"
#include <unistd.h>

int close(int fd) {
	return syscall(syscall_t::CLOS, fd, nil, nil);
}

stdsint read(int fd, void* buf, stduint nbyte) {
	return syscall(syscall_t::READ, fd, _IMM(buf), nbyte);
}

stdsint write(int fd, const void* buf, size_t nbyte) {
	return syscall(syscall_t::WRIT, fd, _IMM(buf), nbyte);
}
