//#include <c/stdinc.h>
#include "aaaaa.h"
#include "c/consio.h"
#include "unistd.h"
#include <pthread.h>
#include <sched.h>

using namespace uni;

// POSIX thread smoke test entry.
//
// This file is intentionally kept separate from `subapps/test.cpp` so thread
// bring-up and synchronization regressions can be tested in isolation.
//
// Select one mode by changing THREAD_SMOKE_MODE:
// - THREAD_SMOKE_JOIN:   create + join + exit status propagation
// - THREAD_SMOKE_DETACH: create + detach + auto-reap path
// - THREAD_SMOKE_MUTEX:  create + join + mutex/futex contention
#define THREAD_SMOKE_JOIN   1
#define THREAD_SMOKE_DETACH 2
#define THREAD_SMOKE_MUTEX  3
#define THREAD_SMOKE_MODE   THREAD_SMOKE_MUTEX

static pthread_mutex_t my_mutex;
static volatile int shared_counter = 0;
static volatile int worker_started = 0;
static volatile int worker_finished = 0;
static volatile stduint worker_seen_arg = 0;
static volatile stduint worker_retval = 0;

static void* worker_thread_func(void* arg) {
	worker_started = 1;
	worker_seen_arg = (stduint)arg;
	#if THREAD_SMOKE_MODE == THREAD_SMOKE_MUTEX
	for (int i = 0; i < 100; i++) {
		pthread_mutex_lock(&my_mutex);
		shared_counter = shared_counter + 1;
		pthread_mutex_unlock(&my_mutex);
		sched_yield();
	}
	#else
	shared_counter = shared_counter + 1;
	#endif
	worker_finished = 1;
	return (void*)88;
}

static int run_posix_thread_smoke() {
	#if THREAD_SMOKE_MODE == THREAD_SMOKE_MUTEX
	pthread_mutex_init(&my_mutex, nullptr);
	#endif
	pthread_t th;
	int create_res = pthread_create(&th, nullptr, worker_thread_func, (void*)12345);
	ploginfo("[Main Thread] pthread_create returned %d", create_res);
	if (create_res != 0) {
		return create_res;
	}

	#if THREAD_SMOKE_MODE == THREAD_SMOKE_JOIN
	void* retval = nullptr;
	int join_res = pthread_join(th, &retval);
	worker_retval = (stduint)retval;
	ploginfo("[Main Thread] pthread_join returned %d retval=%u", join_res, worker_retval);
	if (join_res != 0) {
		return join_res;
	}
	ploginfo("[Main Thread] worker_started=%d worker_finished=%d shared_counter=%d arg=%u retval=%u",
		worker_started, worker_finished, shared_counter, worker_seen_arg, worker_retval);
	return 0;
	#elif THREAD_SMOKE_MODE == THREAD_SMOKE_DETACH
	int det_res = pthread_detach(th);
	ploginfo("[Main Thread] pthread_detach returned %d", det_res);
	if (det_res != 0) {
		return det_res;
	}
	for (int i = 0; i < 20 && !worker_finished; i++) {
		sysrest(1, 50);
		sched_yield();
	}
	ploginfo("[Main Thread] worker_started=%d worker_finished=%d shared_counter=%d arg=%u",
		worker_started, worker_finished, shared_counter, worker_seen_arg);
	return 0;
	#elif THREAD_SMOKE_MODE == THREAD_SMOKE_MUTEX
	for (int i = 0; i < 100; i++) {
		pthread_mutex_lock(&my_mutex);
		shared_counter = shared_counter + 1;
		pthread_mutex_unlock(&my_mutex);
		sched_yield();
	}

	void* retval = nullptr;
	int join_res = pthread_join(th, &retval);
	worker_retval = (stduint)retval;
	ploginfo("[Main Thread] pthread_join returned %d retval=%u", join_res, worker_retval);
	if (join_res != 0) {
		return join_res;
	}
	ploginfo("[Main Thread] worker_started=%d worker_finished=%d shared_counter=%d arg=%u retval=%u",
		worker_started, worker_finished, shared_counter, worker_seen_arg, worker_retval);
	return 0;
	#else
	return -1;
	#endif
}

int main(int argc, char** argv)
{
	#if __BITS__ == 64
	_preprocess();
	#endif
	return run_posix_thread_smoke();
}
