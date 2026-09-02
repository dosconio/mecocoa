#ifndef FILESYS_HPP_
#define FILESYS_HPP_

#include "../include/mecocoa.hpp"

// To resolve RMOD_LIST redefining issue, we ensure standard include guards
// And we only include traits from cpp/trait
#include <cpp/System/Network/Layer/Application/Socket.hpp>
#include <cpp/System/Network/Layer/Transport/TCP.hpp>
#include <cpp/System/Network/Layer/Transport/UDP.hpp>
#include <cpp/trait/FilesysTrait.hpp>
#include <cpp/trait/StorageTrait.hpp>

struct DeviceNode;

// VFS Layer for Mecocoa
// Linux-like Virtual File System

namespace uni {

struct vfs_inode;
struct vfs_super_block;
struct vfs_dentry;

// Represents a registered file system type
struct file_system_type {
	const char* name = nullptr;
	// Returns a new initialized FilesysTrait instance if the storage contains this file system.
	FilesysTrait* (*probe)(StorageTrait& storage, stduint partition_dev) = nullptr;
	
	file_system_type* next = nullptr;
};

// Represents an active mounted file system
struct vfs_super_block {
	FilesysTrait* fs = nullptr;             // The backend file system operations
	vfs_dentry* s_root = nullptr;           // Root dentry of this mount
	file_system_type* type = nullptr;       // Pointer to the FS type
	stduint device_id = 0;            // Underlying device id
	DeviceNode* source_device_node = nullptr; // Source storage/partition node in device tree
	
	vfs_super_block* next = nullptr;
};

#ifndef I_TYPE_MASK
#define I_TYPE_MASK     0170000
#define I_REGULAR       0100000
#define I_BLOCK_SPECIAL 0060000
#define I_DIRECTORY     0040000
#define I_CHAR_SPECIAL  0020000
#define I_NAMED_PIPE	0010000
#define I_LNK           0120000  //{TODO} (SymLink)
#define I_SOCK          0140000  //{TODO} (Socket)
#endif

// In-memory node structure
struct vfs_inode {
	vfs_super_block* i_sb = nullptr;
	
	stduint i_no = 0;                 // Inode number
	stduint i_mode = 0;               // File mode (type and permissions)
	stduint i_size = 0;               // File size in bytes
	
	void* internal_handler = nullptr;       // e.g. FAT_FileHandle* or internal inode* from specific FS
	
	stduint ref_count = 0;
};

#define VFS_MAX_FILENAME 64

// Directory entry caching and tree structure
struct vfs_dentry {
	vfs_dentry* d_parent = nullptr;         // Parent directory
	vfs_dentry* d_first_child = nullptr;    // First child directory/file
	vfs_dentry* d_next_sibling = nullptr;   // Next sibling in the same directory

	vfs_inode* d_inode = nullptr;           // The associated inode (if loaded)
	char d_name[VFS_MAX_FILENAME] = {};     // The name of this entry

	// Mount point: If a filesystem is mounted ON THIS dentry,
	// d_mounts points to the root dentry of that mounted filesystem.
	vfs_dentry* d_mounts = nullptr;

