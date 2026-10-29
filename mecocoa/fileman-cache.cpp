// ASCII g++ TAB4 LF
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#if CONFIG_ENABLE_MMU
namespace uni {
	extern Mutex vfs_lock;
	Mutex shared_file_copy_lock;
	extern Atomic<stduint> shared_file_copy_epoch;
	extern Atomic<stduint> shared_file_active_user_copies[2];
}

namespace {
	// A process pin retains an executable inode and freezes its content until teardown.
	struct ExecutableImagePin {
		ProcessBlock* process;
		uni::SharedPtr<uni::vfs_inode> inode;
		bool mapped_file = false;
		ExecutableImagePin* next = nullptr;
	};
	// vfs_lock protects this list; the inode reference survives unlink and unmount.
	ExecutableImagePin* executable_image_pins = nullptr;

	struct FileCacheKey {
		uint64 identity;
		uint64 content_version;
		uint64 file_offset;

		bool Valid() const {
			return identity && content_version && !(file_offset & (PAGE_SIZE - 1));
		}

		bool operator==(const FileCacheKey& other) const {
			return identity == other.identity &&
				content_version == other.content_version &&
				file_offset == other.file_offset;
		}
	};

	enum class FileCacheEntryState {
		Loading,
		Ready,
		Error,
	};

	struct FileCacheEntry {
		FileCacheKey key;
		// The filler holds completion until state and physical ownership are final.
		Mutex completion;
		FileCacheEntryState state = FileCacheEntryState::Loading;
		stduint physical_address = 0;
		bool owns_cache_reference = false;
		stduint fills = 0;
		stduint ready_hits = 0;
		stduint waiters = 0;

		explicit FileCacheEntry(const FileCacheKey& key) : key(key) {}
		FileCacheEntry(const FileCacheEntry&) = delete;
		FileCacheEntry& operator=(const FileCacheEntry&) = delete;

		~FileCacheEntry() {
			if (!owns_cache_reference) return;
			KASSERT(physical_address);
			const bool released = Memory::ReleaseManagedPageCache(physical_address);
			KASSERT(released);
		}
	};

	using FileCacheEntryOwner = uni::SharedPtr<FileCacheEntry>;

	struct FileCacheRecord {
		FileCacheEntryOwner entry;
		FileCacheRecord* next = nullptr;
	};

	class FileCacheTable {
		static constexpr stduint BucketCount = 256;
		// Bound retained clean pages until a pressure-aware reclaim policy is added.
		static constexpr stduint RetainedPageLimit = 1024;
		Mutex lock;
		FileCacheRecord* buckets[BucketCount] = {};
		stduint record_count = 0;

		static stduint Bucket(const FileCacheKey& key) {
			const uint64 page = key.file_offset / PAGE_SIZE;
			const uint64 mixed = key.identity ^ (key.identity >> 32) ^
				key.content_version ^ (key.content_version >> 32) ^
				page ^ (page >> 32);
			return stduint(mixed) & (BucketCount - 1);
		}

		// Caller holds lock.
		FileCacheRecord** FindLink(const FileCacheKey& key) {
			FileCacheRecord** link = &buckets[Bucket(key)];
			while (*link && !((*link)->entry->key == key)) link = &(*link)->next;
			return link;
		}

		static void DeleteRecords(FileCacheRecord* records) {
			while (records) {
				FileCacheRecord* next = records->next;
				delete records;
				records = next;
			}
		}

		// Caller holds lock. A mapped page remains discoverable for later readers.
		FileCacheRecord* DetachInactiveReadyPages(stduint limit) {
			FileCacheRecord* removed = nullptr;
			for (stduint bucket = 0; bucket < BucketCount && limit; bucket++) {
				FileCacheRecord** link = &buckets[bucket];
				while (*link && limit) {
					FileCacheEntryOwner& entry = (*link)->entry;
					ManagedPageInfo page = {};
					if (entry->state != FileCacheEntryState::Ready ||
						!entry.unique() ||
						!Memory::QueryManagedPage(entry->physical_address, page) ||
						page.mapping_refs || page.cache_refs != 1) {
						link = &(*link)->next;
						continue;
					}
					FileCacheRecord* record = *link;
					*link = record->next;
					record->next = removed;
					removed = record;
					record_count--;
					limit--;
				}
			}
			return removed;
		}

		// Caller holds lock. Removed records are destroyed after releasing it.
		FileCacheRecord* TrimReadyPages() {
			return record_count > RetainedPageLimit ?
				DetachInactiveReadyPages(record_count - RetainedPageLimit) : nullptr;
		}

	public:
		FileCacheEntryOwner FindOrInsert(const FileCacheKey& key, bool& inserted) {
			inserted = false;
			if (!key.Valid()) return {};

			{
				MutexLocal guard(&lock);
				FileCacheRecord* record = *FindLink(key);
				if (record) {
					if (record->entry->state == FileCacheEntryState::Loading)
						record->entry->waiters++;
					else record->entry->ready_hits++;
					return record->entry;
				}
			}

			auto prepared_entry = uni::make_shared<FileCacheEntry>(key);
			if (!prepared_entry) return {};
			auto* prepared_record = new FileCacheRecord { prepared_entry, nullptr };
			if (!prepared_record) return {};
			prepared_entry->completion.Acquire();

			FileCacheEntryOwner result;
			FileCacheRecord* removed = nullptr;
			{
				MutexLocal guard(&lock);
				FileCacheRecord** link = FindLink(key);
				if (*link) {
					result = (*link)->entry;
					if (result->state == FileCacheEntryState::Loading)
						result->waiters++;
					else result->ready_hits++;
				}
				else {
					*link = prepared_record;
					record_count++;
					result = prepared_entry;
					inserted = true;
					removed = TrimReadyPages();
				}
			}
			DeleteRecords(removed);
			if (!inserted) {
				prepared_entry->completion.Release();
				delete prepared_record;
			}
			return result;
		}

