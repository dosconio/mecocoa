// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <cpp/string>
#include <c/bitmap.h>
using namespace uni;
#include "../include/filesys.hpp"
#include "../include/fileman.hpp" // for DEV_TTY etc
#include "../include/console.hpp" // for VTTY_OUTQ, SysMessage, vtty_type_t
// VFS and DevFs Implementation

extern uni::Queue<SysMessage> message_queue_conv;// defined in graphic.cpp

namespace uni {

	static file_system_type* registered_filesystems = nullptr;
	static vfs_super_block* super_blocks = nullptr;
	static vfs_dentry* vfs_root = nullptr; // Global root directory
	static Spinlock vfs_lock;

	vfs_dentry* Filesys::getRoot() { return vfs_root; }

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
			}
			else {
				vfs_dentry* p = parent->d_first_child;
				while (p->d_next_sibling) p = p->d_next_sibling;
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
		virtual void* search(rostr fullpath, FilesysSearchArgs* args) override { return nullptr; }
		virtual bool proper(void* handler, stduint cmd, const void* moreinfo = 0) override { return false; }
		virtual bool enumer(void* dir_handler, _tocall_ft _fn) override {
			// dir_handler is the vfs_dentry* of this directory (set at inode creation)
			vfs_dentry* dir = (vfs_dentry*)dir_handler;
			if (!dir || !_fn) return false;
			vfs_dentry* child = dir->d_first_child;
			while (child) {
				bool is_dir = child->d_inode &&
					(child->d_inode->i_mode & I_TYPE_MASK) == I_DIRECTORY;
				_fn((void*)(stduint)is_dir, (void*)child->d_name);
				child = child->d_next_sibling;
			}
			return true;
		}
		virtual stduint readfl(void* fil_handler, Slice file_slice, byte* dst) override { return 0; }
		virtual stduint writfl(void* fil_handler, Slice file_slice, const byte* src) override { return 0; }
	};

	static RootFs global_rootfs_driver;
	alignas(16) byte buf_root_sb[sizeof(vfs_super_block)];
}
extern file_system_type fs_fat;


void Filesys::Initialize() {

	Filesys::Register(&fs_fat);

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
	root_inode->internal_handler = vfs_root; // RootFs: dir_handler == its own dentry
	vfs_root->d_inode = root_inode;
	vfs_root->d_mounted_on = nullptr;
	
	// Pre-allocate FHS standard directories
	const char* standard_dirs[] = {
		"bin", "boot", "dev", "etc", "her", "home", "proc", "run", "sys", "usr", "var"
	};
	
	for (stduint i = 0; i < sizeof(standard_dirs) / sizeof(const char*); i++) {
		vfs_dentry* dir_dentry = alloc_dentry(vfs_root, standard_dirs[i]);
		vfs_inode* dir_inod = alloc_inode(root_sb);
		dir_inod->i_mode = I_DIRECTORY;
		dir_inod->internal_handler = dir_dentry; // RootFs: dir_handler == its own dentry
		dir_dentry->d_inode = dir_inod;
	}
}

