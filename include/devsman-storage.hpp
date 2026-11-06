#ifndef DEVSMAN_STORAGE_HPP_
#define DEVSMAN_STORAGE_HPP_

#include <c/stdinc.h>

enum class StorageDriverMsg : stduint {
	Attach = 0x31000,
	Read,
	Write,
	Ready,
	Data,
	Complete,
	Cancel,
};

enum StorageDriverFlag : uint32 {
	StorageDriverFlag_Readable = 1 << 0,
	StorageDriverFlag_Writable = 1 << 1,
	StorageDriverFlag_MediaPresent = 1 << 2,
};

static constexpr uint32 StorageDriverProtocolVersion = 1;
static constexpr uint32 StorageDriverNameCapacity = 24;
static constexpr uint32 StorageDriverMaxBlockSize = 4096;

struct StorageDriverInfo {
	uint32 version = StorageDriverProtocolVersion;
	uint32 dev_handle = 0;
	uint32 unit = 0;
	uint32 block_size = 0;
	uint32 block_count = 0;
	uint32 flags = 0;
	uint32 slice_type = 0;
	char name[StorageDriverNameCapacity] = {};
};

struct StorageDriverRequest {
	uint32 version = StorageDriverProtocolVersion;
	uint32 unit = 0;
	uint32 block = 0;
};

struct StorageDriverReply {
	uint32 version = StorageDriverProtocolVersion;
	int32 status = -1;
	uint32 bytes = 0;
};

static_assert(sizeof(StorageDriverInfo) == 52, "StorageDriverInfo IPC layout changed");
static_assert(sizeof(StorageDriverRequest) == 12, "StorageDriverRequest IPC layout changed");
static_assert(sizeof(StorageDriverReply) == 12, "StorageDriverReply IPC layout changed");

#if defined(_MCCA) && ((_MCCA & 0xFF00) == 0x8600)
#include <cpp/trait/StorageTrait.hpp>
#include <cpp/atomic>
struct DeviceNode;
namespace Powercall {
	DeviceNode* ResolveOwnedDeviceHandle(stduint sender_tid, stduint handle, stduint* owner_pid);
	struct StorageSession;
	class StorageAdapter : public uni::StorageTrait {
		StorageSession* session;
		DeviceNode* node;
		uint32 unit;
		uint32 block_count;
		uint32 slice_type;
		uni::Atomic<byte> online = 0;
		uni::Atomic<byte> media_present = 0;
		stduint cached_block = stduint(-1);
		bool ReadBlock(stduint block, void* dest);
	public:
		StorageAdapter(StorageSession* session, DeviceNode* node, const StorageDriverInfo& info);
		~StorageAdapter() override;
		bool Read(stduint block, void* dest, stduint times = 1) override;
		bool Write(stduint block, const void* source, stduint times = 1) override;
		stduint getUnits() override;
		uni::PartitionSlice getSlice(stduint dev) override;
		int operator[](uint64 offset) override;
		DeviceNode* GetNode() const { return node; }
		bool HasMedia() const;
		static StorageAdapter* Register(stduint sender_tid, const StorageDriverInfo& info);
		static void DriverExited(stduint pid);
	};
}
#endif

#if defined(_MCCA)
#include <cpp/atomic>
namespace Devs {
	enum class StorageWaitStatus : byte { Pending, Completed, Failed, TimedOut };
	struct StorageWaitContext {
		// IRQs wake one service waiter; concurrent requests receive targeted completion notifications.
		uni::Atomic<stduint> waiter{~stduint(0)};
		uni::Atomic<uint32> notification{0};
	};
	using StorageWaitProbe = StorageWaitStatus (*)(void* context);
	// Probes run in thread context; drivers serialize shared queue/hardware access inside the probe.
	StorageWaitStatus WaitStorage(StorageWaitContext& wait, StorageWaitProbe probe, void* context,
		stduint timeout_ticks, stduint poll_limit, bool interrupt_driven);
	// Call only after releasing any driver spinlock; waking a thread takes scheduler_lock.
	void NotifyStorageWait(StorageWaitContext& wait);
	void NotifyStorageWait(StorageWaitContext& wait, stduint tid);

	#if (_MCCA & 0xFF00) == 0x8600
	/* The owner serializes service timers. A deferred handler must call Consume() under that
	   same lock: cancellation cannot retract a callback already dispatched on another CPU. */
	class StorageTimer {
		bool armed = false;
		stduint deadline = 0, target = 0;
		_tocall_ft callback = nullptr;
	public:
		StorageTimer() = default;
		StorageTimer(const StorageTimer&) = delete;
		StorageTimer& operator=(const StorageTimer&) = delete;
		bool ArmWake(stduint delay, stduint tid);
		bool ArmService(stduint delay, stduint context, _tocall_ft handler);
		bool Consume(stduint context, _tocall_ft handler);
		void Cancel();
	};

	struct StorageDmaRequirements {
		byte alignment_bits = 12;
		uint64 address_limit = ~uint64(0);
		stduint boundary = 0;
		bool prefer_low_pool = false;
	};

	// Page-owned, identity-mapped DMA storage; drivers serialize access and certify hardware quiescence.
	class StorageDmaBuffer {
		DeviceNode* device = nullptr;
		bool translated = false, in_flight = false;
	public:
		byte* virt = nullptr;
		uint64 device_address = 0;
		stduint size = 0;
		StorageDmaBuffer() = default;
		~StorageDmaBuffer();
		StorageDmaBuffer(const StorageDmaBuffer&) = delete;
		StorageDmaBuffer& operator=(const StorageDmaBuffer&) = delete;
		bool Ensure(DeviceNode* node, stduint bytes, const StorageDmaRequirements& requirements = {});
		// Active hardware or failed IOMMU invalidation leaves both allocation and mapping retained.
		bool Release();
		void MarkInFlight();
		void MarkQuiesced();
		void SyncForDevice() const;
		void SyncForCpu() const;
	};
	#endif
}
#endif

#endif