		void Complete(const FileCacheEntryOwner& entry, stduint physical_address) {
			FileCacheRecord* removed = nullptr;
			FileCacheRecord* trimmed = nullptr;
			{
				MutexLocal guard(&lock);
				if (physical_address) {
					entry->physical_address = physical_address;
					entry->owns_cache_reference = true;
					entry->fills++;
					entry->state = FileCacheEntryState::Ready;
					trimmed = TrimReadyPages();
				} else {
					entry->state = FileCacheEntryState::Error;
					FileCacheRecord** link = FindLink(entry->key);
					if (*link && (*link)->entry.get() == entry.get()) {
						removed = *link;
						*link = removed->next;
						record_count--;
					}
				}
			}
			delete removed;
			DeleteRecords(trimmed);
			entry->completion.Release();
		}

		// The caller holds vfs_lock while advancing the inode version.
		void InvalidateVersions(uint64 identity, uint64 current_version) {
			if (!identity) return;
			FileCacheRecord* removed = nullptr;
			{
				MutexLocal guard(&lock);
				for (stduint bucket = 0; bucket < BucketCount; bucket++) {
					FileCacheRecord** link = &buckets[bucket];
					while (*link) {
						const FileCacheKey& key = (*link)->entry->key;
						if (key.identity != identity ||
							key.content_version == current_version) {
							link = &(*link)->next;
							continue;
						}
						FileCacheRecord* record = *link;
						*link = record->next;
						record->next = removed;
						removed = record;
						record_count--;
					}
				}
			}
			DeleteRecords(removed);
		}

		void ForgetInode(uint64 identity) {
			if (!identity) return;
			FileCacheRecord* removed = nullptr;
			stduint ready_pages = 0;
			stduint fills = 0;
			stduint hits = 0;
			stduint waits = 0;
			{
				MutexLocal guard(&lock);
				for (stduint bucket = 0; bucket < BucketCount; bucket++) {
					FileCacheRecord** link = &buckets[bucket];
					while (*link) {
						FileCacheEntry* entry = (*link)->entry.get();
						if (entry->key.identity != identity) {
							link = &(*link)->next;
							continue;
						}
						if (entry->state == FileCacheEntryState::Ready) ready_pages++;
						fills += entry->fills;
						hits += entry->ready_hits;
						waits += entry->waiters;
						FileCacheRecord* record = *link;
						*link = record->next;
						record->next = removed;
						removed = record;
						record_count--;
					}
				}
			}
			DeleteRecords(removed);
			if (ready_pages || hits || waits)
				ploginfo("[filecache] inode=%[64H] ready=%u fills=%u hits=%u waits=%u",
					identity, ready_pages, fills, hits, waits);
		}

		stduint ReclaimInactive(stduint limit) {
			if (!limit) return 0;
			FileCacheRecord* removed = nullptr;
			{
				MutexLocal guard(&lock);
				removed = DetachInactiveReadyPages(limit);
			}
			stduint reclaimed = 0;
			for (FileCacheRecord* record = removed; record; record = record->next)
				reclaimed++;
			DeleteRecords(removed);
			return reclaimed;
		}

		bool Remove(const FileCacheKey& key, const FileCacheEntry* expected) {
			if (!key.Valid() || !expected) return false;
			FileCacheRecord* removed = nullptr;
			{
				MutexLocal guard(&lock);
				FileCacheRecord** link = FindLink(key);
				if (*link && (*link)->entry.get() == expected) {
					removed = *link;
					*link = removed->next;
					record_count--;
				}
			}
			const bool found = removed != nullptr;
			delete removed;
			return found;
		}
	};

	FileCacheTable file_cache;

	enum class FileCachePageResult {
		Ready,
		Retry,
		Error,
	};

	FileCachePageResult GetCachedPage(uni::vfs_inode* inode,
		const FileCacheKey& key, FileCacheEntryOwner& result) {
		bool filler = false;
		result = file_cache.FindOrInsert(key, filler);
		if (!result) return FileCachePageResult::Error;
		if (!filler) {
			// Never wait for a filler while holding vfs_lock or the table lock.
			result->completion.Acquire();
			const bool ready = result->state == FileCacheEntryState::Ready;
			result->completion.Release();
			if (ready) return FileCachePageResult::Ready;
			MutexLocal guard(&uni::vfs_lock);
			return inode->content_version != key.content_version ?
				FileCachePageResult::Retry : FileCachePageResult::Error;
		}

		void* physical = mempool.allocate(PAGE_SIZE, 12);
		if ((!physical || physical == (void*)~_IMM0) &&
			file_cache.ReclaimInactive(1))
			physical = mempool.allocate(PAGE_SIZE, 12);
		if (!physical || physical == (void*)~_IMM0) {
			file_cache.Complete(result, 0);
			return FileCachePageResult::Error;
		}
		// Mempool returns the directly usable physical address here; mglb would change it.
		MemSet(physical, 0, PAGE_SIZE);
		bool stale = false;
		bool filled = false;
		{
			MutexLocal guard(&uni::vfs_lock);
			if (inode->identity != key.identity ||
				inode->content_version != key.content_version ||
				key.file_offset >= inode->i_size) stale = true;
			else {
				uni::FilesysTrait* fs = inode->mount_state ?
					inode->mount_state->filesystem :
					(inode->i_sb ? inode->i_sb->fs : nullptr);
				const stduint length = minof(_IMM(PAGE_SIZE),
					inode->i_size - stduint(key.file_offset));
				filled = fs && fs->readfl(inode->internal_handler,
					Slice { stduint(key.file_offset), length },
					(byte*)physical) == length;
			}
		}
		if (!filled) {
			mempool.deallocate(physical, PAGE_SIZE);
			file_cache.Complete(result, 0);
			return stale ? FileCachePageResult::Retry : FileCachePageResult::Error;
		}
		if (!Memory::AdoptManagedPage(_IMM(physical), ManagedPageSource::File)) {
			file_cache.Complete(result, 0);
			return FileCachePageResult::Error;
		}
		if (!Memory::RetainManagedPageCache(_IMM(physical))) {
			Memory::ReleaseManagedPageMapping(_IMM(physical));
			file_cache.Complete(result, 0);
			return FileCachePageResult::Error;
		}
		if (!Memory::ReleaseManagedPageMapping(_IMM(physical))) {
			Memory::ReleaseManagedPageCache(_IMM(physical));
			file_cache.Complete(result, 0);
			return FileCachePageResult::Error;
		}
		file_cache.Complete(result, _IMM(physical));
		return FileCachePageResult::Ready;
	}

