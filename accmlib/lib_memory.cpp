#include "aaaaa.h"
#include "c/ustring.h"
#include "c/consio.h"
#include "c/mempool.h"
#include "c/system/paging.h"
#include <sys/mman.h>

class UserHeapMmapAllocator : public uni::trait::Malloc {
public:
	virtual void* allocate(stduint size, stduint alignment = 0, stduint boundary = 0) override {
		stduint page_size = (size + 0xFFF) & ~_IMM(0xFFF);
		return (void*)syscall(syscall_t::MMAP, page_size, PGPROP_present | PGPROP_writable | PGPROP_user_access, ~_IMM0);
	}
	virtual bool deallocate(void* ptr, stduint size = 0) override {
		if (!ptr) return true;
		stduint page_size = (size + 0xFFF) & ~_IMM(0xFFF);
		return syscall(syscall_t::UMAP, (stduint)ptr, page_size) != ~_IMM0;
	}
};

static UserHeapMmapAllocator mmap_alloc;
static uni::Mempool user_mempool;
static bool user_heap_initialized = false;

void user_heap_init() {
	uni_default_allocator = &mmap_alloc;
	user_mempool.Reset();
	user_mempool.enable_auto_expand = true;
	user_mempool.auto_expand_step = 0x40000; // Expand by 256KB
	user_heap_initialized = true;
}

_ESYM_C void* malloc(size_t size) {
	if (!user_heap_initialized) {
		user_heap_init();
	}
	return user_mempool.allocate(size);
}

_ESYM_C void free(void* ptr) {
	if (!ptr) return;
	if (!user_heap_initialized) {
		user_heap_init();
	}
	user_mempool.deallocate(ptr);
}

_PACKED(struct) MempoolHeader {
	stduint size;
	stduint prop;
};

_ESYM_C void* calloc(size_t nmemb, size_t size) {
	size_t total = nmemb * size;
	void* ptr = malloc(total);
	if (ptr) {
		MemSet(ptr, 0, total);
	}
	return ptr;
}

_ESYM_C void* realloc(void* ptr, size_t size) {
	if (!ptr) return malloc(size);
	if (!size) {
		free(ptr);
		return nullptr;
	}
	MempoolHeader* header = (MempoolHeader*)ptr - 1;
	if (header->prop != _IMM(0xFEDC5AA5)) {
		return nullptr; // Safety fallback
	}
	size_t old_size = header->size;
	if (old_size >= size) {
		return ptr; // Already large enough
	}
	void* new_ptr = malloc(size);
	if (new_ptr) {
		MemCopyN(new_ptr, ptr, old_size);
		free(ptr);
	}
	return new_ptr;
}

_ESYM_C void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
	// File mappings currently support a kernel-chosen address and MAP_SHARED.
	if (addr || !length || offset < 0 || (offset & 0xFFF) || flags != MAP_SHARED || fd < 0 ||
		(prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) ||
		!(prot & PROT_READ)) return MAP_FAILED;
	#if (_ACCM & 0xFF00) == 0x8600 || (_ACCM & 0xFF00) == 0x1000
	syscall_mmap_request_t req;
	req.address = (stduint)addr;
	req.prot = (stduint)prot;
	req.flags = (stduint)flags;
	req.fd = fd;
	req.offset = offset;
	return (void*)syscall(syscall_t::MMAP, length, ~_IMM0, (stduint)&req);
	#else
	return MAP_FAILED;
	#endif
}

_ESYM_C int munmap(void* addr, size_t length) {
	stduint ret = syscall(syscall_t::UMAP, (stduint)addr, length);
	return ret == length ? 0 : -1;
}

// ----

void operator delete(void* p, size_t size) noexcept {
	if (!p) return;
	if (!user_heap_initialized) {
		user_heap_init();
	}
	user_mempool.deallocate(p, size);
}

void operator delete[](void* p, size_t size) noexcept {
	if (!p) return;
	if (!user_heap_initialized) {
		user_heap_init();
	}
	user_mempool.deallocate(p, size);
}