void Filesys::Register(file_system_type* fs_type) {
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

struct vfs_lookup_ctx {
	vfs_dentry* current;
	vfs_super_block* sb;
	bool found_mount_point;
};

static stduint vfs_on_search_segment(void* handle, const char* name, stduint is_dir, stduint size, void* user_data) {
	vfs_lookup_ctx* ctx = (vfs_lookup_ctx*)user_data;
	if (!ctx || !ctx->current || !name) return 0;

	vfs_dentry* next_d = lookup_dentry(ctx->current, name, StrLength(name));
	if (!next_d) {
		next_d = alloc_dentry(ctx->current, name);
		if (!next_d) return 0;
		vfs_inode* new_inod = alloc_inode(ctx->sb);
		if (!new_inod) return 0;

		new_inod->i_size = (stduint)size;
		if (is_dir == 1) new_inod->i_mode = I_DIRECTORY;
		else if (is_dir == 2) new_inod->i_mode = I_CHAR_SPECIAL;
		else new_inod->i_mode = I_REGULAR;
		
		if (is_dir == 2) {
			new_inod->internal_handler = handle;
		}
		else {
			byte* saved_h = new byte[128];
			if (saved_h) {
				MemCopyN(saved_h, handle, 128);
				new_inod->internal_handler = saved_h;
			}
		}
		
		next_d->d_inode = new_inod;
	}
	ctx->current = next_d;
	// Handle mount point crossing during delegation
	if (ctx->current->d_mounts) {
		ctx->current = ctx->current->d_mounts;
		ctx->sb = ctx->current->d_inode->i_sb;
		ctx->found_mount_point = true;
		return 0; // Stop delegating to the old FS, we've crossed a mount point boundary
	}
	return 1; // Continue search
}


static vfs_dentry* _Index_unlocked(const char* pathname, vfs_dentry* base) {
	if (!pathname || pathname[0] == '\0') return nullptr;
	
	vfs_dentry* current = (pathname[0] == '/') ? vfs_root : base;
	if (!current) current = vfs_root; // Fallback to root if no base provided
	char inner_path[256] = ""; // Path relative to the current mount point
	
	if (pathname[0] != '/' && current) {
		vfs_dentry* temp = current;
		vfs_dentry* stack[32];
		int stack_ptr = 0;
		while (temp && temp->d_parent && stack_ptr < 32) {
			stack[stack_ptr++] = temp;
			temp = temp->d_parent;
		}
		stduint cur_ilen = 0;
		for (int i = stack_ptr - 1; i >= 0; i--) {
			vfs_dentry* d = stack[i];
			stduint len = StrLength(d->d_name);
			if (cur_ilen + len + 2 < 256) {
				if (cur_ilen > 0) inner_path[cur_ilen++] = '/';
				MemCopyN(inner_path + cur_ilen, d->d_name, len);
				inner_path[cur_ilen + len] = '\0';
				cur_ilen += len;
			}
		}
	}
	
	if (pathname[0] == '/') {
		pathname++;
	}
	
	while(*pathname) {
		while(*pathname == '/') pathname++;
		if (*pathname == '\0') break;

		const char* next_slash = pathname;
		while(*next_slash && *next_slash != '/') next_slash++;
		
		int len = next_slash - pathname;
		char part[VFS_MAX_FILENAME];
		if (len >= VFS_MAX_FILENAME) len = VFS_MAX_FILENAME - 1;
		MemCopyN(part, pathname, len);
		part[len] = '\0';

		// Handle special components: "." stays, ".." goes to parent
		if (part[0] == '.' && part[1] == '\0') {
			pathname = next_slash;
			continue; // "." : stay at current directory
		}
		if (part[0] == '.' && part[1] == '.' && part[2] == '\0') {
			if (current->d_parent != nullptr) {
				current = current->d_parent; // go to parent in same FS
			} else if (current->d_mounted_on != nullptr) {
				// Cross mount boundary upward (e.g. root of mounted FAT -> VFS anchor)
				vfs_dentry* anchor = current->d_mounted_on;
				current = anchor->d_parent ? anchor->d_parent : anchor;
			}
			// else: already at VFS root, ".." of root points to itself, stay put
			pathname = next_slash;
			continue;
		}

		vfs_dentry* next_d = lookup_dentry(current, part, len);
		
		if (!next_d) {
			// DELEGATION MODE:
			// If we hit a cache miss within a physical FS, delegate the ENTIRE remainder
			vfs_dentry* real_current = current;
			if (real_current->d_inode && real_current->d_inode->i_sb && real_current->d_inode->i_sb->fs && real_current->d_inode->i_sb->fs != &global_rootfs_driver) {
				FilesysTrait* fs = real_current->d_inode->i_sb->fs;

				byte h_buf[128];
				vfs_dentry* mount_root = real_current;
				while (mount_root && mount_root->d_parent) {
					mount_root = mount_root->d_parent;
				}
				vfs_lookup_ctx ctx = { mount_root, real_current->d_inode->i_sb, false };
				FilesysSearchArgs args = { h_buf, nullptr, vfs_on_search_segment, &ctx };

				// Reconstruct relative path to the mount point root to ensure correct delegation
				char* dyn_inner_path = (char*)malloc(256);
				MemSet(dyn_inner_path, 0, 256);
				vfs_dentry* temp = real_current;
				vfs_dentry** vfs_stack = (vfs_dentry**)malloc(32 * sizeof(vfs_dentry*));
				int stack_ptr = 0;
				while (temp && temp != mount_root && stack_ptr < 32) {
					vfs_stack[stack_ptr++] = temp;
					temp = temp->d_parent;
				}
				stduint cur_ilen = 0;
				for (int i = stack_ptr - 1; i >= 0; i--) {
					vfs_dentry* d = vfs_stack[i];
					stduint len = StrLength(d->d_name);
					if (cur_ilen + len + 2 < 256) {
						if (cur_ilen > 0) dyn_inner_path[cur_ilen++] = '/';
						MemCopyN(dyn_inner_path + cur_ilen, d->d_name, len);
						dyn_inner_path[cur_ilen + len] = '\0';
						cur_ilen += len;
					}
				}
				free(vfs_stack);

				String full_remainder;
				if (dyn_inner_path[0]) full_remainder.Format("%s/%s", dyn_inner_path, pathname);
				else full_remainder.Format("/%s", pathname);

				free(dyn_inner_path);

				if (fs->search(full_remainder.reference(), &args)) {
					// fs->search successfully resolved the path (or partial path) through callbacks
							// ctx.current already points to the leaf or the mount point root
					vfs_dentry* final_d = ctx.current;
					return (final_d && final_d->d_mounts) ? final_d->d_mounts : final_d;
				}
				return nullptr;
			}
		}
		
		if (!next_d) return nullptr;
		
		// Crossing mount point for the NEXT iteration
		if (next_d->d_mounts) {
			current = next_d->d_mounts;
			inner_path[0] = '\0'; // New FS root
		} else {
			current = next_d;
			stduint cur_ilen = StrLength(inner_path);
			if (cur_ilen + len + 2 < 256) {
				if (cur_ilen > 0) inner_path[cur_ilen++] = '/';
				MemCopyN(inner_path + cur_ilen, part, len);
				inner_path[cur_ilen + len] = '\0';
			}
		}
		
		pathname = next_slash;
	}
	
	return (current && current->d_mounts) ? current->d_mounts : current;
}

vfs_dentry* Filesys::Index(const char* pathname, vfs_dentry* base) {
	return _Index_unlocked(pathname, base);
}

static void _Prune_unlocked(vfs_dentry* dentry) {
	if (!dentry) return;
	vfs_dentry* child = dentry->d_first_child;
	while (child) {
		vfs_dentry* next = child->d_next_sibling;
		_Prune_unlocked(child);
		child = next;
	}
	if (dentry->d_inode) delete dentry->d_inode;
	delete dentry;
}

static vfs_dentry* _Create_unlocked(const char* pathname, stduint mode, vfs_dentry* base) {
	if (!pathname || pathname[0] == '\0') return nullptr;

	vfs_dentry* current = (pathname[0] == '/') ? vfs_root : base;
	if (!current) current = vfs_root;

	if (pathname[0] == '/') pathname++;

	while (*pathname) {
		while (*pathname == '/') pathname++;
		if (*pathname == '\0') break;

		const char* next_slash = pathname;
		while (*next_slash && *next_slash != '/') next_slash++;
		int len = next_slash - pathname;

		char part[VFS_MAX_FILENAME];
		if (len >= VFS_MAX_FILENAME) len = VFS_MAX_FILENAME - 1;
		MemCopyN(part, pathname, len);
		part[len] = '\0';

		vfs_dentry* next_d = lookup_dentry(current, part, len);
		if (!next_d) {
			// Create the missing directory entry in the current filesystem (usually rootfs)
			next_d = alloc_dentry(current, part);
			vfs_inode* inod = alloc_inode(current->d_inode->i_sb);
			inod->i_mode = I_DIRECTORY; // Mount points are always directories
			inod->internal_handler = next_d; // RootFs: dir_handler == its own dentry
			next_d->d_inode = inod;
		}

		// Crossing mount points if we are traversing
		if (next_d->d_mounts) current = next_d->d_mounts;
		else current = next_d;

		pathname = next_slash;
	}
	return current;
}

vfs_dentry* Filesys::Create(const char* pathname, stduint mode, vfs_dentry* base) {
	SpinlockLocal guard(&vfs_lock);
	return _Create_unlocked(pathname, mode, base);
}


bool Filesys::MountFilesys(FilesysTrait* fs, file_system_type* type, const char* target_path) {
	SpinlockLocal guard(&vfs_lock);
	vfs_dentry* target = _Index_unlocked(target_path, nullptr);
	if (!target) {
		// Auto-create intermediate directories in VFS if they don't exist
		target = _Create_unlocked(target_path, I_DIRECTORY, nullptr);
	}
	if (!target) return false;
	
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

	// Fetch the physical FS root directory handle and heap-copy it for persistence
	// (same pattern as vfs_on_search_segment: copy 128-byte handle buffer to heap)
	byte root_h_buf[128];
	FilesysSearchArgs root_args = { root_h_buf, nullptr, nullptr, nullptr };
	void* root_handler = fs->search("/", &root_args);
	if (root_handler) {
		byte* saved_handler = new byte[128];
		MemCopyN(saved_handler, root_h_buf, 128);
		s_root->d_inode->internal_handler = saved_handler;
	}
	
	sb->s_root = s_root;
	s_root->d_mounted_on = target; // Store reverse link for path reconstruction
	target->d_mounts = s_root;
	
	return true;
}

bool Filesys::Unmount(const char* target_path) {
	SpinlockLocal guard(&vfs_lock);
	vfs_dentry* target = _Index_unlocked(target_path, nullptr);
	if (!target || !target->d_mounts) {
		return false;
	}

	vfs_dentry* s_root = target->d_mounts;
	vfs_super_block* sb = s_root->d_inode->i_sb;

	// 1. Remove from global superblock list
	if (super_blocks == sb) {
		super_blocks = sb->next;
	}
	else {
		vfs_super_block* p = super_blocks;
		while (p && p->next != sb) p = p->next;
		if (p) p->next = sb->next;
	}

	// 2. Detach from VFS tree
	target->d_mounts = nullptr;

	// 3. Cleanup backend driver and storage
	if (sb->fs) {
		StorageTrait* st = sb->fs->storage;
		delete sb->fs;
		if (st) delete st; // This is the DiscPartition allocated in Mount()
	}

	// 4. Prune all cached dentries for this mount
	_Prune_unlocked(s_root);

	delete sb;
	return true;
}


file_system_type* Filesys::Mount(StorageTrait& storage, stduint dev, const char* target_path) {
	for (file_system_type* fs_type = registered_filesystems; fs_type; fs_type = fs_type->next) {
		// probe() checks sys_id and calls loadfs() internally; non-null means ready to mount
		FilesysTrait* fs = fs_type->probe(storage, dev);
		if (fs) {
			return Filesys::MountFilesys(fs, fs_type, target_path) ? fs_type : nullptr;
		}
	}
	return nullptr;
}

struct PathHarvest {
	char name[256];
	bool is_dir;
	PathHarvest* next;
};

struct EnumContext {
	ProcessBlock* pb;
	void* user_addr;          // User-space address of dirent_t array
	stduint max_count;        // Maximum number of entries to read
	stduint skip_count;       // Entries to skip (file->f_pos)
	stduint current_idx;      // Current item being enumerated in VFS call
	stduint filled_count;     // Number of entries successfully copied to user space
};
static EnumContext g_enum_cxt;

static void user_enumer_callback(void* is_dir, void* name) {
	if (g_enum_cxt.filled_count >= g_enum_cxt.max_count) return;

	// Check if we need to skip this entry
	if (g_enum_cxt.current_idx < g_enum_cxt.skip_count) {
		g_enum_cxt.current_idx++;
		return;
	}

	g_enum_cxt.current_idx++;

	dirent_t kde;
	kde.is_dir = (stduint)is_dir;
	// Copy name and terminate it safely
	stduint name_len = StrLength((const char*)name);
	if (name_len >= 64) name_len = 63;
	MemCopyN(kde.name, (const char*)name, name_len);
	kde.name[name_len] = '\0';

	// Copy to user space memory
	stduint offset = g_enum_cxt.filled_count * sizeof(dirent_t);
	MemCopyP((void*)((stduint)g_enum_cxt.user_addr + offset), g_enum_cxt.pb->paging, &kde, kernel_paging, sizeof(dirent_t));// To local area

	g_enum_cxt.filled_count++;
}

static PathHarvest** g_harvest_tail = nullptr;

static void tree_callback(void* is_dir, void* name) {
	if (!g_harvest_tail) return;
	PathHarvest* n = new PathHarvest;
	if (!n) return;
	StrCopy(n->name, (const char*)name);
	n->is_dir = (bool)(stduint)is_dir;
	n->next = nullptr;
	*g_harvest_tail = n;
	g_harvest_tail = &n->next;
}

void vfs_tree_physical(FilesysTrait* fs, const char* path, int depth) {
	if (depth > 6) return; // Hard depth limit for kernel stack safety

	PathHarvest* head = nullptr;
	PathHarvest** old_tail = g_harvest_tail;
	g_harvest_tail = &head;

	// Use temporary stack buffer for search handle to avoid persistent memory leak
	byte h_buf[128];
	FilesysSearchArgs args = { h_buf, nullptr, nullptr, nullptr };
	void* handler = fs->search(path, &args);
	if (handler) {
		fs->enumer(handler, (_tocall_ft)tree_callback);
	}

	g_harvest_tail = old_tail; // Restore global state for recursion safety

	PathHarvest* curr = head;
	int count = 0;
	while (curr) {
		PathHarvest* next = curr->next;

		// If we exceed display limit, DO NOT break immediately. 
		// We MUST continue iterating just to delete the remaining nodes to prevent memory leak!
		if (++count > 100) {
			plogwarn("vfs_tree_physical: Exceeded display limit\n\r");
			free(curr);
			curr = next;
			continue;
		}

		// Trim trailing spaces for FAT 8.3 compatibility
		// FAT returns names like ".       ". We must trim to "." before comparing
		int nlen = 0;
		while (curr->name[nlen] != '\0') nlen++;
		while (nlen > 0 && curr->name[nlen - 1] == ' ') {
			curr->name[nlen - 1] = '\0';
			nlen--;
		}

		// Now it safely filters out "." and ".."
		if (StrCompare(curr->name, ".") && StrCompare(curr->name, "..")) {
			for (int i = 0; i < depth; i++) Console.OutFormat("  ");
			Console.OutFormat("|- %s\n\r", curr->name);

			if (curr->is_dir) {
				char* subpath = new char[512];
				if (subpath) {
					if (!StrCompare(path, "/")) {
						String(subpath, 512).Format("/%s", curr->name);
					}
					else {
						String(subpath, 512).Format("%s/%s", path, curr->name);
					}
					vfs_tree_physical(fs, subpath, depth + 1);
					free(subpath);
				}
			}
		}

		// Safely delete processed node
		free(curr);
		curr = next;
	}
}

void vfs_tree_node(vfs_dentry* node, int depth) {
	if (!node) return;
	
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
		// Recurse into physical filesystem if mounted
		if (node->d_mounts->d_inode && node->d_mounts->d_inode->i_sb && node->d_mounts->d_inode->i_sb->fs) {
			vfs_tree_physical(node->d_mounts->d_inode->i_sb->fs, "/", depth + 1);
		}

		vfs_tree_node(node->d_mounts->d_first_child, depth + 1);
	} else {
		Console.OutFormat("\n\r");
		vfs_tree_node(node->d_first_child, depth + 1);
	}
	
	vfs_tree_node(node->d_next_sibling, depth);
}