	struct SharedFilePageKey {
		uint64 identity;
		uint64 file_offset;

		bool Valid() const {
			return identity && !(file_offset & (PAGE_SIZE - 1));
		}
		bool operator==(const SharedFilePageKey& other) const {
			return identity == other.identity && file_offset == other.file_offset;
		}
	};

	struct SharedFileAlias {
		ProcessBlock* process;
		stduint address;
		SharedFileAlias* next = nullptr;
	};

	struct SharedFilePage {
		SharedFilePageKey key;
		Mutex completion;
		Mutex lock;
		FileCacheEntryState state = FileCacheEntryState::Loading;
		stduint physical_address = 0;
		bool dirty = false;
		uni::SharedPtr<uni::vfs_inode> dirty_inode;
		SharedFileAlias* aliases = nullptr;

		explicit SharedFilePage(const SharedFilePageKey& key) : key(key) {}
		~SharedFilePage() {
			KASSERT(!dirty && !aliases);
			if (physical_address) {
				const bool released = Memory::ReleaseManagedPageCache(physical_address);
				KASSERT(released);
			}
		}
	};

	using SharedFilePageOwner = uni::SharedPtr<SharedFilePage>;
	struct SharedFilePageRecord {
		SharedFilePageOwner page;
		SharedFilePageRecord* next = nullptr;
	};

	class SharedFilePageTable {
		static constexpr stduint BucketCount = 256;
		static constexpr stduint RetainedPageLimit = 1024;
		Mutex lock;
		SharedFilePageRecord* buckets[BucketCount] = {};
		stduint record_count = 0;

		static stduint Bucket(const SharedFilePageKey& key) {
			const uint64 page = key.file_offset / PAGE_SIZE;
			return stduint(key.identity ^ (key.identity >> 32) ^
				page ^ (page >> 32)) & (BucketCount - 1);
		}
		// Caller holds lock.
		SharedFilePageRecord** FindLink(const SharedFilePageKey& key) {
			SharedFilePageRecord** link = &buckets[Bucket(key)];
			while (*link && !((*link)->page->key == key)) link = &(*link)->next;
			return link;
		}

		// Caller holds lock; active mappings and dirty pages cannot be evicted.
		SharedFilePageRecord* DetachInactive(stduint limit) {
			SharedFilePageRecord* removed = nullptr;
			for (stduint bucket = 0; bucket < BucketCount && limit; bucket++) {
				SharedFilePageRecord** link = &buckets[bucket];
				while (*link && limit) {
					SharedFilePageOwner& page = (*link)->page;
					ManagedPageInfo info = {};
					bool inactive = false;
					{
						MutexLocal page_guard(&page->lock);
						inactive = page->state == FileCacheEntryState::Ready &&
							page.unique() && !page->dirty && !page->aliases &&
							Memory::QueryManagedPage(page->physical_address, info) &&
							!info.mapping_refs && info.cache_refs == 1;
					}
					if (!inactive) {
						link = &(*link)->next;
						continue;
					}
					SharedFilePageRecord* record = *link;
					*link = record->next;
					record->next = removed;
					removed = record;
					record_count--;
					limit--;
				}
			}
			return removed;
		}

		static void DeleteRecords(SharedFilePageRecord* records) {
			while (records) {
				SharedFilePageRecord* next = records->next;
				delete records;
				records = next;
			}
		}
	public:
		SharedFilePageOwner Find(const SharedFilePageKey& key) {
			if (!key.Valid()) return {};
			MutexLocal guard(&lock);
			SharedFilePageRecord* record = *FindLink(key);
			return record ? record->page : SharedFilePageOwner();
		}

		SharedFilePageOwner FindReady(const SharedFilePageKey& key) {
			if (!key.Valid()) return {};
			MutexLocal guard(&lock);
			SharedFilePageRecord* record = *FindLink(key);
			return record && record->page->state == FileCacheEntryState::Ready ?
				record->page : SharedFilePageOwner();
		}

		SharedFilePageOwner FindOrInsert(const SharedFilePageKey& key, bool& inserted) {
			inserted = false;
			if (!key.Valid()) return {};
			if (auto existing = Find(key)) return existing;
			auto prepared = uni::make_shared<SharedFilePage>(key);
			if (!prepared) return {};
			auto* record = new SharedFilePageRecord { prepared, nullptr };
			if (!record) return {};
			prepared->completion.Acquire();
			SharedFilePageOwner result;
			SharedFilePageRecord* trimmed = nullptr;
			{
				MutexLocal guard(&lock);
				SharedFilePageRecord** link = FindLink(key);
				if (*link) result = (*link)->page;
				else {
					*link = record;
					record_count++;
					result = prepared;
					inserted = true;
					if (record_count > RetainedPageLimit)
						trimmed = DetachInactive(record_count - RetainedPageLimit);
				}
			}
			DeleteRecords(trimmed);
			if (!inserted) {
				prepared->completion.Release();
				delete record;
			}
			return result;
		}

		void Complete(const SharedFilePageOwner& page, stduint physical) {
			SharedFilePageRecord* removed = nullptr;
			{
				MutexLocal guard(&lock);
				page->physical_address = physical;
				page->state = physical ? FileCacheEntryState::Ready :
					FileCacheEntryState::Error;
				if (!physical) {
					SharedFilePageRecord** link = FindLink(page->key);
					if (*link && (*link)->page.get() == page.get()) {
						removed = *link;
						*link = removed->next;
						record_count--;
					}
				}
			}
			delete removed;
			page->completion.Release();
		}