	// Reverse Mount: If this is the root dentry of a mounted filesystem,
	// d_mounted_on points to the dentry in the parent FS where it was mounted.
	vfs_dentry* d_mounted_on = nullptr;
};

// Pipe channel representation for anonymous memory pipelines
struct PipeChannel {
	QueueLimited buffer;          // Circular ring buffer
	stduint reader_count = 0;         // Reader descriptors counter
	stduint writer_count = 0;         // Writer descriptors counter
	Mutex lock;                   // Mutex to protect concurrent operations
	Queue<::ThreadBlock*> rq;     // Read waiting queue
	Queue<::ThreadBlock*> wq;     // Write waiting queue
};

// Opened file representation (File descriptor struct)
struct vfs_file {
	vfs_dentry* f_dentry = nullptr;
	vfs_inode* f_inode = nullptr;
	stduint f_pos = 0;                // Current read/write position
	stduint f_mode = 0;               // Open mode (R/W/A etc)
	FilesysEnumState f_enum_state = {};
};

struct SocketHandle {
	Network::SocketDomain domain = Network::SocketDomain::Unspec;
	Network::SocketType type = Network::SocketType::Datagram;
	Network::SocketProtocol protocol = Network::SocketProtocol::Default;
	uint16 flags = 0;
	stduint udp_inbox_id = stduint(-1);
	bool is_bound = false;
	bool is_connected = false;
	bool is_listening = false;
	Network::SocketEndpointIPv4 local_ipv4 = {};
	Network::SocketEndpointIPv4 remote_ipv4 = {};
};

// Virtual File System
class Filesys {
public:
	//
	static void Initialize();
	// General Path creation (for files)
	static vfs_dentry* Create(const char* pathname, stduint mode, vfs_dentry* base = nullptr);
	// Display the hierarchy of the virtual file system (VFS tree)
	static void Tree(uni::OstreamTrait& os, bool mount_expand);
	// Register a file system driver
	static void Register(file_system_type* fs_type);
	// Probe a partition and if successful, mount it at the target path
	static auto
		Mount(StorageTrait& storage, stduint dev, const char* target_path,
			DeviceNode* source_device_node = nullptr) -> file_system_type*;
	// Detach a filesystem from the VFS tree and release resources
	static bool Unmount(const char* target_path);
	// General path resolution: find dentry for a given path
	static vfs_dentry* Index(const char* pathname, vfs_dentry* base = nullptr);

public:
	// Explicitly mount an instantiated FS to a path (used by DevFs and RootFs)
	static bool MountFilesys(FilesysTrait* fs, file_system_type* type, const char* target_path,
		DeviceNode* source_device_node = nullptr, stduint device_id = 0);

public:
	static int CreatePipe(vfs_file** out_reader, vfs_file** out_writer);
	static int ReadPipe(vfs_file* file, void* buf, stduint count);
	static int WritePipe(vfs_file* file, const void* buf, stduint count);
	static int ClosePipe(vfs_file* file);
	static int CreateSocket(vfs_file** out_file, Network::SocketDomain domain,
		Network::SocketType type, Network::SocketProtocol protocol);
	static SocketHandle* GetSocket(vfs_file* file);
	static int BindSocket(vfs_file* file, const Network::SocketAddress& address);
	static int ConnectSocket(vfs_file* file, const Network::SocketAddress& address);
	static int ListenSocket(vfs_file* file, stduint backlog);
	static int AcceptSocket(vfs_file* file, vfs_file** out_file,
		Network::SocketAddress* address, stduint* address_length);
	static int SendSocket(vfs_file* file, const void* payload, stduint length, const Network::SocketAddress* address);
	static int RecvSocket(vfs_file* file, void* payload, stduint capacity,
		Network::SocketAddress* address, stduint* address_length, stduint flags = 0);
	static int Poll(vfs_file* file, stduint events, stduint* revents);
	static int GetSocketAddress(vfs_file* file, bool peer,
		Network::SocketAddress* address, stduint* address_length);
	static int SetSocketOption(vfs_file* file, stduint level, stduint option_name, int value);
	static int GetSocketOption(vfs_file* file, stduint level, stduint option_name, int* value);
	static int CloseSocket(vfs_file* file);

public:
	static int Open(const char* pathname, int flags, vfs_file** out_file, vfs_dentry* base = nullptr);
	static int Read(vfs_file* file, void* buf, stduint count);
	static int Write(vfs_file* file, const void* buf, stduint count);
	static int Close(vfs_file* file);
	static int Enumer(vfs_file* file, void* buf, stduint count, ProcessBlock* pb);
	static bool Remove(const char* pathname, vfs_dentry* base = nullptr);
	//
	static String getAbsolutePath(vfs_dentry* dentry);
	static vfs_dentry* getRoot();
	static DeviceNode* GetMountSourceNode(vfs_dentry* dentry);
	static DeviceNode* GetMountSourceNode(const char* pathname, vfs_dentry* base = nullptr);
	static stduint CountMountsForSourceNode(DeviceNode* source_device_node);
	static String GetFirstMountPathForSourceNode(DeviceNode* source_device_node);
};


// -------------------------------------------------------------
// DevFs / Pure Memory Device File System
// -------------------------------------------------------------

class DevFs : public FilesysTrait {
public:
	virtual bool makefs(rostr vol_label, void* moreinfo = 0) override;
	virtual bool loadfs(void* moreinfo = 0) override;
	virtual bool create(rostr fullpath, stduint flags, void* exinfo, rostr linkdest = 0) override;
	virtual bool remove(rostr pathname) override;
	virtual void* search(rostr fullpath, FilesysSearchArgs* args) override;
	virtual bool proper(void* handler, stduint cmd, const void* moreinfo = 0) override;
	virtual bool enumer(void* dir_handler, _tocall_ft _fn, FilesysEnumState* state = nullptr) override;
	virtual stduint readfl(void* fil_handler, Slice file_slice, byte* dst) override;
	virtual stduint writfl(void* fil_handler, Slice file_slice, const byte* src) override;
public:
	static int allocate_tty_id();
	static void free_tty_id(int id);
};

extern DevFs global_devfs;

}

#endif
