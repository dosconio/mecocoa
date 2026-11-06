#include "../include/mecocoa.hpp"
#include "../include/devsman-storage.hpp"

#if (_MCCA & 0xFF00) == 0x8600
namespace Powercall {
	struct StorageSession {
		Mutex transaction;
		uni::Atomic<byte> online = 0;
		DeviceNode* controller = nullptr;
		stduint owner_pid = 0;
		stduint owner_tid = 0;
	};

	namespace {
		uni::Vector<StorageSession*> sessions;
		uni::Vector<StorageAdapter*> adapters;

		bool ValidName(const char* name) {
			if (!name[0]) return false;
			for (stduint i = 0; i < StorageDriverNameCapacity; ++i) {
				if (!name[i]) return true;
				if (name[i] == '/' || name[i] == '\\') return false;
			}
			return false;
		}

		bool ReceiveReply(stduint tid, StorageDriverMsg expected, StorageDriverReply& reply) {
			stduint type = 0;
			return !sysrecv(tid, &reply, sizeof(reply), &type) &&
				type == _IMM(expected) && reply.version == StorageDriverProtocolVersion;
		}
	}

	StorageAdapter::StorageAdapter(StorageSession* session, DeviceNode* node, const StorageDriverInfo& info)
		: session(session), node(node), unit(info.unit), block_count(info.block_count),
			slice_type(info.slice_type) {
		Block_Size = info.block_size;
		Block_buffer = new byte[Block_Size];
		readable = (info.flags & StorageDriverFlag_Readable) != 0;
		writable = (info.flags & StorageDriverFlag_Writable) != 0;
		media_present.store((info.flags & StorageDriverFlag_MediaPresent) != 0,
			uni::MemoryOrder_Release);
	}

	StorageAdapter::~StorageAdapter() { delete[] static_cast<byte*>(Block_buffer); }

	bool StorageAdapter::HasMedia() const {
		return session->online.load(uni::MemoryOrder_Acquire) &&
			online.load(uni::MemoryOrder_Acquire) &&
			media_present.load(uni::MemoryOrder_Acquire);
	}

	bool StorageAdapter::ReadBlock(stduint block, void* dest) {
		StorageDriverRequest request = { StorageDriverProtocolVersion, unit, uint32(block) };
		StorageDriverReply reply = {};
		if (syssend(session->owner_tid, &request, sizeof(request), _IMM(StorageDriverMsg::Read))) {
			plogwarn("[Storage] read request failed tid=%u unit=%u block=%u",
				session->owner_tid, unit, uint32(block));
			session->online.store(0, uni::MemoryOrder_Release);
			return false;
		}
		if (!ReceiveReply(session->owner_tid, StorageDriverMsg::Complete, reply)) {
			plogwarn("[Storage] read completion failed tid=%u unit=%u block=%u",
				session->owner_tid, unit, uint32(block));
			session->online.store(0, uni::MemoryOrder_Release);
			return false;
		}
		if (reply.status) {
			plogwarn("[Storage] device read failed tid=%u unit=%u block=%u status=%d",
				session->owner_tid, unit, uint32(block), reply.status);
			return false;
		}
		if (reply.bytes != Block_Size) {
			plogwarn("[Storage] read size mismatch tid=%u unit=%u block=%u bytes=%u expected=%u",
				session->owner_tid, unit, uint32(block), reply.bytes, Block_Size);
			session->online.store(0, uni::MemoryOrder_Release);
			return false;
		}
		stduint type = 0;
		MemSet(dest, 0, Block_Size);
		if (sysrecv(session->owner_tid, dest, Block_Size, &type) ||
			type != _IMM(StorageDriverMsg::Data)) {
			plogwarn("[Storage] read data failed tid=%u unit=%u block=%u type=%u",
				session->owner_tid, unit, uint32(block), type);
			session->online.store(0, uni::MemoryOrder_Release);
			return false;
		}
		return true;
	}

	bool StorageAdapter::Read(stduint block, void* dest, stduint times) {
		if (Taskman::CurrentTID() >= Task_Init || !dest || !times || !readable ||
			block >= block_count || times > block_count - block ||
			times > stduint(-1) / Block_Size) return false;
		MutexLocal guard(&session->transaction);
		if (!HasMedia()) return false;
		for (stduint i = 0; i < times; ++i) {
			byte* target = static_cast<byte*>(dest) + i * Block_Size;
			if (!ReadBlock(block + i, target)) return false;
			if (Block_buffer && target != Block_buffer)
				MemCopyN(Block_buffer, target, Block_Size);
			cached_block = block + i;
		}
		return true;
	}

