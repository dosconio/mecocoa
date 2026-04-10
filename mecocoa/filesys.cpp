// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <cpp/trait/StorageTrait.hpp>
#include "../include/filesys.hpp"
#include "../include/fileman.hpp" // for DEV_TTY etc
// VFS and DevFs Implementation

namespace uni {

static file_system_type* registered_filesystems = nullptr;
static vfs_super_block* super_blocks = nullptr;
static vfs_dentry* vfs_root = nullptr; // Global root directory

// Allocators
static vfs_dentry* alloc_dentry(vfs_dentry* parent, const char* name) {
	vfs_dentry* dentry = new vfs_dentry();
	MemSet(dentry, 0, sizeof(vfs_dentry));
	if (name) {
		StrCopy(dentry->d_name, name);
	}
	dentry->d_parent = parent;
	
	// add to parent's child list
	if (parent) {
		if (!parent->d_first_child) {
			parent->d_first_child = dentry;
		} else {
			vfs_dentry* p = parent->d_first_child;
			while(p->d_next_sibling) p = p->d_next_sibling;
			p->d_next_sibling = dentry;
		}
	}
	return dentry;
}

static vfs_inode* alloc_inode(vfs_super_block* sb) {
	vfs_inode* inode = new vfs_inode();
	MemSet(inode, 0, sizeof(vfs_inode));
	inode->i_sb = sb;
	inode->ref_count = 1;
	return inode;
}


// A Pseudo Memory Filesystem to serve as Rootfs
class RootFs : public FilesysTrait {
public:
	virtual bool makefs(rostr vol_label, void* moreinfo = 0) override { return true; }
	virtual bool loadfs(void* moreinfo = 0) override { return true; }
	virtual bool create(rostr fullpath, stduint flags, void* exinfo, rostr linkdest = 0) override { return false; }
	virtual bool remove(rostr pathname) override { return false; }
	virtual void* search(rostr fullpath, void* moreinfo) override { return nullptr; }
	virtual bool proper(void* handler, stduint cmd, const void* moreinfo = 0) override { return false; }
	virtual bool enumer(void* dir_handler, _tocall_ft _fn) override { return false; }
	virtual stduint readfl(void* fil_handler, Slice file_slice, byte* dst) override { return 0; }
	virtual stduint writfl(void* fil_handler, Slice file_slice, const byte* src) override { return 0; }
};

static RootFs global_rootfs_driver;
alignas(16) byte buf_root_sb[sizeof(vfs_super_block)];


void vfs_init() {
	vfs_root = alloc_dentry(nullptr, "/");
	vfs_super_block* root_sb = new (buf_root_sb) vfs_super_block();
	root_sb->fs = &global_rootfs_driver;
	root_sb->s_root = vfs_root;
	root_sb->type = nullptr;
	root_sb->device_id = 0;
	root_sb->next = nullptr;
	
	super_blocks = root_sb;
	
	vfs_inode* root_inode = alloc_inode(root_sb);
	root_inode->i_mode = I_DIRECTORY;
	vfs_root->d_inode = root_inode;
	
	// Pre-allocate FHS standard directories
	const char* standard_dirs[] = {
		"bin", "boot", "dev", "etc", "her", "home", "proc", "run", "sys", "usr", "var"
	};
	
	for (stduint i = 0; i < sizeof(standard_dirs) / sizeof(const char*); i++) {
		vfs_dentry* dir_dentry = alloc_dentry(vfs_root, standard_dirs[i]);
		vfs_inode* dir_inod = alloc_inode(root_sb);
		dir_inod->i_mode = I_DIRECTORY;
		dir_dentry->d_inode = dir_inod;
	}
}

void register_filesystem(file_system_type* fs_type) {
	fs_type->next = registered_filesystems;
	registered_filesystems = fs_type;
}

// Helper: find dentry by name within a directory
static vfs_dentry* lookup_dentry(vfs_dentry* dir, const char* name, int len) {
	if (!dir) return nullptr;
	vfs_dentry* child = dir->d_first_child;
	while(child) {
		if (StrCompareN(child->d_name, name, len) == 0 && child->d_name[len] == '\0') {
			return child;
		}
		child = child->d_next_sibling;
	}
	return nullptr;
}


vfs_dentry* vfs_namei(const char* pathname) {
	if (!pathname || pathname[0] == '\0') return nullptr;
	
	vfs_dentry* current = vfs_root;
	if (pathname[0] == '/') {
		pathname++;
	}
	
	while(*pathname) {
		// Skip consecutive '/'
		while(*pathname == '/') pathname++;
		if (*pathname == '\0') break;
		
		// Find next component
		const char* next_slash = pathname;
		while(*next_slash && *next_slash != '/') next_slash++;
		
		int len = next_slash - pathname;
		char part[VFS_MAX_FILENAME];
		if (len >= VFS_MAX_FILENAME) len = VFS_MAX_FILENAME - 1;
		MemCopyN(part, pathname, len);
		part[len] = '\0';
		
		vfs_dentry* next_d = lookup_dentry(current, part, len);
		if (!next_d) {
			// Not in cache. Delegating to underlying FS would be implemented here in future.
			return nullptr; // Not found
		}
		
		// MOUNT POINT CROSSING:
		if (next_d->d_mounts) {
			next_d = next_d->d_mounts; // Traverse down the mount point
		}
		
		current = next_d;
		pathname = next_slash;
	}
	
	return current;
}


bool vfs_mount_fs(FilesysTrait* fs, file_system_type* type, const char* target_path) {
	vfs_dentry* target = vfs_namei(target_path);
	if (!target) {
		// We can create the mount point in rootfs if it doesn't exist
		if (target_path[0] == '/') {
			const char* name = target_path + 1;
			target = alloc_dentry(vfs_root, name);
			vfs_inode* mnt_inod = alloc_inode(super_blocks);
			mnt_inod->i_mode = I_DIRECTORY;
			target->d_inode = mnt_inod;
		} else {
			return false;
		}
	}
	
	if (target->d_mounts) return false; // Already heavily mounted
	
	// Create new Superblock for the mount
	vfs_super_block* sb = new vfs_super_block();
	sb->fs = fs;
	sb->type = type;
	sb->next = super_blocks;
	super_blocks = sb;
	
	// Create new root dentry for this mount
	vfs_dentry* s_root = alloc_dentry(nullptr, target->d_name);
	s_root->d_inode = alloc_inode(sb);
	s_root->d_inode->i_mode = I_DIRECTORY;
	
	sb->s_root = s_root;
	target->d_mounts = s_root;
	
	return true;
}


bool vfs_mount(StorageTrait& storage, stduint dev, const char* target_path) {
	for (file_system_type* fs_type = registered_filesystems; fs_type; fs_type = fs_type->next) {
		FilesysTrait* fs = fs_type->probe(storage, dev);
		if (fs) {
			if (fs->loadfs()) {
				return vfs_mount_fs(fs, fs_type, target_path);
			}
		}
	}
	return false;
}

void vfs_tree_node(vfs_dentry* node, int depth) {
	if (!node) return;
	
	// Print indentation
	for (int i = 0; i < depth; i++) {
		Console.OutFormat("  ");
	}
	
	Console.OutFormat("|- %s", node->d_name[0] ? node->d_name : "/");
	if (node->d_mounts) {
		const char* type_name = "unknown/internal";
		if (node->d_mounts->d_inode && node->d_mounts->d_inode->i_sb && node->d_mounts->d_inode->i_sb->type) {
			type_name = node->d_mounts->d_inode->i_sb->type->name;
		}
		Console.OutFormat(" (Mount: %s)\n\r", type_name);
		
		// Hide the structural duplicate mount root node, recurse directly to its contents! (Linux behavior)
		vfs_tree_node(node->d_mounts->d_first_child, depth + 1);
	} else {
		Console.OutFormat("\n\r");
		vfs_tree_node(node->d_first_child, depth + 1);
	}
	
	vfs_tree_node(node->d_next_sibling, depth);
}

void vfs_tree() {
	Console.OutFormat("VFS Virtual Tree Structure:\n\r");
	vfs_tree_node(vfs_root, 0);
}

int vfs_open(const char* pathname, int flags, vfs_file** out_file) {
	vfs_dentry* dentry = vfs_namei(pathname);
	if (!dentry) {
		return -1;
	}
	
	if (!dentry->d_inode) return -1;
	
	vfs_file* file = new vfs_file();
	file->f_dentry = dentry;
	file->f_inode = dentry->d_inode;
	file->f_pos = 0;
	file->f_mode = flags;
	
	if (out_file) *out_file = file;
	return 0; // standard descriptor num would be assigned by the process context
}

int vfs_read(vfs_file* file, void* buf, stduint count) {
	if (!file || !file->f_inode || !file->f_inode->i_sb) return -1;
	FilesysTrait* fs = file->f_inode->i_sb->fs;
	
	stduint bytes = fs->readfl(file->f_inode->internal_handler, Slice{ file->f_pos, count }, (byte*)buf);
	file->f_pos += bytes;
	return bytes;
}

int vfs_write(vfs_file* file, const void* buf, stduint count) {
	if (!file || !file->f_inode || !file->f_inode->i_sb) return -1;
	FilesysTrait* fs = file->f_inode->i_sb->fs;
	
	stduint bytes = fs->writfl(file->f_inode->internal_handler, Slice{ file->f_pos, count }, (const byte*)buf);
	file->f_pos += bytes;
	return bytes;
}

int vfs_close(vfs_file* file) {
	file->f_inode = nullptr;
	return 0;
}

int vfs_remove(const char* pathname) {
	vfs_dentry* dentry = vfs_namei(pathname);
	if (!dentry || !dentry->d_inode || !dentry->d_inode->i_sb) return -1;
	FilesysTrait* fs = dentry->d_inode->i_sb->fs;
	if (fs) {
		return fs->remove(pathname) ? 0 : -1;
	}
	return -1;
}


// -------------------------------------------------------------
// DevFs Implementation
// -------------------------------------------------------------

DevFs global_devfs;

void devfs_register_and_mount() {
	vfs_mount_fs(&global_devfs, nullptr, "/dev");
	
	vfs_dentry* dev_dir = vfs_namei("/dev");
	if (dev_dir) {
		// within DevFs, create dentries for /dev/tty0 to /dev/tty3
		for (int i = 0; i < 4; i++) {
			char tty_name[16];
			String(tty_name, sizeof(tty_name)).Format("tty%u", i);
			vfs_dentry* tty_dentry = alloc_dentry(dev_dir, tty_name);
			vfs_inode* tty_inod = alloc_inode(dev_dir->d_inode->i_sb);
			tty_inod->i_mode = I_CHAR_SPECIAL;
			tty_inod->internal_handler = (void*)(stduint)i; // handler = tty index
			tty_dentry->d_inode = tty_inod;
		}
	}
}

bool DevFs::makefs(rostr vol_label, void* moreinfo) { return true; }
bool DevFs::loadfs(void* moreinfo) { return true; }
bool DevFs::create(rostr fullpath, stduint flags, void* exinfo, rostr linkdest) { return false; }
bool DevFs::remove(rostr pathname) { return false; }
void* DevFs::search(rostr fullpath, void* moreinfo) { return nullptr; }
bool DevFs::proper(void* handler, stduint cmd, const void* moreinfo) { return false; }
bool DevFs::enumer(void* dir_handler, _tocall_ft _fn) { return false; }

stduint DevFs::readfl(void* fil_handler, Slice file_slice, byte* dst) {
	stduint tty_idx = (stduint)fil_handler;
	stduint msg[4];
	msg[0] = MAKE_DEV(DEV_TTY, tty_idx);
	msg[1] = file_slice.address;
	msg[2] = file_slice.length;
	msg[3] = 0; // PID
	return 0; // WIP: syssend to IPC
}

stduint DevFs::writfl(void* fil_handler, Slice file_slice, const byte* src) {
	stduint tty_idx = (stduint)fil_handler;
	stduint msg[4];
	msg[0] = MAKE_DEV(DEV_TTY, tty_idx);
	msg[1] = file_slice.address;
	msg[2] = file_slice.length;
	msg[3] = 0; // PID
	return 0; // WIP: syssend to IPC
}

}