void Filesys::Tree() {
	SpinlockLocal guard(&vfs_lock);
	Console.OutFormat("VFS Virtual Tree Structure:\n\r");
	vfs_tree_node(vfs_root, 0);
}

int Filesys::Open(const char* pathname, int flags, vfs_file** out_file, vfs_dentry* base) {
	vfs_dentry* dentry = Filesys::Index(pathname, base);
	if (dentry && dentry->d_inode) {
		// O_DIRECTORY: If pathname refers to a non-directory file, open() shall fail.
		if ((flags & O_DIRECTORY) && (dentry->d_inode->i_mode & I_TYPE_MASK) != I_DIRECTORY) {
			return -1;
		}
	}

	if (!dentry || !dentry->d_inode) {
		// File does not exist. Check if we need to create it.
		if (flags & O_CREAT) {
			// Extract parent directory path
			char* dir_path = (char*)malloc(256);
			MemSet(dir_path, 0, 256);
			const char* last_slash = pathname;

			for (const char* p = pathname; *p; p++) {
				if (*p == '/') last_slash = p;
			}

			vfs_dentry* parent_dentry = nullptr;
			if (last_slash == pathname && pathname[0] != '/') {
				parent_dentry = base ? base : vfs_root;
			}
			else {
				if (last_slash == pathname) {
					StrCopy(dir_path, "/");
				}
				else {
					stduint len = last_slash - pathname;
					if (len == 0) len = 1; // Preserve root '/'
					MemCopyN(dir_path, pathname, len);
					dir_path[len] = '\0';
				}
				parent_dentry = Filesys::Index(dir_path, base);
			}

			if (!parent_dentry || !parent_dentry->d_inode || !parent_dentry->d_inode->i_sb) {
				plogwarn("Parent directory not found for creation: %s", dir_path);
				free(dir_path);
				return -1;
			}

			free(dir_path);

			FilesysTrait* fs = parent_dentry->d_inode->i_sb->fs;
			if (!fs) return -1;

			// Construct the new dentry for the Virtual File System properly
			const char* fname = last_slash;
			if (*fname == '/') fname++;

			// Delegate the creation to the physical filesystem
			// Pass the physical handler of the parent directory
			void* new_inode_ptr = parent_dentry->d_inode->internal_handler;

			// Strip VFS absolute path: Pass ONLY the pure filename to the physical FS
			if (!fs->create(fname, flags, &new_inode_ptr, nullptr)) {
				plogwarn("Physical filesystem failed to create file: %s", pathname);
				return -1;
			}

			// Use alloc_dentry to link the new file into the VFS directory tree
			dentry = alloc_dentry(parent_dentry, fname);

			// Use alloc_inode to create a proper Virtual Inode with a valid Superblock (i_sb)
			vfs_inode* new_vfs_inode = alloc_inode(parent_dentry->d_inode->i_sb);
			new_vfs_inode->i_mode = (flags & O_DIRECTORY) ? I_DIRECTORY : I_REGULAR;
			new_vfs_inode->i_size = 0;

			// SECURE BRIDGE: Save the physical handler into internal_handler
			new_vfs_inode->internal_handler = new_inode_ptr;

			dentry->d_inode = new_vfs_inode;

		}
		else {
			// File not found and O_CREAT is not specified
			return -1;
		}
	}
	else {
		// File already exists
		if ((flags & O_CREAT) && (flags & O_EXCL)) {
			ploginfo("file `%s' exists (O_EXCL)", pathname);
			return -1;
		}

		if (flags & O_TRUNC) {
			// POSIX: O_TRUNC has no effect on FIFO or terminal device files.
			// And requires writability.
			int mode = flags & O_ACCMODE;
			if (mode == O_WRONLY || mode == O_RDWR) {
				dentry->d_inode->i_size = 0;
			}
		}
	}

	vfs_file* file = new vfs_file();
	file->f_dentry = dentry;
	file->f_inode = dentry->d_inode;
	file->f_mode = flags;
	
	// O_APPEND: The file offset shall be set to the end of the file prior to each write.
	if (flags & O_APPEND) {
		file->f_pos = dentry->d_inode->i_size;
	} else {
		file->f_pos = 0;
	}

	if (out_file) *out_file = file;
	return 0;
}

