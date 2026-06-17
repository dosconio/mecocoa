
struct Spinlock {
	byte locked = 0;
	stdsint cpu_id = 0;
	Spinlock() {}
	bool Acquire();
	void Release(bool old_if);
};
struct SpinlockLocal {
	bool old_if;
	Spinlock* spinlock;
	SpinlockLocal(Spinlock* spinl) : spinlock(spinl) {
		old_if = spinlock->Acquire();
	}
	/// @brief Drop the lock
	~SpinlockLocal() {
		auto sl = spinlock;
		spinlock = 0;
		if (sl) {
			sl->Release(old_if);
		}
	}
};

struct Mutex {
	Spinlock guard;
	byte locked = 0;
	uni::Queue<ThreadBlock*> wait_queue;
	Mutex() : wait_queue() {}
	void Acquire();
	void Release();
};
struct MutexLocal {
	Mutex* mutex;
	MutexLocal(Mutex* _mutex) : mutex(_mutex) {
		mutex->Acquire();
	}
	~MutexLocal() {
		mutex->Release();
	}
};

// MutexBlock<T> protects one resource domain with a per-domain Mutex.
// It does not manage object lifetime. If the owner object can disappear or
// transition out of active use, callers should first acquire a stable owner
// reference (for example via AcquireActive*) and then Lock() this block.
template <typename T>
class MutexBlock {
	Mutex lock_;
	T data_;
public:
	MutexBlock() : lock_(), data_() {}

	template <typename... Args>
	MutexBlock(Args&&... args) : lock_(), data_(static_cast<Args&&>(args)...) {}

	class Guard {
		MutexBlock<T>* owner_ = nullptr;
	public:
		explicit Guard(MutexBlock<T>* owner) : owner_(owner) {
			owner_->lock_.Acquire();
		}
		Guard(const Guard&) = delete;
		Guard& operator=(const Guard&) = delete;
		Guard(Guard&& other) : owner_(other.owner_) {
			other.owner_ = nullptr;
		}
		Guard& operator=(Guard&& other) = delete;
		~Guard() {
			Unlock();
		}
		void Unlock() {
			auto owner = owner_;
			owner_ = nullptr;
			if (owner) owner->lock_.Release();
		}
		T* operator->() { return &owner_->data_; }
		T& operator*() { return owner_->data_; }
		const T* operator->() const { return &owner_->data_; }
		const T& operator*() const { return owner_->data_; }
	};

	Guard Lock() {
		return Guard(this);
	}
	Mutex& raw_mutex() { return lock_; }
	const Mutex& raw_mutex() const { return lock_; }
};

struct Semaphore {
	Spinlock guard;
	stdsint value;
	uni::Queue<ThreadBlock*> wait_queue;

	/// @brief Initialize semaphore with an initial counter value
	/// @param initial_value The initial available resource count (default is 1)
	Semaphore(stdsint initial_value = 1) : value(initial_value), wait_queue() {}

	/// @brief Acquire operation - Decrements resource count or blocks the current thread
	void Acquire();

	/// @brief Release operation - Increments resource count or wakes up a waiting thread
	void Release();
};

/// @brief RAII local wrapper for Semaphore, automatically invoking Acquire/Release on scope lifetime
struct SemaphoreLocal {
	Semaphore* semaphore;
	SemaphoreLocal(Semaphore* _sem) : semaphore(_sem) {
		semaphore->Acquire();
	}
	~SemaphoreLocal() {
		semaphore->Release();
	}
};