	bool StorageAdapter::Write(stduint block, const void* source, stduint times) {
		if (Taskman::CurrentTID() >= Task_Init || !source || !times || !writable ||
			block >= block_count || times > block_count - block ||
			times > stduint(-1) / Block_Size) return false;
		MutexLocal guard(&session->transaction);
		if (!HasMedia()) return false;
		for (stduint i = 0; i < times; ++i) {
			cached_block = stduint(-1);
			StorageDriverRequest request = { StorageDriverProtocolVersion, unit, uint32(block + i) };
			StorageDriverReply reply = {};
			if (syssend(session->owner_tid, &request, sizeof(request), _IMM(StorageDriverMsg::Write)) ||
				!ReceiveReply(session->owner_tid, StorageDriverMsg::Ready, reply)) {
				session->online.store(0, uni::MemoryOrder_Release);
				return false;
			}
			if (reply.status) return false;
			if (reply.bytes != Block_Size ||
				syssend(session->owner_tid, static_cast<const byte*>(source) + i * Block_Size,
					Block_Size, _IMM(StorageDriverMsg::Data)) ||
					!ReceiveReply(session->owner_tid, StorageDriverMsg::Complete, reply)) {
				session->online.store(0, uni::MemoryOrder_Release);
				return false;
			}
			if (reply.status) return false;
			if (reply.bytes != Block_Size) {
				session->online.store(0, uni::MemoryOrder_Release);
				return false;
			}
		}
		return true;
	}

	stduint StorageAdapter::getUnits() { return block_count; }

	uni::PartitionSlice StorageAdapter::getSlice(stduint dev) {
		uni::PartitionSlice slice = {};
		if (!dev) {
			slice.address = 0;
			slice.length = block_count;
			slice.sys_id = (ClassicFilesysType)byte(slice_type);
		}
		return slice;
	}

	int StorageAdapter::operator[](uint64 offset) {
		if (!Block_Size || offset / Block_Size >= block_count || !Block_buffer || !readable)
			return -1;
		MutexLocal guard(&session->transaction);
		if (!HasMedia()) return -1;
		const stduint block = stduint(offset / Block_Size);
		if (cached_block != block) {
			if (!ReadBlock(block, Block_buffer)) return -1;
			cached_block = block;
		}
		return static_cast<byte*>(Block_buffer)[stduint(offset % Block_Size)];
	}