		stduint ReclaimInactive(stduint limit) {
			SharedFilePageRecord* removed = nullptr;
			{
				MutexLocal guard(&lock);
				removed = DetachInactive(limit);
			}
			stduint count = 0;
			for (auto* record = removed; record; record = record->next) count++;
			DeleteRecords(removed);
			return count;
		}

		bool SnapshotInode(uint64 identity, uni::Vector<stduint>& offsets) {
			MutexLocal guard(&lock);
			for (stduint bucket = 0; bucket < BucketCount; bucket++)
				for (auto* record = buckets[bucket]; record; record = record->next)
					if (record->page->key.identity == identity &&
						record->page->state == FileCacheEntryState::Ready &&
						!offsets.Append(stduint(record->page->key.file_offset)))
						return false;
			return true;
		}

		void CompleteTruncate(uint64 identity, stduint old_size,
			stduint new_size) {
			MutexLocal guard(&lock);
			for (stduint bucket = 0; bucket < BucketCount; bucket++) {
				for (auto* record = buckets[bucket]; record; record = record->next) {
					SharedFilePage* page = record->page.get();
					if (page->key.identity != identity ||
						page->state != FileCacheEntryState::Ready) continue;
					const stduint offset = stduint(page->key.file_offset);
					MutexLocal page_guard(&page->lock);
					if (new_size < old_size && offset < new_size &&
						new_size - offset < PAGE_SIZE)
						MemSet((byte*)page->physical_address + new_size - offset,
							0, PAGE_SIZE - (new_size - offset));
					else if (new_size < old_size && offset >= new_size) {
						MemSet((void*)page->physical_address, 0, PAGE_SIZE);
						page->dirty = false;
						page->dirty_inode = {};
					}
					else if (new_size > old_size && offset < new_size &&
						(old_size <= offset || old_size - offset < PAGE_SIZE)) {
						const stduint begin = old_size > offset ? old_size - offset : 0;
						const stduint end = minof(_IMM(PAGE_SIZE), new_size - offset);
						if (begin < end)
							MemSet((byte*)page->physical_address + begin, 0, end - begin);
					}
				}
			}
		}

		void ForgetInode(uint64 identity) {
			SharedFilePageRecord* removed = nullptr;
			{
				MutexLocal guard(&lock);
				for (stduint bucket = 0; bucket < BucketCount; bucket++) {
					SharedFilePageRecord** link = &buckets[bucket];
					while (*link) {
						SharedFilePage* page = (*link)->page.get();
						if (page->key.identity != identity) {
							link = &(*link)->next;
							continue;
						}
						{
							MutexLocal page_guard(&page->lock);
							KASSERT(!page->dirty && !page->aliases);
						}
						SharedFilePageRecord* record = *link;
						*link = record->next;
						record->next = removed;
						removed = record;
						record_count--;
					}
				}
			}
			DeleteRecords(removed);
		}
	};

	SharedFilePageTable shared_file_pages;

	SharedFilePageOwner FindReadySharedFilePageWithoutWait(
		const SharedFilePageKey& key) {
		return shared_file_pages.FindReady(key);
	}

	SharedFilePageOwner FindReadySharedFilePage(const SharedFilePageKey& key) {
		auto page = shared_file_pages.Find(key);
		if (!page) return {};
		page->completion.Acquire();
		const bool ready = page->state == FileCacheEntryState::Ready;
		page->completion.Release();
		return ready ? page : SharedFilePageOwner();
	}

	SharedFilePageOwner GetSharedFilePage(uni::vfs_inode* inode,
		const SharedFilePageKey& key) {
		bool filler = false;
		SharedFilePageOwner page = shared_file_pages.FindOrInsert(key, filler);
		if (!page) return {};
		if (!filler) {
			page->completion.Acquire();
			const bool ready = page->state == FileCacheEntryState::Ready;
			page->completion.Release();
			return ready ? page : SharedFilePageOwner();
		}

		void* physical = mempool.allocate(PAGE_SIZE, 12);
		if ((!physical || physical == (void*)~_IMM0) &&
			shared_file_pages.ReclaimInactive(1))
			physical = mempool.allocate(PAGE_SIZE, 12);
		if (!physical || physical == (void*)~_IMM0) {
			shared_file_pages.Complete(page, 0);
			return {};
		}
		// The backend and kernel access this physical page through its direct address.
		MemSet(physical, 0, PAGE_SIZE);
		{
			MutexLocal guard(&uni::vfs_lock);
			bool filled = false;
			if (inode->identity == key.identity) {
				uni::FilesysTrait* fs = inode->mount_state ?
					inode->mount_state->filesystem :
					(inode->i_sb ? inode->i_sb->fs : nullptr);
				const stduint length = key.file_offset < inode->i_size ?
					minof(_IMM(PAGE_SIZE), inode->i_size - stduint(key.file_offset)) : 0;
				filled = !length || (fs && fs->readfl(inode->internal_handler,
					Slice { stduint(key.file_offset), length },
					(byte*)physical) == length);
			}
			if (!filled) {
				mempool.deallocate(physical, PAGE_SIZE);
				shared_file_pages.Complete(page, 0);
				return {};
			}
			if (!Memory::AdoptManagedPage(_IMM(physical), ManagedPageSource::File)) {
				shared_file_pages.Complete(page, 0);
				return {};
			}
			if (!Memory::RetainManagedPageCache(_IMM(physical))) {
				Memory::ReleaseManagedPageMapping(_IMM(physical));
				shared_file_pages.Complete(page, 0);
				return {};
			}
			if (!Memory::ReleaseManagedPageMapping(_IMM(physical))) {
				Memory::ReleaseManagedPageCache(_IMM(physical));
				shared_file_pages.Complete(page, 0);
				return {};
			}
			shared_file_pages.Complete(page, _IMM(physical));
		}
		return page;
	}

