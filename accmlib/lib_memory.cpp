#include "inc/aaaaa.h"
#include "c/ustring.h"
#include "c/consio.h"
#include "c/mempool.h"
#include "c/system/paging.h"

class UserHeapMmapAllocator : public uni::trait::Malloc {
public:
	virtual void* allocate(stduint size, stduint alignment = 0, stduint boundary = 0) override {
		stduint page_size = (size + 0xFFF) & ~_IMM(0xFFF);
		return (void*)syscall(syscall_t::MMAP, page_size, PGPROP_present | PGPROP_writable | PGPROP_user_access, 0);
	}
	virtual bool deallocate(void* ptr, stduint size = 0) override {
		if (!ptr) return true;
		stduint page_size = (size + 0xFFF) & ~_IMM(0xFFF);
		return syscall(syscall_t::UMAP, (stduint)ptr, page_size) == 0;
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