	StorageAdapter* StorageAdapter::Register(stduint sender_tid, const StorageDriverInfo& info) {
		if (sender_tid < TaskCount || info.version != StorageDriverProtocolVersion ||
			!info.block_size || info.block_size > StorageDriverMaxBlockSize ||
			!info.block_count || info.slice_type > 255 ||
			!(info.flags & StorageDriverFlag_Readable) ||
			(info.flags & ~(StorageDriverFlag_Readable | StorageDriverFlag_Writable | StorageDriverFlag_MediaPresent)) ||
			!ValidName(info.name)) return nullptr;
		stduint owner_pid = 0;
		DeviceNode* controller = ResolveOwnedDeviceHandle(sender_tid, info.dev_handle, &owner_pid);
		if (!controller || controller->fields.binding.owner_pid != owner_pid) return nullptr;
		StorageSession* session = nullptr;
		for (stduint i = 0; i < sessions.Count(); ++i) {
			if (sessions[i]->controller != controller) continue;
			if (sessions[i]->online.load(uni::MemoryOrder_Acquire)) {
				if (sessions[i]->owner_tid != sender_tid) return nullptr;
			}
			session = sessions[i];
			break;
		}
		DeviceNode* node = nullptr;
		for (auto* child = reinterpret_cast<DeviceNode*>(controller->link.subf); child;
			child = reinterpret_cast<DeviceNode*>(child->link.next)) {
			if (child->link.addr && StrCompare(child->link.addr, info.name) == 0) {
				node = child;
				break;
			}
		}
		if (node && (DeviceNodeType(node->fields.node_type) != DeviceNodeType::StorageDevice ||
			(node->fields.binding.owner_pid && node->fields.binding.owner_pid != owner_pid))) return nullptr;
		StorageAdapter* existing = nullptr;
		for (stduint i = 0; i < adapters.Count(); ++i) {
			if (adapters[i]->session == session && adapters[i]->unit == info.unit) {
				existing = adapters[i];
				break;
			}
		}
		if (existing) {
			if (existing->online.load(uni::MemoryOrder_Acquire) || existing->node != node ||
				existing->Block_Size != info.block_size ||
				existing->block_count != info.block_count || existing->slice_type != info.slice_type ||
				(node->fields.binding.driver_data && node->fields.binding.driver_data != existing))
				return nullptr;
			session->owner_pid = owner_pid;
			session->owner_tid = sender_tid;
			existing->readable = (info.flags & StorageDriverFlag_Readable) != 0;
			existing->writable = (info.flags & StorageDriverFlag_Writable) != 0;
			existing->cached_block = stduint(-1);
			existing->media_present.store((info.flags & StorageDriverFlag_MediaPresent) != 0,
				uni::MemoryOrder_Release);
			if (!Devsman::AttachStorageOps(node, existing)) return nullptr;
			node->fields.binding.owner_pid = uint32(owner_pid);
			existing->online.store(1, uni::MemoryOrder_Release);
			session->online.store(1, uni::MemoryOrder_Release);
			return existing;
		}
		if (node && node->fields.binding.driver_data) return nullptr;
		if (!node) node = Devsman::RegisterStorageDevice(controller, info.name,
			DeviceBusType(controller->fields.bus_type));
		if (!node) return nullptr;
		if (!session) {
			session = new StorageSession;
			if (!session) return nullptr;
			session->controller = controller;
			sessions.Append(session);
		}
		session->owner_pid = owner_pid;
		session->owner_tid = sender_tid;
		auto* adapter = new StorageAdapter(session, node, info);
		if (!adapter) return nullptr;
		if (!adapter->Block_buffer || !Devsman::AttachStorageOps(node, adapter)) {
			delete adapter;
			return nullptr;
		}
		node->fields.binding.owner_pid = uint32(owner_pid);
		adapters.Append(adapter);
		adapter->online.store(1, uni::MemoryOrder_Release);
		session->online.store(1, uni::MemoryOrder_Release);
		return adapter;
	}

	void StorageAdapter::DriverExited(stduint pid) {
		for (stduint i = 0; i < sessions.Count(); ++i) {
			if (sessions[i]->owner_pid == pid) {
				sessions[i]->online.store(0, uni::MemoryOrder_Release);
			}
		}
		for (stduint i = 0; i < adapters.Count(); ++i) {
			if (adapters[i]->session->owner_pid != pid) continue;
			adapters[i]->online.store(0, uni::MemoryOrder_Release);
			adapters[i]->cached_block = stduint(-1);
			adapters[i]->media_present.store(0, uni::MemoryOrder_Release);
		}
	}
}
#endif

#if defined(_MCCA)
namespace Devs {
	void NotifyStorageWait(StorageWaitContext& wait) {
		wait.notification.fetch_add(1, uni::MemoryOrder_Release);
		const stduint tid = wait.waiter.load(uni::MemoryOrder_Acquire);
		if (tid != ~stduint(0)) (void)Taskman::UnblockThread(tid, ThreadBlock::BlockReason::BR_Resting);
	}

	void NotifyStorageWait(StorageWaitContext& wait, stduint tid) {
		wait.notification.fetch_add(1, uni::MemoryOrder_Release);
		if (tid != ~stduint(0)) (void)Taskman::UnblockThread(tid, ThreadBlock::BlockReason::BR_Resting);
	}

