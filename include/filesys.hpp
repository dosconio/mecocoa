#ifndef FILESYS_HPP_
#define FILESYS_HPP_

#include "../include/mecocoa.hpp"

// To resolve RMOD_LIST redefining issue, we ensure standard include guards
// And we only include traits from cpp/trait
#include <cpp/System/Network/Layer/Application/Socket.hpp>
#include <cpp/System/Network/Layer/Transport/TCP.hpp>
#include <cpp/System/Network/Layer/Transport/UDP.hpp>
#include <cpp/ISO_IEC_STD/memory>
#include <cpp/System/Filesys.hpp>
#include <cpp/trait/StorageTrait.hpp>

struct DeviceNode;
struct ProcessCredentials;
struct file_status_t;
struct file_permission_request_t;

// VFS Layer for Mecocoa
// Unix-like Virtual File System
// Unlike Linux, Mecocoa VFS Root is fixed

namespace uni {

struct vfs_inode;
struct vfs_super_block;
struct vfs_dentry;

enum class VFSMountOwnership : byte {
	Borrowed,
	Filesystem,
	FilesystemAndStorage,
};

struct VFSMountState {
	FilesysTrait* filesystem = nullptr;
	StorageTrait* storage = nullptr;
	VFSMountOwnership ownership = VFSMountOwnership::Borrowed;
	bool detached = false;
	// Immutable mount restrictions remain available after namespace detachment.
	bool read_only = false;
	bool no_execute = false;

	VFSMountState(FilesysTrait* filesystem, VFSMountOwnership ownership);
	VFSMountState(const VFSMountState&) = delete;
	VFSMountState& operator=(const VFSMountState&) = delete;
	~VFSMountState();
};

// Represents a registered file system type
struct file_system_type {
	const char* name = nullptr;
	// Returns a new initialized FilesysTrait instance if the storage contains this file system.
	FilesysTrait* (*probe)(StorageTrait& storage, stduint partition_dev) = nullptr;
	
	file_system_type* next = nullptr;
};

// Synthetic ownership and permissions for filesystems without per-file metadata.
struct VFSMountPermissions {
	uint32 uid = 0;
	uint32 gid = 0;
	stduint file_mode = 0644;
	stduint directory_mode = 0755;
	// Mount-wide write restriction, including for UID 0.
	bool read_only = false;
	bool no_execute = false;
};

// Represents an active mounted file system
struct vfs_super_block {
	FilesysTrait* fs = nullptr;             // The backend file system operations
	SharedPtr<VFSMountState> mount_state;
	vfs_dentry* s_root = nullptr;           // Root dentry of this mount
	file_system_type* type = nullptr;       // Pointer to the FS type
	stduint device_id = 0;            // Underlying device id
	DeviceNode* source_device_node = nullptr; // Source storage/partition node in device tree
	VFSMountPermissions permissions;
	
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
	SharedPtr<VFSMountState> mount_state;
	~vfs_inode();
	
	uint64 identity = 0;               // Stable for this inode object's lifetime; never path-derived
	uint64 content_version = 0;        // Read and advanced while holding vfs_lock
	stduint i_no = 0;                 // Inode number
	stduint i_mode = 0;               // File mode (type and permissions)
	uint32 i_uid = 0;
	uint32 i_gid = 0;
	stduint i_size = 0;               // File size in bytes
	bool is_unlinked = false;          // Backend data remains until the last inode reference is released
	
	void (*destroy_handler)(void*) = nullptr; // Optional inode-owned backend handle cleanup.
	void* internal_handler = nullptr;       // e.g. FAT_FileHandle* or internal inode* from specific FS
};

#define VFS_MAX_FILENAME 64

// Directory entry caching and tree structure
struct vfs_dentry {
	vfs_dentry* d_parent = nullptr;         // Parent directory
	vfs_dentry* d_first_child = nullptr;    // First child directory/file
	vfs_dentry* d_next_sibling = nullptr;   // Next sibling in the same directory