	bool AliasPageMatches(SharedFilePage* page, SharedFileAlias* alias,
		stduint& properties, PageEntry*& leaf) {
		bool vma_matches = false;
		for (stduint i = 0; i < alias->process->vmas.Count(); i++) {
			const auto& vma = alias->process->vmas[i];
			if (alias->address < vma.vm_start || alias->address >= vma.vm_end)
				continue;
			vma_matches = vma.vm_type == VirtualMemoryAreaType::File &&
				vma.vm_mode == VirtualMemoryAreaMode::Shared && vma.vfile &&
				vma.vfile->f_inode &&
				vma.vfile->f_inode->identity == page->key.identity &&
				uint64(vma.file_offset) + uint64(alias->address - vma.vm_start) ==
					page->key.file_offset;
			break;
		}
		if (!vma_matches) return false;
		pageint index = alias->address;
		leaf = alias->process->paging.getEntry(index);
		return leaf && _IMM(leaf) != ~_IMM0 && index.crt_level == 0 &&
			leaf->DecodeLeafProperties(index.crt_level, properties) &&
			(properties & PGPROP_present) && (properties & PGPROP_user_access) &&
			_IMM(alias->process->paging[alias->address]) == page->physical_address;
	}

	// Caller holds vfs_lock and the page mutex. Never wait with vma_lock held.
	bool ProtectSharedFileAliases(SharedFilePage* page) {
		for (auto* alias = page->aliases; alias; alias = alias->next) {
			stduint properties = 0;
			PageEntry* leaf = nullptr;
			bool changed_permissions = false;
			{
				SpinlockLocal vma_guard(&alias->process->vma_lock);
				if (!AliasPageMatches(page, alias, properties, leaf)) return false;
				if (properties & PGPROP_writable) {
					#if _MCCA != 0x8632 && _SYS_MULTICORE
					if (Taskman::PCU_CORES > 1) return false;
					#endif
					pageint index = alias->address;
					PageEntry protected_page;
					if (!protected_page.SetupAsLeaf(page->physical_address,
						index.crt_level, properties & ~PGPROP_writable)) return false;
					leaf->raw = protected_page.raw;
					changed_permissions = true;
				}
			}
			if (changed_permissions)
				Memory::InvalidateProcessPage(alias->process, alias->address);
		}
		return true;
	}

	// The copy mutex serializes epoch flips. A counted MccaMemCopyP has passed
	// preflight and only copies physical bytes, so it never waits for vfs_lock.
	// New unrelated copies join the next epoch and cannot starve this wait.
	void WaitForSharedFileCopies() {
		const stduint previous = uni::shared_file_copy_epoch.fetch_add(1,
			uni::MemoryOrder_Seq_Cst);
		while (uni::shared_file_active_user_copies[previous & 1].load(
			uni::MemoryOrder_Acquire)) Taskman::Schedule(true);
	}
}

namespace uni {
	bool PrepareSharedFileTruncate(vfs_inode* inode);
	// Caller holds vfs_lock; regular writes, truncation, and shared writes check this list.
	bool ExecutableFilePinned(vfs_inode* inode) {
		if (!inode) return false;
		for (auto* pin = executable_image_pins; pin; pin = pin->next)
			if (pin->inode.get() == inode) return true;
		return false;
	}

	static bool PinExecutableImageInternal(ProcessBlock* process, vfs_file* file,
		uint64 expected_version, bool mapped_file, bool& created) {
		created = false;
		if (!process || !file || !file->f_inode) return false;
		auto* pin = new ExecutableImagePin { process, file->f_inode,
			mapped_file, nullptr };
		if (!pin) return false;
		bool pinned = false;
		bool already_pinned = false;
		{
			MutexLocal copy_guard(&shared_file_copy_lock);
			MutexLocal file_guard(&vfs_lock);
			if (mapped_file) {
				for (auto* previous = executable_image_pins; previous;
					previous = previous->next) {
					if (previous->mapped_file && previous->process == process &&
						previous->inode.get() == file->f_inode.get()) {
						already_pinned = true;
						break;
					}
				}
			}
			// Revoke writable shared aliases before freezing the ELF image.
			bool clean = !already_pinned && (mapped_file ||
				file->f_inode->content_version == expected_version) &&
				PrepareSharedFileTruncate(file->f_inode.get());
			uni::Vector<stduint> offsets;
			if (clean && !shared_file_pages.SnapshotInode(
				file->f_inode->identity, offsets)) clean = false;
			// Dirty shared pages would make the backend ELF image inconsistent.
			for (stduint i = 0; clean && i < offsets.Count(); i++) {
				auto page = FindReadySharedFilePageWithoutWait(
					{ file->f_inode->identity, uint64(offsets[i]) });
				if (!page) continue;
				MutexLocal page_guard(&page->lock);
				if (page->dirty) clean = false;
			}
			if (clean) {
				pin->next = executable_image_pins;
				executable_image_pins = pin;
				pinned = true;
				created = true;
			}
		}
		if (!pinned) delete pin;
		return pinned || already_pinned;
	}

	bool PinExecutableImage(ProcessBlock* process, vfs_file* file,
		uint64 expected_version) {
		bool created = false;
		return PinExecutableImageInternal(process, file, expected_version,
			false, created);
	}

	bool PinMappedExecutableImage(ProcessBlock* process, vfs_file* file,
		bool& created) {
		return PinExecutableImageInternal(process, file, 0, true, created);
	}

	void UnpinMappedExecutableImage(ProcessBlock* process, vfs_inode* inode) {
		ExecutableImagePin* removed = nullptr;
		{
			MutexLocal guard(&vfs_lock);
			auto** link = &executable_image_pins;
			while (*link) {
				if (!(*link)->mapped_file || (*link)->process != process ||
					(*link)->inode.get() != inode) {
					link = &(*link)->next;
					continue;
				}
				removed = *link;
				*link = removed->next;
				break;
			}
		}
		delete removed;
	}

	bool CloneExecutableImagePin(ProcessBlock* parent, ProcessBlock* child) {
		MutexLocal guard(&vfs_lock);
		for (auto* source = executable_image_pins; source; source = source->next) {
			if (source->process != parent) continue;
			auto* pin = new ExecutableImagePin { child, source->inode,
				source->mapped_file, executable_image_pins };
			if (!pin) return false;
			executable_image_pins = pin;
		}
		return true;
	}