	StorageWaitStatus WaitStorage(StorageWaitContext& wait, StorageWaitProbe probe, void* context,
		stduint timeout_ticks, stduint poll_limit, bool interrupt_driven) {
		if (!probe || !timeout_ticks || !poll_limit) return StorageWaitStatus::Failed;
		const stduint start_tick = tick;
		stduint polls = 0;
		bool can_sleep = false, can_yield = false;
		#if (_MCCA & 0xFF00) == 0x8600
		auto* thread = Taskman::CurrentTB();
		const stduint tid = Taskman::CurrentTID();
		const bool initial_if = IC.TryMaskInterrupt();
		if (initial_if) IC.enInterrupt(true);
		// Initialization precedes the reserved service tasks; do not start scheduling during that phase.
		can_yield = initial_if && thread && Taskman::Locate(Task_Devsman) && stduint(tick) != 0;
		/* Thread-wake timers run on Devsman. Neither it nor FileSys, which it may synchronously
		   wait for, may sleep on that timer; those service paths must keep polling hardware. */
		can_sleep = can_yield && interrupt_driven && tid != Task_Devsman && tid != Task_FileSys;
		auto release_waiter = [&]() {
			stduint expected = tid;
			// A concurrent probe may have become the IRQ service waiter while this request completed.
			(void)wait.waiter.compare_exchange(expected, ~stduint(0), uni::MemoryOrder_Acq_Rel, uni::MemoryOrder_Acquire);
		};
		#endif
		StorageWaitStatus result = StorageWaitStatus::Pending;
		for (;;) {
			#if (_MCCA & 0xFF00) == 0x8600
			if (can_sleep) wait.waiter.store(tid, uni::MemoryOrder_Release);
			#endif
			const uint32 observed = wait.notification.load(uni::MemoryOrder_Acquire);
			result = probe(context);
			if (result != StorageWaitStatus::Pending) break;
			if (stduint(stduint(tick) - start_tick) >= timeout_ticks) {
				result = StorageWaitStatus::TimedOut;
				break;
			}
			++polls;
			if (!can_sleep && polls >= poll_limit) {
				result = StorageWaitStatus::TimedOut;
				break;
			}
			#if (_MCCA & 0xFF00) == 0x8600
			if (can_sleep && polls >= 64) {
				const bool old_if = IC.TryMaskInterrupt();
				if (!old_if) {
					can_sleep = can_yield = false;
					release_waiter();
					continue;
				}
				thread->Block(ThreadBlock::BlockReason::BR_Resting);
				StorageTimer wake_timer;
				/* Recheck after Block: an IRQ between the probe and Block must not leave us asleep.
				   A one-tick wake also covers IRQ service-waiter turnover between concurrent requests. */
				if (wait.notification.load(uni::MemoryOrder_Acquire) != observed) {
					thread->Unblock(ThreadBlock::BlockReason::BR_Resting);
				}
				else if (!wake_timer.ArmWake(1, tid)) {
					thread->Unblock(ThreadBlock::BlockReason::BR_Resting);
					can_sleep = false;
					release_waiter();
				}
				if (old_if) IC.enInterrupt(true);
				Taskman::Schedule(true);
				wake_timer.Cancel();
			}
			else if (can_yield && !(polls & 63u)) Taskman::Schedule(true);
			else _ASM volatile("pause" ::: "memory");
			#else
			_ASM volatile("" ::: "memory");
			#endif
		}
		#if (_MCCA & 0xFF00) == 0x8600
		release_waiter();
		#endif
		return result;
	}

	#if (_MCCA & 0xFF00) == 0x8600
	bool StorageTimer::ArmWake(stduint delay, stduint tid) {
		if (!delay || tid == ~stduint(0)) return false;
		Cancel();
		target = tid;
		callback = nullptr;
		deadline = stduint(tick) + delay;
		armed = Systimex::AppendThreadWake(delay, tid);
		return armed;
	}

	bool StorageTimer::ArmService(stduint delay, stduint context, _tocall_ft handler) {
		if (!handler) return false;
		if (!delay) delay = 1;
		const stduint next = stduint(tick) + delay;
		if (armed && callback == handler && target == context && stdsint(next - deadline) >= 0) return true;
		Cancel();
		target = context;
		callback = handler;
		deadline = next;
		armed = Systimex::AppendDeferredCallback(delay, context, handler);
		return armed;
	}

	bool StorageTimer::Consume(stduint context, _tocall_ft handler) {
		if (!armed || !callback || callback != handler || target != context ||
			stdsint(stduint(tick) - deadline) < 0) return false;
		Cancel();
		return true;
	}

	void StorageTimer::Cancel() {
		if (!armed) return;
		if (callback) Systimex::CancelDeferredCallback(target, callback);
		else Systimex::CancelThreadWake(target);
		armed = false;
	}

