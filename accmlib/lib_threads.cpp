#include "aaaaa.h"
#include <unistd.h>
#include <pthread.h>
#include <sched.h>

// Structure to track thread metadata in user space
struct pthread_meta {
	stduint tid;
	void* stack_addr;
	void* (*start_routine)(void*);
	void* arg;
	void* retval;
	bool is_detached;
};

#define MAX_THREADS 64
static pthread_meta* active_threads[MAX_THREADS] = {nullptr};
static void* dead_stacks[MAX_THREADS] = {nullptr};

// Lock-free collection of detached thread stacks
static void collect_dead_stacks() {
	for (int i = 0; i < MAX_THREADS; i++) {
		void* stack = __atomic_exchange_n(&dead_stacks[i], nullptr, __ATOMIC_SEQ_CST);
		if (stack) {
			free(stack);
		}
	}
}

// Register thread metadata
static void register_thread(pthread_meta* meta) {
	for (int i = 0; i < MAX_THREADS; i++) {
		if (__atomic_load_n(&active_threads[i], __ATOMIC_RELAXED) == nullptr) {
			pthread_meta* expected = nullptr;
			if (__atomic_compare_exchange_n(&active_threads[i], &expected, meta, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
				break;
			}
		}
	}
}

// Find thread metadata by tid
static pthread_meta* find_thread(stduint tid) {
	for (int i = 0; i < MAX_THREADS; i++) {
		pthread_meta* m = __atomic_load_n(&active_threads[i], __ATOMIC_SEQ_CST);
		if (m && m->tid == tid) {
			return m;
		}
	}
	return nullptr;
}

// Unregister thread metadata
static void unregister_thread(pthread_meta* meta) {
	for (int i = 0; i < MAX_THREADS; i++) {
		if (__atomic_load_n(&active_threads[i], __ATOMIC_RELAXED) == meta) {
			__atomic_store_n(&active_threads[i], (pthread_meta*)nullptr, __ATOMIC_SEQ_CST);
			break;
		}
	}
}

// Thread entry wrapper stub
static void thread_stub(void* meta_ptr) {
	pthread_meta* meta = (pthread_meta*)meta_ptr;
	void* ret = meta->start_routine(meta->arg);
	pthread_exit(ret);
}

extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start_routine)(void*), void* arg) {
	if (!thread || !start_routine) return -1;

	// Collect any pending dead detached thread stacks
	collect_dead_stacks();

	// Allocate a 16KB stack for the thread in user space
	stduint stack_size = 16384;
	byte* stack_addr = (byte*)malloc(stack_size);
	if (!stack_addr) return -1;
	byte* stack_top = stack_addr + stack_size;

	pthread_meta* meta = (pthread_meta*)malloc(sizeof(pthread_meta));
	if (!meta) {
		free(stack_addr);
		return -1;
	}

	meta->stack_addr = stack_addr;
	meta->start_routine = start_routine;
	meta->arg = arg;
	meta->retval = nullptr;
	meta->is_detached = (attr && attr->detachstate == 1);

	// ploginfo("[pthread_create] stack=%[x] top=%[x] meta=%[x] stub=%[x] start=%[x] arg=%[x]", stack_addr, stack_top, meta, thread_stub, start_routine, arg);

	// Spawn the kernel thread
	stdsint tid = syscall(syscall_t::TNEW, _IMM(thread_stub), _IMM(meta), _IMM(stack_top));
	if (tid < 0) {
		plogerro("[pthread_create] TNEW failed entry=%[x] arg=%[x] stack_top=%[x]",
			thread_stub, meta, stack_top);
		free(stack_addr);
		free(meta);
		return -1;
	}

	meta->tid = (stduint)tid;
	// ploginfo("[pthread_create] created tid=%u meta=%[x] stack=%[x]", meta->tid, meta, stack_addr);
	register_thread(meta);
	*thread = (pthread_t)meta;

	return 0;
}

extern "C" void pthread_exit(void* retval) {
	pthread_meta* meta = (pthread_meta*)pthread_self();
	if (meta) {
		meta->retval = retval;
		if (meta->is_detached) {
			void* stack = meta->stack_addr;
			// Schedule stack for deferred cleanup to avoid crashing
			for (int j = 0; j < MAX_THREADS; j++) {
				void* expected = nullptr;
				if (__atomic_compare_exchange_n(&dead_stacks[j], &expected, stack, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
					break;
				}
			}
			unregister_thread(meta);
			free(meta);
		}
	}
	syscall(syscall_t::TEXI, _IMM(retval));
	while (1);
}

extern "C" int pthread_join(pthread_t thread, void** retval) {
	pthread_meta* meta = (pthread_meta*)thread;
	if (!meta) return -1;

	stdsint res = syscall(syscall_t::TJOI, meta->tid, 0);
	if (res < 0) return res;

	if (retval) {
		*retval = meta->retval;
	}

	// Safely free stack and metadata
	free(meta->stack_addr);
	unregister_thread(meta);
	free(meta);

	return 0;
}

extern "C" pthread_t pthread_self(void) {
	stduint tid = syscall(syscall_t::TGET);
	return (pthread_t)find_thread(tid);
}

extern "C" int pthread_detach(pthread_t thread) {
	pthread_meta* meta = (pthread_meta*)thread;
	if (!meta) return -1;

	stdsint res = syscall(syscall_t::TDET, meta->tid);
	if (res < 0) return res;

	meta->is_detached = true;
	return 0;
}

extern "C" int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
	if (!mutex) return -1;
	mutex->lock_value = 0;
	return 0;
}

extern "C" int pthread_mutex_lock(pthread_mutex_t* mutex) {
	if (!mutex) return -1;
	// Atomic swap with acquire barrier
	while (__atomic_exchange_n(&mutex->lock_value, 1, __ATOMIC_ACQUIRE) != 0) {
		// Wait on lock_value address in kernel
		syscall(syscall_t::FUTX, _IMM(&mutex->lock_value), 0, 1);
	}
	return 0;
}

extern "C" int pthread_mutex_unlock(pthread_mutex_t* mutex) {
	if (!mutex) return -1;
	// Store 0 with release barrier
	__atomic_store_n(&mutex->lock_value, 0, __ATOMIC_RELEASE);
	// Wake up 1 waiting thread
	syscall(syscall_t::FUTX, _IMM(&mutex->lock_value), 1, 1);
	return 0;
}

extern "C" int pthread_mutex_destroy(pthread_mutex_t* mutex) {
	if (!mutex) return -1;
	mutex->lock_value = 0;
	return 0;
}

extern "C" int sched_yield(void) {
	return syscall(syscall_t::TYLD);
}
