#ifndef FILESYS_HPP_
#define FILESYS_HPP_

#include "../include/mecocoa.hpp"

// To resolve RMOD_LIST redefining issue, we ensure standard include guards
// And we only include traits from cpp/trait
#include <cpp/trait/FilesysTrait.hpp>
#include <cpp/trait/StorageTrait.hpp>

// VFS Layer for Mecocoa
// Linux-like Virtual File System

namespace uni {

struct vfs_inode;
struct vfs_super_block;
struct vfs_dentry;

// Represents a registered file system type
struct file_system_type {
	const char* name;
	// Returns a new initialized FilesysTrait instance if the storage contains this file system.
	FilesysTrait* (*probe)(StorageTrait& storage, stduint partition_dev);
	
	file_system_type* next;
};

// Represents an active mounted file system
struct vfs_super_block {
	FilesysTrait* fs;             // The backend file system operations
	vfs_dentry* s_root;           // Root dentry of this mount
	file_system_type* type;       // Pointer to the FS type
	stduint device_id;            // Underlying device id
	
	vfs_super_block* next;
};

#ifndef I_TYPE_MASK
#define I_TYPE_MASK     0170000
#define I_REGULAR       0100000
#define I_BLOCK_SPECIAL 0060000
#define I_DIRECTORY     0040000
#define I_CHAR_SPECIAL  0020000
#define I_NAMED_PIPE	0010000
#endif

// In-memory node structure
struct vfs_inode {
	vfs_super_block* i_sb;
	
	stduint i_no;                 // Inode number
	stduint i_mode;               // File mode (type and permissions)
	stduint i_size;               // File size in bytes
	
	void* internal_handler;       // e.g. FAT_FileHandle* or internal inode* from specific FS
	
	stduint ref_count;
};

#define VFS_MAX_FILENAME 64

// Directory entry caching and tree structure
struct vfs_dentry {
	vfs_dentry* d_parent;         // Parent directory
	vfs_dentry* d_first_child;    // First child directory/file
	vfs_dentry* d_next_sibling;   // Next sibling in the same directory
	
	vfs_inode* d_inode;           // The associated inode (if loaded)
	char d_name[VFS_MAX_FILENAME];// The name of this entry
	
	// Mount point: If a filesystem is mounted ON THIS dentry,
	// d_mounts points to the root dentry of that mounted filesystem.
	vfs_dentry* d_mounts;
};

// Opened file representation (File descriptor struct)
struct vfs_file {
	vfs_dentry* f_dentry;
	vfs_inode* f_inode;
	stduint f_pos;                // Current read/write position
	stduint f_mode;               // Open mode (R/W/A etc)
};

// Virtual File System
class Filesys {
public:
	//
	static void Initialize();
	// General Path creation (for files)
	static vfs_dentry* Create(const char* pathname, stduint mode);
	// Display the hierarchy of the virtual file system (VFS tree)
	static void Tree();
	// Register a file system driver
	static void Register(file_system_type* fs_type);
	// Probe a partition and if successful, mount it at the target path
	static bool Mount(StorageTrait& storage, stduint dev, const char* target_path);
	// General path resolution: find dentry for a given path
	static vfs_dentry* Index(const char* pathname);

	friend void devfs_register_and_mount();
protected:
	// Explicitly mount an instantiated FS to a path (used by DevFs and RootFs)
	static bool MountFilesys(FilesysTrait* fs, file_system_type* type, const char* target_path);

public:
	static int Open(const char* pathname, int flags, vfs_file** out_file);
	static int Read(vfs_file* file, void* buf, stduint count);
	static int Write(vfs_file* file, const void* buf, stduint count);
	static int Close(vfs_file* file);
	static int Remove(const char* pathname);
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
	virtual bool enumer(void* dir_handler, _tocall_ft _fn) override;
	virtual stduint readfl(void* fil_handler, Slice file_slice, byte* dst) override;
	virtual stduint writfl(void* fil_handler, Slice file_slice, const byte* src) override;
};

extern DevFs global_devfs;
void devfs_register_and_mount();

}

#endif