	namespace {
		bool StorageDmaRangeFits(uint64 address, stduint bytes, const StorageDmaRequirements& requirements) {
			if (!bytes || address > requirements.address_limit || uint64(bytes - 1) > requirements.address_limit - address) return false;
			const uint64 alignment = uint64(1) << requirements.alignment_bits;
			if (address & (alignment - 1)) return false;
			return !requirements.boundary ||
				(address / requirements.boundary == (address + bytes - 1) / requirements.boundary);
		}

		void FreeStorageDma(void* buffer, stduint bytes) {
			if (dma_low_pool.IsInRange(buffer)) dma_low_pool.Deallocate(buffer, bytes);
			else mempool.deallocate(buffer, bytes);
		}
	}

	StorageDmaBuffer::~StorageDmaBuffer() {
		if (!Release()) plogwarn("[Storage] retaining DMA buffer %p bytes=%u", virt, size);
	}

	bool StorageDmaBuffer::Ensure(DeviceNode* node, stduint bytes, const StorageDmaRequirements& requirements) {
		if (!node || !bytes || in_flight || requirements.alignment_bits >= sizeof(stduint) * 8 ||
			bytes > ~stduint(0) - (PAGE_SIZE - 1) ||
			(requirements.boundary && (requirements.boundary & (requirements.boundary - 1)))) return false;
		const stduint allocation_bytes = (bytes + PAGE_SIZE - 1) & ~(stduint(PAGE_SIZE) - 1);
		if (uint64(allocation_bytes - 1) > requirements.address_limit ||
			(requirements.boundary && allocation_bytes > requirements.boundary)) return false;
		if (virt && device == node && device_address == uint64(_IMM(virt)) && size >= allocation_bytes &&
			StorageDmaRangeFits(device_address, size, requirements)) return true;
		if (!Release()) return false;
		const byte alignment_bits = maxof(byte(12), requirements.alignment_bits);
		byte* buffer = requirements.prefer_low_pool && alignment_bits == 12 && allocation_bytes <= 0x10000
			? static_cast<byte*>(dma_low_pool.Allocate(allocation_bytes)) : nullptr;
		if (!buffer) buffer = static_cast<byte*>(mempool.allocate(allocation_bytes, alignment_bits));
		if (!buffer) return false;
		if (!StorageDmaRangeFits(uint64(_IMM(buffer)), allocation_bytes, requirements)) {
			FreeStorageDma(buffer, allocation_bytes);
			return false;
		}
		Iova::DmaMapping mapping{};
		if (!Iova::MapDeviceDma(node, _IMM(buffer), allocation_bytes, Iova::DmaMappingFlag::Identity, mapping)) {
			FreeStorageDma(buffer, allocation_bytes);
			return false;
		}
		device = node;
		virt = buffer;
		device_address = mapping.device_address;
		size = allocation_bytes;
		translated = mapping.translated;
		if (device_address != uint64(_IMM(virt)) || !StorageDmaRangeFits(device_address, size, requirements)) {
			// Retain the page if invalidation fails: freeing a device-reachable mapping would be unsafe.
			if (!Release()) plogwarn("[Storage] retaining DMA mapping after failed invalidation");
			return false;
		}
		MemSet(virt, 0, size);
		SyncForDevice();
		return true;
	}

	bool StorageDmaBuffer::Release() {
		if (!virt) return true;
		if (in_flight) return false;
		if (translated && !Iova::UnmapDeviceDma(device, device_address, size)) return false;
		FreeStorageDma(virt, size);
		device = nullptr;
		virt = nullptr;
		device_address = 0;
		size = 0;
		translated = false;
		return true;
	}

	void StorageDmaBuffer::MarkInFlight() {
		SyncForDevice();
		in_flight = true;
	}

	void StorageDmaBuffer::MarkQuiesced() {
		in_flight = false;
		SyncForCpu();
	}

	// x86/x64 DMA is cache-coherent; ownership barriers still order descriptors, doorbells and payload accesses.
	void StorageDmaBuffer::SyncForDevice() const { __sync_synchronize(); }
	void StorageDmaBuffer::SyncForCpu() const { __sync_synchronize(); }
	#endif
}
#endif
