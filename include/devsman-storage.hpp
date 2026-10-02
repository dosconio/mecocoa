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

#endif