	void UnpinExecutableImage(ProcessBlock* process) {
		ExecutableImagePin* removed = nullptr;
		{
			MutexLocal guard(&vfs_lock);
			auto** link = &executable_image_pins;
			while (*link) {
				if ((*link)->process != process) {
					link = &(*link)->next;
					continue;
				}
				auto* pin = *link;
				*link = pin->next;
				pin->next = removed;
				removed = pin;
			}
		}
		while (removed) {
			auto* next = removed->next;
			delete removed;
			removed = next;
		}
	}

	// Replace the old exec pin without exposing either inode to a writable gap.
	bool TransferExecutableImagePin(ProcessBlock* source,
		ProcessBlock* destination) {
		if (!source || !destination || source == destination) return false;
		ExecutableImagePin* removed = nullptr;
		bool transferred = false;
		{
			MutexLocal guard(&vfs_lock);
			for (auto* pin = executable_image_pins; pin; pin = pin->next) {
				if (pin->process == source) {
					transferred = true;
					break;
				}
			}
			if (!transferred) return false;
			auto** link = &executable_image_pins;
			while (*link) {
				if ((*link)->process != destination) {
					link = &(*link)->next;
					continue;
				}
				auto* pin = *link;
				*link = pin->next;
				pin->next = removed;
				removed = pin;
			}
			for (auto* pin = executable_image_pins; pin; pin = pin->next) {
				if (pin->process != source) continue;
				pin->process = destination;
			}
		}
		while (removed) {
			auto* next = removed->next;
			delete removed;
			removed = next;
		}
		return transferred;
	}

	// Caller holds vfs_lock. A loading page will observe the completed I/O
	// before publication because its filler also publishes under vfs_lock.
	bool ReadSharedFilePageIfPresent(vfs_inode* inode, stduint file_offset,
		stduint page_offset, void* destination, stduint length) {
		auto page = FindReadySharedFilePageWithoutWait(
			{ inode->identity, uint64(file_offset) });
		if (!page) return false;
		MutexLocal page_guard(&page->lock);
		MemCopyN(destination, (const byte*)page->physical_address + page_offset,
			length);
		return true;
	}

	// Caller holds shared_file_copy_lock, then vfs_lock.
	bool PrepareSharedFileWrite(vfs_inode* inode, stduint position,
		stduint length) {
		if (!length) return true;
		if (length > ~_IMM0 - position) return false;
		const stduint end = position + length;
		bool shared_page_found = false;
		for (stduint offset = position & ~_IMM(PAGE_SIZE - 1);
			offset < end;) {
			auto page = FindReadySharedFilePageWithoutWait(
				{ inode->identity, uint64(offset) });
			if (page) {
				shared_page_found = true;
				MutexLocal page_guard(&page->lock);
				if (!ProtectSharedFileAliases(page.get())) return false;
			}
			if (end - offset <= PAGE_SIZE) break;
			offset += PAGE_SIZE;
		}
		if (shared_page_found) WaitForSharedFileCopies();
		return true;
	}

	// Caller holds shared_file_copy_lock, then vfs_lock. Only bytes actually
	// accepted by the backend are copied into already-published shared pages.
	void UpdateSharedFilePages(vfs_inode* inode, stduint position,
		const byte* data, stduint length) {
		while (length) {
			const stduint offset = position & ~_IMM(PAGE_SIZE - 1);
			const stduint within = position - offset;
			const stduint chunk = minof(length, _IMM(PAGE_SIZE) - within);
			auto page = FindReadySharedFilePageWithoutWait(
				{ inode->identity, uint64(offset) });
			if (page) {
				MutexLocal page_guard(&page->lock);
				MemCopyN((byte*)page->physical_address + within, data, chunk);
			}
			position += chunk;
			data += chunk;
			length -= chunk;
		}
	}

	// Caller holds shared_file_copy_lock, then vfs_lock. Protection is done
	// before the backend changes size; failure leaves the inode unchanged.
	bool PrepareSharedFileTruncate(vfs_inode* inode) {
		uni::Vector<stduint> offsets;
		if (!shared_file_pages.SnapshotInode(inode->identity, offsets)) return false;
		bool shared_page_found = false;
		for (stduint i = 0; i < offsets.Count(); i++) {
			auto page = FindReadySharedFilePageWithoutWait(
				{ inode->identity, uint64(offsets[i]) });
			if (!page) continue;
			shared_page_found = true;
			MutexLocal page_guard(&page->lock);
			if (!ProtectSharedFileAliases(page.get())) return false;
		}
		if (shared_page_found) WaitForSharedFileCopies();
		return true;
	}

	// Caller holds shared_file_copy_lock, then vfs_lock.
	void CompleteSharedFileTruncate(vfs_inode* inode, stduint old_size,
		stduint new_size) {
		shared_file_pages.CompleteTruncate(inode->identity, old_size, new_size);
	}

	void InvalidateFileCacheVersions(uint64 identity, uint64 current_version) {
		file_cache.InvalidateVersions(identity, current_version);
	}

	void ForgetFileCacheInode(uint64 identity) {
		file_cache.ForgetInode(identity);
		shared_file_pages.ForgetInode(identity);
	}

	// The caller owns one mapping reference after success. The shared table owns
	// an independent cache reference until the inode is forgotten.
	bool AcquireSharedFileMappingPage(vfs_inode* inode, stduint file_offset,
		stduint& physical_address) {
		physical_address = 0;
		if (!inode || (file_offset & (PAGE_SIZE - 1))) return false;
		const SharedFilePageKey key { inode->identity, uint64(file_offset) };
		auto page = GetSharedFilePage(inode, key);
		if (!page || !Memory::RetainManagedPageMapping(page->physical_address))
			return false;
		physical_address = page->physical_address;
		return true;
	}