int Filesys::Read(vfs_file* file, void* buf, stduint count) {
	if (!file || !file->f_inode || !file->f_inode->i_sb) return -1;
	SpinlockLocal guard(&vfs_lock);
	FilesysTrait* fs = file->f_inode->i_sb->fs;
	
	stduint bytes = fs->readfl(file->f_inode->internal_handler, Slice{ file->f_pos, count }, (byte*)buf);
	file->f_pos += bytes;
	return bytes;
}

int Filesys::Write(vfs_file* file, const void* buf, stduint count) {
	if (!file || !file->f_inode || !file->f_inode->i_sb) return -1;
	SpinlockLocal guard(&vfs_lock);
	FilesysTrait* fs = file->f_inode->i_sb->fs;

	stduint bytes = fs->writfl(file->f_inode->internal_handler, Slice{ file->f_pos, count }, (const byte*)buf);
	file->f_pos += bytes;
	if (file->f_pos > file->f_inode->i_size) {
		file->f_inode->i_size = file->f_pos;
	}
	return bytes;
}

int Filesys::Close(vfs_file* file) {
	SpinlockLocal guard(&vfs_lock);
	if (file) {
		if (file->f_inode && file->f_pos > file->f_inode->i_size) {
			file->f_inode->i_size = file->f_pos;
		}
		file->f_inode = nullptr;
		free(file);
	}
	return 0;
}