	SharedPtr<vfs_inode> d_inode;            // Namespace ownership of the associated inode
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
	stduint active_io = 0;            // ReadPipe/WritePipe frames currently using this channel
	bool closing = false;             // Last fd closed; free after active_io drains
	Spinlock lock;                    // Protects buffer state and wait lists; must not sleep
	::ThreadBlock* read_wait_head = nullptr;
	::ThreadBlock* write_wait_head = nullptr;
	PipeChannel* global_next = nullptr;
};

// Opened file representation (File descriptor struct)
struct vfs_file {
	SharedPtr<vfs_inode> f_inode;
	stduint f_pos = 0;                // Current read/write position
	stduint f_mode = 0;               // Open mode (R/W/A etc)
	FilesysEnumState f_enum_state = {};
};

struct SocketHandle {
	Network::SocketDomain domain = Network::SocketDomain::Unspec;
	Network::SocketType type = Network::SocketType::Datagram;
	Network::SocketProtocol protocol = Network::SocketProtocol::Default;
	uint16 flags = 0;
	int last_error = 0;
	stduint receive_timeout_ms = 0;
	stduint send_timeout_ms = 0;
	stduint keepalive_idle_seconds = 30;
	stduint keepalive_interval_seconds = 1;
	stduint keepalive_probe_limit = 3;
	stduint udp_inbox_id = stduint(-1);
	bool is_bound = false;
	bool is_connected = false;
	bool is_connecting = false;
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
			DeviceNode* source_device_node = nullptr,
			VFSMountPermissions permissions = {}) -> file_system_type*;
	// Detach a filesystem from the VFS tree and release resources
	static bool Unmount(const char* target_path);
	// General path resolution: find dentry for a given path
	static vfs_dentry* Index(const char* pathname, vfs_dentry* base = nullptr);
	// Update CWD, identity and storage ownership together after checking search access.
	static int ChangeDirectory(const char* pathname, vfs_dentry*& cwd, uint64& cwd_identity, SharedPtr<vfs_inode>& cwd_inode, const ProcessCredentials& credentials);
	// Build the path under vfs_lock. A detached CWD returns -ESTALE.
	static int GetWorkingDirectory(vfs_dentry* cwd, uint64 cwd_identity, String& path);
	// Path lookup checks directory search access and snapshots metadata under the same VFS lock.
	static int StatPath(const char* path, const ProcessCredentials& credentials,
		vfs_dentry* cwd, uint64 cwd_identity, file_status_t& result);
	// The caller retains the inode independently of the descriptor and namespace lifetime.
	static int StatInode(const SharedPtr<vfs_inode>& inode, file_status_t& result);
	static int SetPermissionsPath(const char* path, const ProcessCredentials& credentials, vfs_dentry* cwd, uint64 cwd_identity, const file_permission_request_t& request, bool mode_change);
	static int SetPermissionsInode(const SharedPtr<vfs_inode>& inode, const ProcessCredentials& credentials, const file_permission_request_t& request, bool mode_change);

public:
	// Explicitly mount an instantiated FS to a path (used by DevFs and RootFs)
	// Non-borrowed ownership is transferred on entry, including failure paths.
	static bool MountFilesys(FilesysTrait* fs, file_system_type* type, const char* target_path,
		DeviceNode* source_device_node = nullptr, stduint device_id = 0,
		VFSMountOwnership ownership = VFSMountOwnership::Borrowed,
		VFSMountPermissions permissions = {});

public:
	static int CreatePipe(vfs_file** out_reader, vfs_file** out_writer);
	static PipeChannel* AcquirePipeChannel(vfs_file* file);
	static void ReleasePipeChannel(PipeChannel* chan);
	static int ReadPipe(vfs_file* file, void* buf, stduint count);
	static int ReadPipe(PipeChannel* chan, void* buf, stduint count);
	static int WritePipe(vfs_file* file, const void* buf, stduint count);
	static int WritePipe(PipeChannel* chan, const void* buf, stduint count);
	static void CancelPipeWait(::ThreadBlock* th, bool release_io = false);
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
	static int ShutdownSocket(vfs_file* file, stduint how);
	static int CloseSocket(vfs_file* file);

public:
	// User-originated opens supply the process credentials and saved CWD identity.
	// Trusted kernel callers may pass null credentials. Returns zero or a negative errno.
	static int Open(const char* pathname, int flags, vfs_file** out_file, vfs_dentry* base = nullptr,
		const ProcessCredentials* credentials = nullptr, uint64 cwd_identity = 0, const FilesysCreateAttributes* creation = nullptr);
	// Authorize execution and retain the file; the ELF loader parses and installs the authorized image.
	static int OpenExecutable(const char* pathname, vfs_file** out_file,
		const ProcessCredentials& credentials, vfs_dentry* base = nullptr, uint64 cwd_identity = 0);
	// POSIX mmap() requires readable files and writable access for shared writes (https://pubs.opengroup.org/onlinepubs/9799919799/functions/mmap.html).
	static int GetMappingPermissions(vfs_file* file, int open_mode, bool shared, stduint& allowed);
	static int Read(vfs_file* file, void* buf, stduint count);
	static int Write(vfs_file* file, const void* buf, stduint count);
	static bool Truncate(vfs_file* file, stduint size);
	#ifdef _FILESYS_DATETIME
	static bool GetInfo(vfs_file* file, FilesysFileInfo& info);
	#endif
	static stdsint Ctrl(vfs_file* file, stduint cmd, void* args);
	static int Close(vfs_file* file);
	// Listing names requires a directory opened for reading. Path traversal uses search permission instead.
	static int Enumer(vfs_file* file, void* buf, stduint count, ProcessBlock* pb);
	// Unlink a regular file; user requests require parent write/search access and sticky ownership checks.
	static bool Remove(const char* pathname, vfs_dentry* base = nullptr,
		const ProcessCredentials* credentials = nullptr, uint64 cwd_identity = 0);
	// Remove only empty directories, protecting mount roots and enforcing parent and sticky permissions.
	static int RemoveDirectory(const char* pathname, vfs_dentry* base, const ProcessCredentials* credentials, uint64 cwd_identity);
	// Drop a retained inode under VFS serialization, including backend storage reclamation.
	static void ReleaseInode(SharedPtr<vfs_inode>& inode);
	// Rename with replacement; check both parents and sticky ownership, retaining the replaced inode until its last reference; return zero or a negative errno.
	static int Rename(const char* source, const char* destination, vfs_dentry* base = nullptr, const ProcessCredentials* credentials = nullptr, uint64 cwd_identity = 0);
	// Retain an inode only when the weak identity entry still refers to a live object.
	static SharedPtr<vfs_inode> FindInode(uint64 identity);
	//
	static String getAbsolutePath(vfs_dentry* dentry);
	static vfs_dentry* getRoot();
	static DeviceNode* GetMountSourceNode(vfs_dentry* dentry);
	static DeviceNode* GetMountSourceNode(const char* pathname, vfs_dentry* base = nullptr);
	static stduint CountMountsForSourceNode(DeviceNode* source_device_node);
	static String GetFirstMountPathForSourceNode(DeviceNode* source_device_node);

public:
	static String system_virtual_root_path;// e.g. /mnt/ide2.0
	static String system_runtime_root_path;// e.g. /mnt/ide2.0/x86
	static const String& GetSystemVirtualRootPath() { return system_virtual_root_path; }
	static const String& GetSystemRuntimeRootPath() { return system_runtime_root_path; }
};


// -------------------------------------------------------------
// DevFs / Pure Memory Device File System
// -------------------------------------------------------------

class DevFs : public FilesysTrait {
public:
	virtual bool permit(void* handler, FilesysPermissionResult& result, bool set_or_get = false) override;
	virtual bool makefs(rostr vol_label, void* moreinfo = 0) override;
	virtual bool loadfs(void* moreinfo = 0) override;
	virtual bool create(rostr fullpath, stduint flags, FilesysCreateParameter& parameter) override;
	virtual bool remove(rostr pathname) override;
	virtual void* search(rostr fullpath, FilesysSearchArgs* args) override;
	virtual bool proper(void* handler, stduint cmd, const void* moreinfo = 0) override;
	virtual bool enumer(void* dir_handler, _tocall_ft _fn, FilesysEnumState* state = nullptr) override;
	virtual stduint readfl(void* fil_handler, Slice file_slice, byte* dst) override;
	virtual stduint writfl(void* fil_handler, Slice file_slice, const byte* src) override;
public:
	static DeviceNode* GetDeviceNode(void* handler);
	static int allocate_tty_id();
	static void free_tty_id(int id);
};

extern DevFs global_devfs;

}

#endif