	// The page lock and VMA spinlock publish the PTE and its alias together.
	// A first write faults so ResolveSharedFileWrite can pin and dirty the page.
	void* MapSharedFileVmaPage(ProcessBlock* process, stduint address,
		VirtualMemoryAccess access, stduint expected_flags,
		stduint file_offset, vfs_inode* inode) {
		stduint physical = 0;
		if (!AcquireSharedFileMappingPage(inode, file_offset, physical))
			return nullptr;
		auto page = FindReadySharedFilePage(
			{ inode->identity, uint64(file_offset) });
		auto* alias = new SharedFileAlias { process, address, nullptr };
		void* result = nullptr;
		bool installed = false;
		if (page && alias && page->physical_address == physical) {
			MutexLocal page_guard(&page->lock);
			SpinlockLocal vma_guard(&process->vma_lock);
			for (stduint i = 0; i < process->vmas.Count(); i++) {
				const auto& vma = process->vmas[i];
				if (address < vma.vm_start || address >= vma.vm_end) continue;
				if (vma.vm_type != VirtualMemoryAreaType::File ||
					vma.vm_mode != VirtualMemoryAreaMode::Shared ||
					vma.vm_flags != expected_flags || !vma.vfile ||
					vma.vfile->f_inode.get() != inode ||
					uint64(vma.file_offset) + uint64(address - vma.vm_start) !=
						uint64(file_offset)) break;
				if (_IMM(process->paging[address]) != ~_IMM0) {
					pageint index = address;
					PageEntry* leaf = process->paging.getEntry(index);
					stduint properties = 0;
					if (_IMM(process->paging[address]) == physical &&
						leaf && _IMM(leaf) != ~_IMM0 &&
						leaf->DecodeLeafProperties(index.crt_level, properties) &&
						(access != VirtualMemoryAccess::Write ||
							(properties & PGPROP_writable)) &&
						(access != VirtualMemoryAccess::Execute ||
							!(properties & PGPROP_nonexecutable)))
						result = process->paging[address];
					break;
				}
				const stduint properties = vma.vm_flags & (PGPROP_present |
					PGPROP_user_access | PGPROP_nonexecutable);
				if (process->paging.Map(address, physical, PAGE_SIZE,
					PAGESIZE_4KB, properties)) {
					alias->next = page->aliases;
					page->aliases = alias;
					installed = true;
					result = access == VirtualMemoryAccess::Write ?
						nullptr : (void*)physical;
				}
				break;
			}
		}
		if (installed) Memory::InvalidateProcessPage(process, address);
		else {
			delete alias;
			KASSERT(Memory::ReleaseManagedPageMapping(physical));
		}
		return result;
	}

	// The caller holds process->mutex_mapping, or the child is unpublished.
	// The PTE has already been installed read-only.
	bool RegisterSharedFileAlias(vfs_inode* inode, stduint file_offset,
		ProcessBlock* process, stduint address, stduint physical_address) {
		if (!inode || !process || (address & (PAGE_SIZE - 1))) return false;
		auto page = FindReadySharedFilePage({ inode->identity, uint64(file_offset) });
		if (!page ||
			page->physical_address != physical_address) return false;
		auto* alias = new SharedFileAlias { process, address, nullptr };
		if (!alias) return false;
		bool registered = false;
		{
			MutexLocal guard(&page->lock);
			SpinlockLocal vma_guard(&process->vma_lock);
			stduint properties = 0;
			PageEntry* leaf = nullptr;
			if (AliasPageMatches(page.get(), alias, properties, leaf) &&
				!(properties & PGPROP_writable)) {
				bool duplicate = false;
				for (auto* current = page->aliases; current; current = current->next)
					if (current->process == process && current->address == address)
						duplicate = true;
				if (!duplicate) {
					alias->next = page->aliases;
					page->aliases = alias;
					registered = true;
				}
			}
		}
		if (!registered) delete alias;
		return registered;
	}

	// The caller holds process->mutex_mapping and unregisters before unmapping.
	bool UnregisterSharedFileAlias(uint64 identity, stduint file_offset,
		ProcessBlock* process, stduint address) {
		auto page = FindReadySharedFilePage({ identity, uint64(file_offset) });
		if (!page) return false;
		SharedFileAlias* removed = nullptr;
		{
			MutexLocal guard(&page->lock);
			SharedFileAlias** link = &page->aliases;
			while (*link && ((*link)->process != process ||
				(*link)->address != address)) link = &(*link)->next;
			if (*link) {
				removed = *link;
				*link = removed->next;
			}
		}
		const bool found = removed != nullptr;
		delete removed;
		return found;
	}

	// The caller holds process->mutex_mapping. Only registered shared pages
	// reach this path; private COW and legacy mappings retain their own path.
	void* ResolveSharedFileWrite(ProcessBlock* process, stduint address) {
		MutexLocal copy_guard(&shared_file_copy_lock);
		SharedFilePageKey key = {};
		SharedPtr<vfs_inode> inode;
		{
			SpinlockLocal guard(&process->vma_lock);
			for (stduint i = 0; i < process->vmas.Count(); i++) {
				const auto& vma = process->vmas[i];
				if (address < vma.vm_start || address >= vma.vm_end) continue;
				if (vma.vm_type == VirtualMemoryAreaType::File &&
					vma.vm_mode == VirtualMemoryAreaMode::Shared &&
					(vma.vm_flags & PGPROP_writable) && vma.vfile &&
					vma.vfile->f_inode) {
					inode = vma.vfile->f_inode;
					key = { inode->identity, uint64(vma.file_offset) +
						uint64(address - vma.vm_start) };
				}
				break;
			}
		}
		if (!inode || !key.Valid()) return nullptr;
		auto page = FindReadySharedFilePage(key);
		if (!page) return nullptr;
		MutexLocal file_guard(&vfs_lock);
		if (ExecutableFilePinned(inode.get())) return nullptr;
		if (key.file_offset >= inode->i_size) return nullptr;
		{
			MutexLocal guard(&page->lock);
			bool registered = false;
			for (auto* alias = page->aliases; alias; alias = alias->next)
				if (alias->process == process && alias->address == address)
					registered = true;
			if (!registered) return nullptr;
			SharedFileAlias current { process, address, nullptr };
			SpinlockLocal vma_guard(&process->vma_lock);
			stduint properties = 0;
			PageEntry* leaf = nullptr;
			if (!AliasPageMatches(page.get(), &current, properties, leaf) ||
				(properties & PGPROP_writable)) return nullptr;
			pageint index = address;
			PageEntry writable;
			if (!writable.SetupAsLeaf(page->physical_address, index.crt_level,
				properties | PGPROP_writable)) return nullptr;
			page->dirty_inode = inode;
			page->dirty = true;
			leaf->raw = writable.raw;
		}
		Memory::InvalidateProcessPage(process, address);
		return (void*)page->physical_address;
	}