int Filesys::Enumer(vfs_file* file, void* buf, stduint count, ProcessBlock* pb) {
	if (!file || !file->f_inode || !file->f_inode->i_sb) return -1;
	SpinlockLocal guard(&vfs_lock);
	FilesysTrait* fs = file->f_inode->i_sb->fs;

	g_enum_cxt.pb = pb;
	g_enum_cxt.user_addr = buf;
	g_enum_cxt.max_count = count;
	g_enum_cxt.skip_count = file->f_pos;
	g_enum_cxt.current_idx = 0;
	g_enum_cxt.filled_count = 0;

	fs->enumer(file->f_inode->internal_handler, (_tocall_ft)user_enumer_callback);

	file->f_pos += g_enum_cxt.filled_count;
	return g_enum_cxt.filled_count;
}

bool Filesys::Remove(const char* pathname, vfs_dentry* base) {
	vfs_dentry* dentry = Filesys::Index(pathname, base);
	if (!dentry || !dentry->d_inode || !dentry->d_inode->i_sb) return false;

	FilesysTrait* fs = dentry->d_inode->i_sb->fs;
	if (fs) {
		// Reconstruct relative path for the physical filesystem
		char rel_path[256];
		rel_path[0] = '\0';
		vfs_dentry* curr = dentry;

		// Traverse upward until the mount point root (where d_parent is nullptr)
		while (curr && curr->d_parent) {
			char temp[256];
			StrCopy(temp, rel_path);

			// Prepend current directory/file name
			String(rel_path, 256).Format("/%s%s", curr->d_name, temp);
			curr = curr->d_parent;
		}

		// Handle root edge-case
		if (rel_path[0] == '\0') {
			StrCopy(rel_path, "/");
		}

		// Pass the pure relative path to the underlying physical FS
		bool ret = fs->remove(rel_path);
		if (ret) {
			SpinlockLocal guard(&vfs_lock);
			vfs_dentry* parent = dentry->d_parent;
			if (parent) {
				if (parent->d_first_child == dentry) {
					parent->d_first_child = dentry->d_next_sibling;
				}
				else {
					vfs_dentry* prev = parent->d_first_child;
					while (prev && prev->d_next_sibling != dentry) {
						prev = prev->d_next_sibling;
					}
					if (prev) {
						prev->d_next_sibling = dentry->d_next_sibling;
					}
				}
			}
			// Keep dentry and inode in memory to prevent Use-After-Free for active process file descriptors.
			// They will remain as disconnected orphans and safely closed by closedir/close without crash.
		}
		return ret;
	}
	return false;
}

//{} single mount level only
String Filesys::getAbsolutePath(vfs_dentry* dentry) {
	String res;
	if (!dentry) return res;

	char path[512];
	path[0] = '\0';
	vfs_dentry* curr = dentry;

	while (curr) {
		if (curr == vfs_root) break;

		char temp[512];
		StrCopy(temp, path);
		String(path, 512).Format("/%s%s", curr->d_name, temp);

		if (curr->d_parent == nullptr && curr->d_mounted_on != nullptr) {
			curr = curr->d_mounted_on->d_parent;
		}
		else {
			curr = curr->d_parent;
		}
	}
	if (path[0] == '\0') {
		StrCopy(path, "/");
	}
	res.Format("%s", path);
	return res;
}

// -------------------------------------------------------------
// DevFs Implementation
// -------------------------------------------------------------

uni::DevFs uni::global_devfs;

static uint32 _tty_id_bits = 0;
static Bitmap tty_id_allocator(&_tty_id_bits, 4);

int DevFs::allocate_tty_id() {
	for (int i = 0; i < 32; i++) {
		if (!tty_id_allocator.bitof(i)) {
			tty_id_allocator.setof(i, true);
			return i;
		}
	}
	return -1;
}

void DevFs::free_tty_id(int id) {
	tty_id_allocator.setof(id, false);
}