	// Caller holds the copy mutex; take vfs_lock, then the page lock. Protect aliases and
	// drain the previous physical-copy epoch before reading page bytes.
	// A failed write preserves the dirty bit and inode pin for a later retry.
	bool FlushSharedFilePageWithCopyLock(uint64 identity, stduint file_offset) {
		auto page = FindReadySharedFilePage({ identity, uint64(file_offset) });
		if (!page) return true;
		MutexLocal file_guard(&vfs_lock);
		SharedPtr<vfs_inode> inode;
		{
			MutexLocal page_guard(&page->lock);
			if (!page->dirty) return true;
			inode = page->dirty_inode;
			if (!inode || inode->identity != identity) return false;
			if (!ProtectSharedFileAliases(page.get())) return false;
			WaitForSharedFileCopies();
			if (file_offset >= inode->i_size) return false;
			const stduint length = minof(_IMM(PAGE_SIZE),
				inode->i_size - file_offset);
			FilesysTrait* fs = inode->mount_state ?
				inode->mount_state->filesystem :
				(inode->i_sb ? inode->i_sb->fs : nullptr);
			if (!fs) return false;
			const stduint written = fs->writfl(inode->internal_handler,
				Slice { file_offset, length },
				(const byte*)page->physical_address);
			if (written) {
				KASSERT(inode->content_version != ~(uint64)0);
				inode->content_version++;
				file_cache.InvalidateVersions(identity, inode->content_version);
			}
			if (written != length) return false;
			page->dirty = false;
			page->dirty_inode = {};
		}
		return true;
	}

	bool FlushSharedFilePage(uint64 identity, stduint file_offset) {
		MutexLocal copy_guard(&shared_file_copy_lock);
		return FlushSharedFilePageWithCopyLock(identity, file_offset);
	}

	// A successful call transfers one managed mapping reference to the caller.
	// A zero physical address denotes a page wholly beyond the current EOF.
	bool AcquireCachedFileMappingPage(vfs_inode* inode, stduint file_offset,
		uint64& version, stduint& physical_address) {
		version = 0;
		physical_address = 0;
		if (!inode || (file_offset & (PAGE_SIZE - 1))) return false;
		for (;;) {
			FileCacheKey key = {};
			{
				MutexLocal guard(&vfs_lock);
				key = { inode->identity, inode->content_version,
					uint64(file_offset) };
				if (file_offset >= inode->i_size) {
					version = key.content_version;
					return key.Valid();
				}
			}
			FileCacheEntryOwner entry;
			const FileCachePageResult page = GetCachedPage(inode, key, entry);
			if (page == FileCachePageResult::Retry) continue;
			if (page == FileCachePageResult::Error) return false;
			const stduint candidate = entry->physical_address;
			if (!Memory::RetainManagedPageMapping(candidate)) return false;
			bool current = false;
			{
				MutexLocal guard(&vfs_lock);
				current = inode->identity == key.identity &&
					inode->content_version == key.content_version &&
					file_offset < inode->i_size;
			}
			if (!current) {
				const bool released = Memory::ReleaseManagedPageMapping(candidate);
				KASSERT(released);
				continue;
			}
			version = key.content_version;
			physical_address = candidate;
			return true;
		}
	}

	int ReadCachedRegularFile(vfs_file* file, void* buffer, stduint count) {
		if (!file || !file->f_inode || (!buffer && count)) return -1;
		byte* destination = (byte*)buffer;
		stduint total = 0;
		stduint expected_position = 0;
		uint64 read_version = 0;
		while (total < count) {
			FileCacheKey key = {};
			stduint position = 0;
			stduint page_offset = 0;
			stduint length = 0;
			{
				MutexLocal guard(&vfs_lock);
				vfs_inode* inode = file->f_inode.get();
				// A concurrent write or offset change ends this read at a page boundary.
				if (total && (inode->content_version != read_version ||
					file->f_pos != expected_position)) break;
				if (file->f_pos >= inode->i_size) break;
				position = file->f_pos;
				page_offset = file->f_pos & (PAGE_SIZE - 1);
				key = { inode->identity, inode->content_version,
					uint64(file->f_pos - page_offset) };
				length = minof(count - total, minof(
					_IMM(PAGE_SIZE) - page_offset, inode->i_size - file->f_pos));
				if (ReadSharedFilePageIfPresent(inode, stduint(key.file_offset),
					page_offset, destination + total, length)) {
					file->f_pos += length;
					total += length;
					expected_position = file->f_pos;
					read_version = key.content_version;
					continue;
				}
			}
			FileCacheEntryOwner entry;
			const FileCachePageResult page = GetCachedPage(file->f_inode.get(), key, entry);
			if (page == FileCachePageResult::Retry) continue;
			if (page == FileCachePageResult::Error) return total ? int(total) : -1;
			{
				MutexLocal guard(&vfs_lock);
				vfs_inode* inode = file->f_inode.get();
				if (inode->identity != key.identity ||
					inode->content_version != key.content_version ||
					file->f_pos != position) continue;
				length = minof(length, inode->i_size - file->f_pos);
				if (!ReadSharedFilePageIfPresent(inode, stduint(key.file_offset),
					page_offset, destination + total, length))
					MemCopyN(destination + total,
						(const byte*)entry->physical_address + page_offset, length);
				file->f_pos += length;
				total += length;
				expected_position = file->f_pos;
				read_version = key.content_version;
			}
		}
		return int(total);
	}
}
#endif