bool DevFs::makefs(rostr vol_label, void* moreinfo) { return true; }
bool DevFs::loadfs(void* moreinfo) { return true; }
bool DevFs::create(rostr fullpath, stduint flags, void* exinfo, rostr linkdest) { return false; }
bool DevFs::remove(rostr pathname) { return false; }

void* DevFs::search(rostr fullpath, FilesysSearchArgs* args) {
	if (StrCompare(fullpath, "/tty") == 0) {
		if (args->on_segment) args->on_segment((void*)~0, "tty", 2, 0, args->user_data);
		return (void*)~0; // Special handler for /dev/tty
	}
	if (StrCompare(fullpath, "/pts") == 0) {
		if (args->on_segment) args->on_segment((void*)0x1000, "pts", 1, 0, args->user_data);
		return (void*)0x1000; // Marker for pts directory
	}
	if (StrCompareN(fullpath, "/pts/", 5) == 0) {
		const char* s = fullpath + 5;
		stduint id = 0;
		while (*s >= '0' && *s <= '9') {
			id = id * 10 + (*s - '0');
			s++;
		}
		if (*s == '\0') {
			// Verify ID exists
			for (auto nod = vttys.Root(); nod; nod = nod->next) {
				if (((vtty_type_t*)nod->type)->id == id) {
					char name[16]; String(name, 16).Format("%u", id);
					if (args->on_segment) {
						args->on_segment((void*)0x1000, "pts", 1, 0, args->user_data);
						args->on_segment((void*)id, name, 2, 0, args->user_data);
					}
					return (void*)id;
				}
			}
		}
	}
	return nullptr;
}

bool DevFs::proper(void* handler, stduint cmd, const void* moreinfo) {
	if (cmd == (stduint)FilesysCmd::FS_CMD_GET_ISDIR) {
		bool* p_isdir = (bool*)moreinfo;
		if (handler == (void*)0x1000) *p_isdir = true;
		else *p_isdir = false;
		return true;
	}
	return false;
}

bool DevFs::enumer(void* dir_handler, _tocall_ft _fn) {
	if (dir_handler == (void*)~0 || dir_handler == nullptr) { // Root of /dev
		_fn((void*)0, (void*)"tty");
		_fn((void*)1, (void*)"pts");
		return true;
	}
	if (dir_handler == (void*)0x1000) { // /dev/pts
		for (auto nod = vttys.Root(); nod; nod = nod->next) {
			char name[16];
			String(name, 16).Format("%u", ((vtty_type_t*)nod->type)->id);
			_fn((void*)0, (void*)name);
		}
		return true;
	}
	return false;
}

stduint DevFs::readfl(void* fil_handler, Slice file_slice, byte* dst) {
	stduint tty_id = (stduint)fil_handler;
	Dnode* tty_node = nullptr;

	if (tty_id == (stduint)~0) { // Unbound Magic TTY
		return 0;
	} else {
		tty_node = (Dnode*)fil_handler;
	}

	if (!tty_node) {
		return 0;
	}

	QueueLimited* input_queue = VTTY_INNQ(tty_node);
	if (!input_queue) {
		plogwarn("tty %u input queue not found", tty_id);
		return 0;
	}

	Console_t* con = (Console_t*)tty_node->offs;

	stduint bytes_read = 0;
	while (bytes_read < file_slice.length) {
		int ch = input_queue->inn();
		if (ch == -1) break;
		if (ch == '\b' || ch == 0x7F) {
			if (bytes_read > 0) {
				bytes_read--;
				asserv(con)->out("\b \b", 3);
			}
			continue;
		}
		dst[bytes_read++] = (byte)ch;
		if (con) {
			con->OutChar(ch);
		}
		if (ch == '\n') {
			// con->OutChar('\r');
			break;
		}
	}

	return bytes_read;
}

stduint DevFs::writfl(void* fil_handler, Slice file_slice, const byte* src) {
	stduint tty_id = (stduint)fil_handler;
	Dnode* tty_node = nullptr;

	if (tty_id == (stduint)~0) { // Unbound Magic TTY
		return 0;
	} else {
		tty_node = (Dnode*)fil_handler;
	}

	if (!tty_node) return 0;

	Console_t* con = (Console_t*)tty_node->offs;
	if (!con) return 0;

	con->out((rostr)src, file_slice.length);

	return file_slice.length;
}

// FS
#include <c/format/filesys.h>
#include <c/format/filesys/FAT.h>

file_system_type fs_fat = { "fat", [](StorageTrait& storage, stduint dev) -> FilesysTrait* {
		DiscPartition part(storage, dev);
		if (part.getSlice().length == 0) return nullptr;
		byte sys_id = part.getSlice().sys_id;
		// Determine FAT sub-type from partition sys_id
		int fat_ver;
		if (sys_id == FILESYS_FAT12)           fat_ver = 12;
		else if (sys_id == FILESYS_FAT16_CHS)  fat_ver = 16;
		else if (sys_id == FILESYS_FAT16_CHSX) fat_ver = 16;
		else if (sys_id == FILESYS_FAT16_LBA)  fat_ver = 16;
		else if (sys_id == FILESYS_FAT32_CHS)  fat_ver = 32;
		else if (sys_id == FILESYS_FAT32_LBA)  fat_ver = 32;
		else return nullptr; // not a FAT partition

		DiscPartition* p_part = new DiscPartition(storage, dev);
		p_part->getSlice(); // initialize internal slice

		stduint sec_size = p_part->Block_Size > 0 ? p_part->Block_Size : 512;
		byte* fat_sec_buf = new byte[sec_size];
		byte* fat_buf = new byte[0x1000];

		FilesysFAT* fs = new FilesysFAT(fat_ver, *p_part, fat_sec_buf, fat_buf);

		if (fs->loadfs()) {
			return fs;
		}

		// Clean up dynamically allocated buffers on failure to prevent memory leaks
		free(fs);
		free(p_part);
		free(fat_buf);
		free(fat_sec_buf);
		return nullptr;
	},
	nullptr
};

