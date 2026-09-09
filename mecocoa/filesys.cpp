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

#ifndef ECONNRESET
#define ECONNRESET 104
#endif

static uni::vfs_dentry* _Index_unlocked(const char* pathname, uni::vfs_dentry* base);

file_system_type* registered_filesystems = nullptr;

extern SpinlockBlock<uni::Queue<SysMessage>> message_queue_conv;// defined in graphic.cpp

namespace uni {

	static vfs_super_block* super_blocks = nullptr;
	static vfs_dentry* vfs_root = nullptr; // Global root directory
	Mutex vfs_lock;
	static constexpr uint16 SocketHandleFlagLocalAddressAuto = 0x0001u;
	static constexpr uint16 SocketHandleFlagReuseAddress = 0x0002u;

	static bool IsLocalBindIPv4AddressAllowed(const Network::IPv4Address& address) {
		if (address.isZero()) return true;
		#if (_MCCA & 0xFF00) != 0x8600
		return false;
		#else
		syscall_net_route_ipv4_t route{};
		syscall_net_interface_ipv4_t iface{};
		if (!Devsman::GetDefaultIPv4Route(&route, sizeof(route))) return false;
		if (!Devsman::GetIPv4Interface(route.link_index, &iface, sizeof(iface))) return false;
		for0(i, Network::IPv4AddressLength) {
			if (address.octet[i] != iface.address[i]) return false;
		}
		return true;
		#endif
	}

	static stduint count_mounts_for_source_node_unlocked(DeviceNode* source_device_node) {
		if (!source_device_node) return 0;
		stduint count = 0;
		for (auto* sb = super_blocks; sb; sb = sb->next) {
			if (sb->source_device_node == source_device_node) {
				++count;
			}
		}
		return count;
	}

	static String get_first_mount_path_for_source_node_unlocked(DeviceNode* source_device_node) {
		String res;
		if (!source_device_node) return res;
		for (auto* sb = super_blocks; sb; sb = sb->next) {
			if (sb->source_device_node != source_device_node) continue;
			if (!sb->s_root || !sb->s_root->d_mounted_on) continue;
			return Filesys::getAbsolutePath(sb->s_root->d_mounted_on);
		}
		return res;
	}

	vfs_dentry* Filesys::getRoot() { return vfs_root; }

	DeviceNode* Filesys::GetMountSourceNode(vfs_dentry* dentry) {
		MutexLocal guard(&vfs_lock);
		if (!dentry) return nullptr;
		if (dentry->d_mounts && dentry->d_mounts->d_inode && dentry->d_mounts->d_inode->i_sb) {
			return dentry->d_mounts->d_inode->i_sb->source_device_node;
		}
		if (dentry->d_inode && dentry->d_inode->i_sb) {
			return dentry->d_inode->i_sb->source_device_node;
		}
		return nullptr;
	}

	DeviceNode* Filesys::GetMountSourceNode(const char* pathname, vfs_dentry* base) {
		MutexLocal guard(&vfs_lock);
		vfs_dentry* dentry = _Index_unlocked(pathname, base);
		if (!dentry) return nullptr;
		if (dentry->d_mounts && dentry->d_mounts->d_inode && dentry->d_mounts->d_inode->i_sb) {
			return dentry->d_mounts->d_inode->i_sb->source_device_node;
		}
		if (dentry->d_inode && dentry->d_inode->i_sb) {
			return dentry->d_inode->i_sb->source_device_node;
		}
		return nullptr;
	}

	stduint Filesys::CountMountsForSourceNode(DeviceNode* source_device_node) {
		MutexLocal guard(&vfs_lock);
		return count_mounts_for_source_node_unlocked(source_device_node);
	}

	String Filesys::GetFirstMountPathForSourceNode(DeviceNode* source_device_node) {
		MutexLocal guard(&vfs_lock);
		return get_first_mount_path_for_source_node_unlocked(source_device_node);
	}

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
				KASSERT(p != nullptr);
				while (p->d_next_sibling) p = p->d_next_sibling;
				p->d_next_sibling = dentry;
			}
		}
		return dentry;
	}

	static vfs_inode* alloc_inode(vfs_super_block* sb) {
		vfs_inode* inode = new vfs_inode();
		KASSERT(inode != nullptr);
		MemSet(inode, 0, sizeof(vfs_inode));
		inode->i_sb = sb;
		inode->ref_count = 1;
		return inode;
	}

	static bool vfs_enum_emit(FilesysEnumState* state, _tocall_ft callback, void* is_dir, void* name) {
		if (!callback) return false;
		if (!state) {
			callback(is_dir, name);
			return true;
		}
		if (state->finished || state->full()) return false;
		if (state->should_emit()) {
			callback(is_dir, name);
			state->emit_one();
			return !state->full();
		}
		state->skip_one();
		return true;
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
		virtual bool enumer(void* dir_handler, _tocall_ft _fn, FilesysEnumState* state = nullptr) override {
			// dir_handler is the vfs_dentry* of this directory (set at inode creation)
			vfs_dentry* dir = (vfs_dentry*)dir_handler;
			if (!dir || !_fn) return false;
			if (state && (state->finished || state->full())) return true;
			vfs_dentry* child = dir->d_first_child;
			while (child) {
				bool is_dir = child->d_inode &&
					(child->d_inode->i_mode & I_TYPE_MASK) == I_DIRECTORY;
				if (!vfs_enum_emit(state, _fn, (void*)(stduint)is_dir, (void*)child->d_name)) break;
				child = child->d_next_sibling;
			}
			if (state && !state->full()) state->mark_finished();
			return true;
		}
		virtual stduint readfl(void* fil_handler, Slice file_slice, byte* dst) override { return 0; }
		virtual stduint writfl(void* fil_handler, Slice file_slice, const byte* src) override { return 0; }
	};

	static constexpr stduint kDeviceTreeRootMarker = 0x2000u;
	static constexpr stduint kDeviceTreeMountPathMarker = 0x2001u;
	static constexpr stduint kDeviceTreeNodeTypeMarker = 0x2002u;
	static constexpr stduint kDeviceTreeDriverNameMarker = 0x2003u;
	static constexpr stduint kDeviceTreeBlockSizeMarker = 0x2004u;
	static constexpr stduint kDeviceTreeUnitCountMarker = 0x2005u;
	static constexpr stduint kDeviceTreeByteSizeMarker = 0x2006u;
	static constexpr stduint kDeviceTreeBindingStateMarker = 0x2007u;
	static constexpr stduint kDeviceTreeMountCountMarker = 0x2008u;
	static constexpr stduint kDeviceTreeNodeNameMarker = 0x2009u;
	static constexpr rostr kDeviceTreeMountPathName = "mount-path";
	static constexpr rostr kDeviceTreeNodeTypeName = "node-type";
	static constexpr rostr kDeviceTreeDriverNameName = "driver-name";
	static constexpr rostr kDeviceTreeBlockSizeName = "block-size";
	static constexpr rostr kDeviceTreeUnitCountName = "unit-count";
	static constexpr rostr kDeviceTreeByteSizeName = "byte-size";
	static constexpr rostr kDeviceTreeBindingStateName = "binding-state";
	static constexpr rostr kDeviceTreeMountCountName = "mount-count";
	static constexpr rostr kDeviceTreeNodeNameName = "node-name";

	struct DeviceTreeHandle {
		stduint marker = 0;
		DeviceNode* node = nullptr;
	};

	static DeviceNode* device_tree_find_child(DeviceNode* parent, const char* name, stduint len) {
		if (!parent || !name || !len) return nullptr;
		for (auto* child = reinterpret_cast<DeviceNode*>(parent->link.subf);
			child; child = reinterpret_cast<DeviceNode*>(child->link.next)) {
			if (!child->link.addr) continue;
			if (StrCompareN(child->link.addr, name, len) == 0 && child->link.addr[len] == '\0') {
				return child;
			}
		}
		return nullptr;
	}

	static DeviceTreeHandle* device_tree_set_handle(void* handle_buffer, DeviceNode* node, stduint marker) {
		if (!handle_buffer) return nullptr;
		auto* handle = reinterpret_cast<DeviceTreeHandle*>(handle_buffer);
		handle->marker = marker;
		handle->node = node;
		return handle;
	}

	static DeviceNode* device_tree_handle_node(void* handler) {
		if (!handler) return nullptr;
		auto* handle = reinterpret_cast<DeviceTreeHandle*>(handler);
		if (handle->marker != kDeviceTreeRootMarker) return handle->node;
		return handle->node;
	}

	static bool device_tree_handle_is_root(void* handler) {
		if (!handler) return false;
		auto* handle = reinterpret_cast<DeviceTreeHandle*>(handler);
		return handle->marker == kDeviceTreeRootMarker;
	}

	static const char* device_node_type_name(DeviceNodeType type) {
		switch (type) {
		case DeviceNodeType::SystemRoot: return "SystemRoot";
		case DeviceNodeType::BusRoot: return "BusRoot";
		case DeviceNodeType::PCI_Root: return "PCI_Root";
		case DeviceNodeType::PciBus: return "PciBus";
		case DeviceNodeType::PciDevice: return "PciDevice";
		case DeviceNodeType::IsaBus: return "IsaBus";
		case DeviceNodeType::StorageDevice: return "StorageDevice";
		case DeviceNodeType::UsbBus: return "UsbBus";
		case DeviceNodeType::UsbRootHub: return "UsbRootHub";
		case DeviceNodeType::UsbPort: return "UsbPort";
		case DeviceNodeType::UsbDevice: return "UsbDevice";
		case DeviceNodeType::UsbInterface: return "UsbInterface";
		case DeviceNodeType::PlatformDevice: return "PlatformDevice";
		case DeviceNodeType::SerioController: return "SerioController";
		case DeviceNodeType::SerioDevice: return "SerioDevice";
		default: return "Unknown";
		}
	}

	static const char* device_binding_state_name(uint32 state) {
		switch (DriverBindingState(state)) {
		case DriverBindingState::None: return "none";
		case DriverBindingState::Matched: return "matched";
		case DriverBindingState::Probed: return "probed";
		case DriverBindingState::Started: return "started";
		case DriverBindingState::Failed: return "failed";
		default: return "unknown";
		}
	}

	static bool device_tree_handle_is_property(void* handler) {
		if (!handler) return false;
		auto* handle = reinterpret_cast<DeviceTreeHandle*>(handler);
		return handle->marker >= kDeviceTreeMountPathMarker && handle->marker <= kDeviceTreeNodeNameMarker;
	}

	static bool device_tree_node_is_storage(DeviceNode* node) {
		return node && DeviceNodeType(node->fields.node_type) == DeviceNodeType::StorageDevice;
	}

	static bool device_tree_try_storage_numbers(DeviceNode* node, stduint* block_size, stduint* unit_count, uint64* byte_size) {
		if (!device_tree_node_is_storage(node)) return false;
		auto* storage = static_cast<uni::StorageTrait*>(node->fields.binding.driver_data);
		if (!storage) return false;
		stduint local_block_size = storage->Block_Size;
		stduint local_unit_count = storage->getUnits();
		uint64 local_byte_size = uint64(local_block_size) * uint64(local_unit_count);
		if (block_size) *block_size = local_block_size;
		if (unit_count) *unit_count = local_unit_count;
		if (byte_size) *byte_size = local_byte_size;
		return true;
	}

	static bool device_tree_property_name_to_marker(const char* name, stduint len, stduint& marker_out) {
		struct PropertyNameMap { const char* name; stduint marker; };
		static const PropertyNameMap maps[] = {
			{kDeviceTreeMountPathName, kDeviceTreeMountPathMarker},
			{kDeviceTreeNodeTypeName, kDeviceTreeNodeTypeMarker},
			{kDeviceTreeDriverNameName, kDeviceTreeDriverNameMarker},
			{kDeviceTreeBindingStateName, kDeviceTreeBindingStateMarker},
			{kDeviceTreeMountCountName, kDeviceTreeMountCountMarker},
			{kDeviceTreeNodeNameName, kDeviceTreeNodeNameMarker},
			{kDeviceTreeBlockSizeName, kDeviceTreeBlockSizeMarker},
			{kDeviceTreeUnitCountName, kDeviceTreeUnitCountMarker},
			{kDeviceTreeByteSizeName, kDeviceTreeByteSizeMarker},
		};
		for (const auto& map : maps) {
			if (len == StrLength(map.name) && StrCompareN(name, map.name, len) == 0) {
				marker_out = map.marker;
				return true;
			}
		}
		return false;
	}

	static bool device_tree_property_available(DeviceNode* node, stduint marker) {
		if (!node) return false;
		switch (marker) {
		case kDeviceTreeMountPathMarker:
			return count_mounts_for_source_node_unlocked(node) != 0;
		case kDeviceTreeNodeNameMarker:
		case kDeviceTreeNodeTypeMarker:
			return true;
		case kDeviceTreeDriverNameMarker:
			return node->fields.binding.driver_name != nullptr;
		case kDeviceTreeBindingStateMarker:
			return node->fields.binding.state != static_cast<uint32>(DriverBindingState::None);
		case kDeviceTreeMountCountMarker:
			return count_mounts_for_source_node_unlocked(node) != 0;
		case kDeviceTreeBlockSizeMarker:
		case kDeviceTreeUnitCountMarker:
		case kDeviceTreeByteSizeMarker:
			return device_tree_node_is_storage(node) && node->fields.binding.driver_data != nullptr;
		default:
			return false;
		}
	}

	static stduint device_tree_property_text(DeviceNode* node, stduint marker, char* buf, stduint cap) {
		if (!node || !buf || cap == 0) return 0;
		buf[0] = '\0';
		String out(buf, cap);
		int written = 0;
		switch (marker) {
		case kDeviceTreeMountPathMarker: {
			String mount_path = get_first_mount_path_for_source_node_unlocked(node);
			written = out.Format("%s", mount_path.reference());
			break;
		}
		case kDeviceTreeNodeNameMarker:
			written = out.Format("%s", node->link.addr ? node->link.addr : "");
			break;
		case kDeviceTreeNodeTypeMarker:
			written = out.Format("%s", device_node_type_name(DeviceNodeType(node->fields.node_type)));
			break;
		case kDeviceTreeDriverNameMarker:
			written = out.Format("%s", node->fields.binding.driver_name ? node->fields.binding.driver_name : "");
			break;
		case kDeviceTreeBindingStateMarker:
			written = out.Format("%s", device_binding_state_name(node->fields.binding.state));
			break;
		case kDeviceTreeMountCountMarker:
			written = out.Format("%u", count_mounts_for_source_node_unlocked(node));
			break;
		case kDeviceTreeBlockSizeMarker: {
			stduint block_size = 0;
			if (!device_tree_try_storage_numbers(node, &block_size, nullptr, nullptr)) return 0;
			written = out.Format("%u", block_size);
			break;
		}
		case kDeviceTreeUnitCountMarker: {
			stduint unit_count = 0;
			if (!device_tree_try_storage_numbers(node, nullptr, &unit_count, nullptr)) return 0;
			written = out.Format("%u", unit_count);
			break;
		}
		case kDeviceTreeByteSizeMarker: {
			uint64 byte_size = 0;
			if (!device_tree_try_storage_numbers(node, nullptr, nullptr, &byte_size)) return 0;
			written = out.Format("%[64d]", byte_size);
			break;
		}
		default:
			return 0;
		}
		return written > 0 ? stduint(written) : 0;
	}

	class DeviceTreeFs : public FilesysTrait {
	public:
		virtual bool makefs(rostr vol_label, void* moreinfo = 0) override { return true; }
		virtual bool loadfs(void* moreinfo = 0) override { return true; }
		virtual bool create(rostr fullpath, stduint flags, void* exinfo, rostr linkdest = 0) override { return false; }
		virtual bool remove(rostr pathname) override { return false; }
		virtual void* search(rostr fullpath, FilesysSearchArgs* args) override {
			if (!fullpath || !*fullpath) return nullptr;
			if (StrCompare(fullpath, "/") == 0) {
				return device_tree_set_handle(args ? args->handle_buffer : nullptr, Devsman::Root(), kDeviceTreeRootMarker);
			}
			auto* node = Devsman::Root();
			if (!node) return nullptr;
			const char* segment = fullpath[0] == '/' ? fullpath + 1 : fullpath;
			bool first_segment = true;
			while (*segment) {
				const char* slash = segment;
				while (*slash && *slash != '/') slash++;
				stduint seg_len = stduint(slash - segment);
				if (!seg_len) break;
				const bool is_last_segment = (*slash == '\0');
				stduint property_marker = 0;
				if (is_last_segment && device_tree_property_name_to_marker(segment, seg_len, property_marker)) {
					if (!device_tree_property_available(node, property_marker)) return nullptr;
					if (args && args->on_segment) {
						auto* handle = device_tree_set_handle(args->handle_buffer, node, property_marker);
						if (!handle) return nullptr;
						char prop_name[VFS_MAX_FILENAME];
						stduint copy_len = seg_len >= VFS_MAX_FILENAME ? VFS_MAX_FILENAME - 1 : seg_len;
						MemCopyN(prop_name, segment, copy_len);
						prop_name[copy_len] = '\0';
						if (!args->on_segment(handle, prop_name, 0, 0, args->user_data)) return handle;
					}
					return device_tree_set_handle(args ? args->handle_buffer : nullptr, node, property_marker);
				}
				if (first_segment &&
					node->link.addr &&
					StrCompareN(node->link.addr, segment, seg_len) == 0 &&
					node->link.addr[seg_len] == '\0') {
					// "/system-root" addresses the tree root object itself.
				}
				else {
					node = device_tree_find_child(node, segment, seg_len);
					if (!node) return nullptr;
				}
				if (args && args->on_segment) {
					auto* handle = device_tree_set_handle(args->handle_buffer, node, 0);
					if (!handle) return nullptr;
					char seg_name[VFS_MAX_FILENAME];
					stduint copy_len = seg_len >= VFS_MAX_FILENAME ? VFS_MAX_FILENAME - 1 : seg_len;
					MemCopyN(seg_name, segment, copy_len);
					seg_name[copy_len] = '\0';
					if (!args->on_segment(handle, seg_name, 1, 0, args->user_data)) return handle;
				}
				if (*slash == '\0') break;
				segment = slash + 1;
				first_segment = false;
			}
			return device_tree_set_handle(args ? args->handle_buffer : nullptr, node, 0);
		}
		virtual bool proper(void* handler, stduint cmd, const void* moreinfo = 0) override {
			if (cmd == (stduint)FilesysCmd::FS_CMD_GET_ISDIR) {
				if (auto* p_isdir = (bool*)moreinfo) {
					*p_isdir = handler != nullptr && !device_tree_handle_is_property(handler);
				}
				return true;
			}
			if (cmd == (stduint)FilesysCmd::FS_CMD_GET_SIZE) {
				if (auto* p_size = (stduint*)moreinfo) {
					if (device_tree_handle_is_property(handler)) {
						char text[128];
						*p_size = device_tree_property_text(
							device_tree_handle_node(handler),
							reinterpret_cast<DeviceTreeHandle*>(handler)->marker,
							text, sizeof(text));
					}
					else {
						*p_size = 0;
					}
				}
				return true;
			}
			return false;
		}
		virtual bool enumer(void* dir_handler, _tocall_ft _fn, FilesysEnumState* state = nullptr) override {
			if (!_fn) return false;
			if (state && (state->finished || state->full())) return true;
			if (device_tree_handle_is_root(dir_handler)) {
				if (auto* root = Devsman::Root()) {
					for (auto* child = reinterpret_cast<DeviceNode*>(root->link.subf);
						child; child = reinterpret_cast<DeviceNode*>(child->link.next)) {
						if (!child->link.addr) continue;
						if (!vfs_enum_emit(state, _fn, (void*)1, (void*)child->link.addr)) break;
					}
				}
				if (state && !state->full()) state->mark_finished();
				return true;
			}
			auto* node = device_tree_handle_node(dir_handler);
			if (!node) return false;
			for (auto* child = reinterpret_cast<DeviceNode*>(node->link.subf);
				child; child = reinterpret_cast<DeviceNode*>(child->link.next)) {
				if (!child->link.addr) continue;
				if (!vfs_enum_emit(state, _fn, (void*)1, (void*)child->link.addr)) return true;
			}
			if (device_tree_property_available(node, kDeviceTreeNodeTypeMarker) &&
				!vfs_enum_emit(state, _fn, (void*)0, (void*)kDeviceTreeNodeTypeName)) return true;
			if (device_tree_property_available(node, kDeviceTreeNodeNameMarker) &&
				!vfs_enum_emit(state, _fn, (void*)0, (void*)kDeviceTreeNodeNameName)) return true;
			if (device_tree_property_available(node, kDeviceTreeDriverNameMarker) &&
				!vfs_enum_emit(state, _fn, (void*)0, (void*)kDeviceTreeDriverNameName)) return true;
			if (device_tree_property_available(node, kDeviceTreeBindingStateMarker) &&
				!vfs_enum_emit(state, _fn, (void*)0, (void*)kDeviceTreeBindingStateName)) return true;
			if (device_tree_property_available(node, kDeviceTreeBlockSizeMarker) &&
				!vfs_enum_emit(state, _fn, (void*)0, (void*)kDeviceTreeBlockSizeName)) return true;
			if (device_tree_property_available(node, kDeviceTreeUnitCountMarker) &&
				!vfs_enum_emit(state, _fn, (void*)0, (void*)kDeviceTreeUnitCountName)) return true;
			if (device_tree_property_available(node, kDeviceTreeByteSizeMarker) &&
				!vfs_enum_emit(state, _fn, (void*)0, (void*)kDeviceTreeByteSizeName)) return true;
			if (device_tree_property_available(node, kDeviceTreeMountCountMarker) &&
				!vfs_enum_emit(state, _fn, (void*)0, (void*)kDeviceTreeMountCountName)) return true;
			if (device_tree_property_available(node, kDeviceTreeMountPathMarker) &&
				!vfs_enum_emit(state, _fn, (void*)0, (void*)kDeviceTreeMountPathName)) return true;
			if (state) state->mark_finished();
			return true;
		}
		virtual stduint readfl(void* fil_handler, Slice file_slice, byte* dst) override {
			if (!device_tree_handle_is_property(fil_handler) || !dst) return 0;
			char text[128];
			stduint full_len = device_tree_property_text(
				device_tree_handle_node(fil_handler),
				reinterpret_cast<DeviceTreeHandle*>(fil_handler)->marker,
				text, sizeof(text));
			if (file_slice.address >= full_len) return 0;
			stduint remain = full_len - file_slice.address;
			stduint copy_len = file_slice.length < remain ? file_slice.length : remain;
			MemCopyN(dst, text + file_slice.address, copy_len);
			return copy_len;
		}
		virtual stduint writfl(void* fil_handler, Slice file_slice, const byte* src) override { return 0; }
	};

	static RootFs global_rootfs_driver;
	static DeviceTreeFs global_devtreefs_driver;
	alignas(16) byte buf_root_sb[sizeof(vfs_super_block)];
}
extern file_system_type fs_fat;
extern file_system_type fs_iso9660;
extern file_system_type fs_udf;


void Filesys::Initialize() {

	Filesys::Register(&fs_fat);
	Filesys::Register(&fs_iso9660);
	Filesys::Register(&fs_udf);

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

	Filesys::MountFilesys(&global_devtreefs_driver, nullptr, "/.dev");
}

void Filesys::Register(file_system_type* fs_type) {
	KASSERT(fs_type != nullptr);
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
	MutexLocal guard(&vfs_lock);
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
	MutexLocal guard(&vfs_lock);
	return _Create_unlocked(pathname, mode, base);
}


bool Filesys::MountFilesys(FilesysTrait* fs, file_system_type* type, const char* target_path,
	DeviceNode* source_device_node, stduint device_id) {
	MutexLocal guard(&vfs_lock);
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
	sb->device_id = device_id;
	sb->source_device_node = source_device_node;
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
	MutexLocal guard(&vfs_lock);
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


file_system_type* Filesys::Mount(StorageTrait& storage, stduint dev, const char* target_path,
	DeviceNode* source_device_node) {
	for (file_system_type* fs_type = registered_filesystems; fs_type; fs_type = fs_type->next) {
		// probe() checks sys_id and calls loadfs() internally; non-null means ready to mount
		FilesysTrait* fs = fs_type->probe(storage, dev);
		if (fs) {
			return Filesys::MountFilesys(fs, fs_type, target_path, source_device_node, dev) ? fs_type : nullptr;
		}
	}
	return nullptr;
}

struct PathHarvest {
	char name[256] = {};
	bool is_dir = false;
	PathHarvest* next = nullptr;
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
	PathHarvest* n = new PathHarvest();
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

void vfs_tree_node(uni::OstreamTrait& os, vfs_dentry* node, int depth, bool mount_expand) {
	if (!node) return;
	
	for (int i = 0; i < depth; i++) {
		os.OutFormat("  ");
	}
	
	os.OutFormat("|- %s", node->d_name[0] ? node->d_name : "/");
	if (node->d_mounts) {
		const char* type_name = "unknown/internal";
		if (node->d_mounts->d_inode && node->d_mounts->d_inode->i_sb && node->d_mounts->d_inode->i_sb->type) {
			type_name = node->d_mounts->d_inode->i_sb->type->name;
		}
		os.OutFormat(" (Mount: %s)\n\r", type_name);
		// Recurse into physical filesystem if mounted
		if (mount_expand && node->d_mounts->d_inode && node->d_mounts->d_inode->i_sb && node->d_mounts->d_inode->i_sb->fs) {
			vfs_tree_physical(node->d_mounts->d_inode->i_sb->fs, "/", depth + 1);
		}

		vfs_tree_node(os, node->d_mounts->d_first_child, depth + 1, mount_expand);
	} else {
		os.OutFormat("\n\r");
		vfs_tree_node(os, node->d_first_child, depth + 1, mount_expand);
	}
	
	vfs_tree_node(os, node->d_next_sibling, depth, mount_expand);
}

void Filesys::Tree(uni::OstreamTrait& os, bool mount_expand) {
	MutexLocal guard(&vfs_lock);
	os.OutFormat("VFS Virtual Tree Structure:\n\r");
	vfs_tree_node(os, vfs_root, 0, mount_expand);
}

int Filesys::Open(const char* pathname, int flags, vfs_file** out_file, vfs_dentry* base) {
	MutexLocal guard(&vfs_lock);
	vfs_dentry* dentry = _Index_unlocked(pathname, base);
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
				parent_dentry = _Index_unlocked(dir_path, base);
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
	if (!file || !file->f_inode) return -1;
	if ((file->f_inode->i_mode & I_TYPE_MASK) == I_NAMED_PIPE) {
		return Filesys::ReadPipe(file, buf, count);
	}
	if ((file->f_inode->i_mode & I_TYPE_MASK) == I_SOCK) {
		auto* socket = Filesys::GetSocket(file);
		if (!socket || !socket->is_connected) return -1;
		const stduint io_flags = (file->f_mode & O_NONBLOCK) ? 0 : syscall_net_io_flag_wait;
		return Filesys::RecvSocket(file, buf, count, nullptr, nullptr, io_flags);
	}
	if (!file->f_inode->i_sb) return -1;
	MutexLocal guard(&vfs_lock);
	FilesysTrait* fs = file->f_inode->i_sb->fs;
	
	stduint bytes = fs->readfl(file->f_inode->internal_handler, Slice{ file->f_pos, count }, (byte*)buf);
	file->f_pos += bytes;
	return bytes;
}

int Filesys::Write(vfs_file* file, const void* buf, stduint count) {
	if (!file || !file->f_inode) return -1;
	if ((file->f_inode->i_mode & I_TYPE_MASK) == I_NAMED_PIPE) {
		return Filesys::WritePipe(file, buf, count);
	}
	if ((file->f_inode->i_mode & I_TYPE_MASK) == I_SOCK) {
		auto* socket = Filesys::GetSocket(file);
		if (!socket || !socket->is_connected) return -1;
		return Filesys::SendSocket(file, buf, count, nullptr);
	}
	if (!file->f_inode->i_sb) return -1;
	MutexLocal guard(&vfs_lock);
	FilesysTrait* fs = file->f_inode->i_sb->fs;

	stduint bytes = fs->writfl(file->f_inode->internal_handler, Slice{ file->f_pos, count }, (const byte*)buf);
	file->f_pos += bytes;
	if (file->f_pos > file->f_inode->i_size) {
		file->f_inode->i_size = file->f_pos;
	}
	return bytes;
}

int Filesys::Close(vfs_file* file) {
	if (!file || !file->f_inode) return -1;
	if ((file->f_inode->i_mode & I_TYPE_MASK) == I_NAMED_PIPE) {
		return Filesys::ClosePipe(file);
	}
	if ((file->f_inode->i_mode & I_TYPE_MASK) == I_SOCK) {
		return Filesys::CloseSocket(file);
	}
	MutexLocal guard(&vfs_lock);
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
	if (count == 0) return 0;
	MutexLocal guard(&vfs_lock);
	FilesysTrait* fs = file->f_inode->i_sb->fs;
	file->f_enum_state.begin(count);

	g_enum_cxt.pb = pb;
	g_enum_cxt.user_addr = buf;
	g_enum_cxt.max_count = count;
	g_enum_cxt.skip_count = 0;
	g_enum_cxt.current_idx = 0;
	g_enum_cxt.filled_count = 0;

	fs->enumer(file->f_inode->internal_handler, (_tocall_ft)user_enumer_callback, &file->f_enum_state);

	file->f_pos = file->f_enum_state.position;
	return g_enum_cxt.filled_count;
}

bool Filesys::Remove(const char* pathname, vfs_dentry* base) {
	MutexLocal guard(&vfs_lock);
	vfs_dentry* dentry = _Index_unlocked(pathname, base);
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

static uint32 _tty_id_bits[8] = {};
static Bitmap tty_id_allocator(&_tty_id_bits, 4 * 8);

int DevFs::allocate_tty_id() {
	for (int i = 0; i < 32; i++) {
		if (!tty_id_allocator.bitof(i)) {
			tty_id_allocator.setof(i, true);
			return i;
		}
	}
	plogerro("[TTYID] allocate failed bits=%[x] offs=%[x]", _tty_id_bits, tty_id_allocator.offs);
	return -1;
}

void DevFs::free_tty_id(int id) {
	if (id < 0 || id >= bitsof(_tty_id_bits)) {
		plogerro("[TTYID] free invalid id=%d (0x%[x]) bits=%[x] offs=%[x]",
			id, (stduint)id, _tty_id_bits, tty_id_allocator.offs);
	}
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

bool DevFs::enumer(void* dir_handler, _tocall_ft _fn, FilesysEnumState* state) {
	if (!_fn) return false;
	if (state && (state->finished || state->full())) return true;
	if (dir_handler == (void*)~0 || dir_handler == nullptr) { // Root of /dev
		if (!uni::vfs_enum_emit(state, _fn, (void*)0, (void*)"tty")) return true;
		if (!uni::vfs_enum_emit(state, _fn, (void*)1, (void*)"pts")) return true;
		if (state) state->mark_finished();
		return true;
	}
	if (dir_handler == (void*)0x1000) { // /dev/pts
		for (auto nod = vttys.Root(); nod; nod = nod->next) {
			char name[16];
			String(name, 16).Format("%u", ((vtty_type_t*)nod->type)->id);
			if (!uni::vfs_enum_emit(state, _fn, (void*)0, (void*)name)) return true;
		}
		if (state) state->mark_finished();
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
#include <c/format/filesys/CD.h>

file_system_type fs_udf = { "udf", [](StorageTrait& storage, stduint dev) -> FilesysTrait* {
		DiscPartition part(storage, dev);
		if (part.Block_Size != 2048) {
			return nullptr;
		}
		ploginfo("[UDF] probe dev=%u block=%u", dev, part.Block_Size);
		PartitionSlice slice = part.getSlice();
		ploginfo("[UDF] slice addr=%u len=%u type=%x", slice.address, slice.length, slice.sys_id);
		if (slice.length == 0) {
			ploginfo("[UDF] skip: empty slice");
			return nullptr;
		}

		DiscPartition* p_part = new DiscPartition(storage, dev);
		p_part->getSlice();
		byte* sec_buf = new byte[p_part->Block_Size];
		FilesysUDF* fs = new FilesysUDF(*p_part, sec_buf);

		if (fs->loadfs()) {
			return fs;
		}

		delete fs;
		delete p_part;
		return nullptr;
	},
	nullptr
};

file_system_type fs_iso9660 = { "iso9660", [](StorageTrait& storage, stduint dev) -> FilesysTrait* {
		DiscPartition part(storage, dev);
		if (part.Block_Size != 2048) {
			return nullptr;
		}
		ploginfo("[ISO9660] probe dev=%u block=%u", dev, part.Block_Size);
		PartitionSlice slice = part.getSlice();
		ploginfo("[ISO9660] slice addr=%u len=%u type=%x", slice.address, slice.length, slice.sys_id);
		if (slice.length == 0) {
			ploginfo("[ISO9660] skip: empty slice");
			return nullptr;
		}

		DiscPartition* p_part = new DiscPartition(storage, dev);
		p_part->getSlice();
		byte* sec_buf = new byte[p_part->Block_Size];
		FilesysISO9660* fs = new FilesysISO9660(*p_part, sec_buf);

		if (fs->loadfs()) {
			return fs;
		}

		delete fs;
		delete p_part;
		return nullptr;
	},
	nullptr
};

file_system_type fs_fat = { "fat", [](StorageTrait& storage, stduint dev) -> FilesysTrait* {
		DiscPartition part(storage, dev);
		if (part.getSlice().length == 0) return nullptr;
		byte sys_id = part.getSlice().sys_id;
		auto try_fat = [&](int fat_ver) -> FilesysTrait* {
			DiscPartition* p_part = new DiscPartition(storage, dev);
			p_part->getSlice(); // initialize internal slice

			stduint sec_size = p_part->Block_Size > 0 ? p_part->Block_Size : 512;
			byte* fat_sec_buf = new byte[sec_size];
			byte* fat_buf = new byte[0x1000];
			FilesysFAT* fs = new FilesysFAT(fat_ver, *p_part, fat_sec_buf, fat_buf);
			fs->allow_allocate = true;

			if (fs->loadfs()) {
				return fs;
			}

			delete fs;
			delete p_part;
			delete[] fat_buf;
			delete[] fat_sec_buf;
			return nullptr;
		};

		// Prefer explicit type declaration. ESP is allowed to directly try FAT.
		if (sys_id == FILESYS_FAT12) return try_fat(12);
		if (sys_id == FILESYS_FAT16_CHS || sys_id == FILESYS_FAT16_CHSX || sys_id == FILESYS_FAT16_LBA) return try_fat(16);
		if (sys_id == FILESYS_FAT32_CHS || sys_id == FILESYS_FAT32_LBA) return try_fat(32);
		if (sys_id == FILESYS_EFI_SYS) {
			if (auto fs = try_fat(32)) return fs;
			if (auto fs = try_fat(16)) return fs;
			if (auto fs = try_fat(12)) return fs;
		}
		return nullptr;
	},
	nullptr
};


// ---- VFS Pipe Implementation ----

extern "C" stduint sys_kill(stduint pid, int sig, stduint tid);

int Filesys::CreatePipe(vfs_file** out_reader, vfs_file** out_writer) {
	MutexLocal guard(&vfs_lock);
	
	// Allocate PipeChannel on heap
	PipeChannel* chan = new PipeChannel();
	if (!chan) return -1;
	
	// Allocate a 4KB memory buffer for the ring buffer
	char* buf = new char[4096];
	if (!buf) {
		delete chan;
		return -1;
	}
	chan->buffer = QueueLimited(Slice{ (stduint)buf, 4096 });
	chan->reader_count = 1;
	chan->writer_count = 1;
	
	// Allocate VFS Inode for the pipe
	vfs_inode* inode = alloc_inode(nullptr);
	if (!inode) {
		delete[] buf;
		delete chan;
		return -1;
	}
	inode->i_mode = I_NAMED_PIPE;
	inode->internal_handler = chan;
	inode->ref_count = 2; // For reader and writer
	inode->i_size = 0;
	
	// Allocate vfs_file for reader
	vfs_file* file_r = new vfs_file();
	if (!file_r) {
		delete inode;
		delete[] buf;
		delete chan;
		return -1;
	}
	file_r->f_dentry = nullptr;
	file_r->f_inode = inode;
	file_r->f_pos = 0;
	file_r->f_mode = O_RDONLY;
	
	// Allocate vfs_file for writer
	vfs_file* file_w = new vfs_file();
	if (!file_w) {
		delete file_r;
		delete inode;
		delete[] buf;
		delete chan;
		return -1;
	}
	file_w->f_dentry = nullptr;
	file_w->f_inode = inode;
	file_w->f_pos = 0;
	file_w->f_mode = O_WRONLY;
	
	*out_reader = file_r;
	*out_writer = file_w;
	return 0;
}

int Filesys::CreateSocket(vfs_file** out_file, Network::SocketDomain domain,
	Network::SocketType type, Network::SocketProtocol protocol) {
	if (!out_file) return -1;
	MutexLocal guard(&vfs_lock);

	SocketHandle* socket = new SocketHandle();
	if (!socket) return -1;
	socket->domain = domain;
	socket->type = type;
	socket->protocol = protocol;

	vfs_inode* inode = alloc_inode(nullptr);
	if (!inode) {
		delete socket;
		return -1;
	}
	inode->i_mode = I_SOCK;
	inode->internal_handler = socket;
	inode->ref_count = 1;
	inode->i_size = 0;

	vfs_file* file = new vfs_file();
	if (!file) {
		delete inode;
		delete socket;
		return -1;
	}
	file->f_dentry = nullptr;
	file->f_inode = inode;
	file->f_pos = 0;
	file->f_mode = 0;

	*out_file = file;
	return 0;
}

SocketHandle* Filesys::GetSocket(vfs_file* file) {
	if (!file || !file->f_inode) return nullptr;
	if ((file->f_inode->i_mode & I_TYPE_MASK) != I_SOCK) return nullptr;
	return reinterpret_cast<SocketHandle*>(file->f_inode->internal_handler);
}

int Filesys::BindSocket(vfs_file* file, const Network::SocketAddress& address) {
	SocketHandle* socket = Filesys::GetSocket(file);
	if (!socket || socket->is_bound) return -1;
	if (socket->domain != Network::SocketDomain::IPv4) return -1;
	if (address.domain != uint16(Network::SocketDomain::IPv4)) return -1;
	if (address.length < sizeof(Network::SocketAddressIPv4)) return -1;

	const auto& ipv4 = reinterpret_cast<const Network::SocketAddressIPv4&>(address);
	if (!ipv4.port) return -1;
	if (!IsLocalBindIPv4AddressAllowed(ipv4.address)) return -1;
	#if (_MCCA & 0xFF00) != 0x8600
	(void)ipv4;
	return -1;
	#else
	if (socket->type == Network::SocketType::Stream) {
		if (socket->protocol != Network::SocketProtocol::Default &&
			socket->protocol != Network::SocketProtocol::TCP) return -1;
		socket->local_ipv4.address = ipv4.address;
		socket->local_ipv4.port = ipv4.port;
		socket->flags &= ~SocketHandleFlagLocalAddressAuto;
		socket->protocol = Network::SocketProtocol::TCP;
		socket->is_bound = true;
		return 0;
	}
	if (socket->type != Network::SocketType::Datagram) return -1;
	if (socket->protocol != Network::SocketProtocol::Default &&
		socket->protocol != Network::SocketProtocol::UDP) return -1;
	stduint inbox_id = stduint(-1);
	const bool reuse_address = (socket->flags & SocketHandleFlagReuseAddress) != 0;
	if (!Devsman::BindUdpPort(ipv4.port, reuse_address, inbox_id)) return -1;

	socket->local_ipv4.address = ipv4.address;
	socket->local_ipv4.port = ipv4.port;
	socket->udp_inbox_id = inbox_id;
	socket->flags &= ~SocketHandleFlagLocalAddressAuto;
	socket->protocol = Network::SocketProtocol::UDP;
	socket->is_bound = true;
	return 0;
	#endif
}

int Filesys::ListenSocket(vfs_file* file, stduint backlog) {
	SocketHandle* socket = Filesys::GetSocket(file);
	if (!socket || !socket->is_bound || socket->is_connected || socket->is_listening) return -1;
	if (socket->domain != Network::SocketDomain::IPv4) return -1;
	if (socket->type != Network::SocketType::Stream) return -1;
	if (socket->protocol != Network::SocketProtocol::Default &&
		socket->protocol != Network::SocketProtocol::TCP) return -1;
	if (!socket->local_ipv4.port) return -1;

	#if (_MCCA & 0xFF00) != 0x8600
	(void)backlog;
	return -1;
	#else
	if (!Devsman::ListenTcpPort(socket->local_ipv4.port, backlog)) return -1;
	socket->protocol = Network::SocketProtocol::TCP;
	socket->is_listening = true;
	return 0;
	#endif
}

int Filesys::AcceptSocket(vfs_file* file, vfs_file** out_file,
	Network::SocketAddress* address, stduint* address_length) {
	if (!out_file) return -1;
	*out_file = nullptr;
	SocketHandle* socket = Filesys::GetSocket(file);
	if (!socket || !socket->is_bound || !socket->is_listening) return -1;
	if (socket->domain != Network::SocketDomain::IPv4) return -1;
	if (socket->type != Network::SocketType::Stream) return -1;
	if (socket->protocol != Network::SocketProtocol::TCP &&
		socket->protocol != Network::SocketProtocol::Default) return -1;

	#if (_MCCA & 0xFF00) != 0x8600
	(void)address;
	(void)address_length;
	return -1;
	#else
	Network::TCPConnectionContext context{};
	stdsint accepted = Devsman::AcceptTcpConnection(socket->local_ipv4.port, context);
	if (accepted == 0 && !(file->f_mode & O_NONBLOCK)) {
		if (!Devsman::WaitTcpAccept(socket->local_ipv4.port)) return -1;
		accepted = Devsman::AcceptTcpConnection(socket->local_ipv4.port, context);
	}
	if (accepted <= 0) return -1;

	vfs_file* accepted_file = nullptr;
	if (Filesys::CreateSocket(&accepted_file, Network::SocketDomain::IPv4,
		Network::SocketType::Stream, Network::SocketProtocol::TCP) < 0 || !accepted_file) {
		return -1;
	}
	SocketHandle* accepted_socket = Filesys::GetSocket(accepted_file);
	if (!accepted_socket) {
		Filesys::Close(accepted_file);
		return -1;
	}
	accepted_socket->local_ipv4 = { context.local.address, context.local.port };
	accepted_socket->remote_ipv4 = { context.remote.address, context.remote.port };
	accepted_socket->protocol = Network::SocketProtocol::TCP;
	accepted_socket->is_bound = true;
	accepted_socket->is_connected = true;

	if (address && address_length && *address_length >= sizeof(Network::SocketAddressIPv4)) {
		auto* ipv4 = reinterpret_cast<Network::SocketAddressIPv4*>(address);
		Network::SocketWriteAddress(*ipv4, context.remote.address, context.remote.port);
		*address_length = sizeof(Network::SocketAddressIPv4);
	}
	*out_file = accepted_file;
	return 1;
	#endif
}

int Filesys::ConnectSocket(vfs_file* file, const Network::SocketAddress& address) {
	SocketHandle* socket = Filesys::GetSocket(file);
	if (!socket) return -1;
	if (socket->domain != Network::SocketDomain::IPv4) return -1;
	if (address.domain != uint16(Network::SocketDomain::IPv4)) return -1;
	if (address.length < sizeof(Network::SocketAddressIPv4)) return -1;

	const auto& target = reinterpret_cast<const Network::SocketAddressIPv4&>(address);
	if (!target.port || target.address.isZero()) return -1;

	#if (_MCCA & 0xFF00) != 0x8600
	(void)target;
	return -1;
	#else
	if (socket->type == Network::SocketType::Stream) {
		if (socket->protocol != Network::SocketProtocol::Default &&
			socket->protocol != Network::SocketProtocol::TCP) return -1;
		if (socket->is_connected || socket->is_listening) return -1;
		uint16 local_port = socket->is_bound ? socket->local_ipv4.port : 0;
		Network::TCPConnectionContext context{};
		const stdsint connected = Devsman::ConnectTcp(target.address, target.port, local_port, context);
		if (connected <= 0) return -1;
		socket->local_ipv4.address = context.local.address;
		socket->local_ipv4.port = context.local.port;
		socket->remote_ipv4.address = context.remote.address;
		socket->remote_ipv4.port = context.remote.port;
		if (!socket->is_bound) socket->flags |= SocketHandleFlagLocalAddressAuto;
		socket->protocol = Network::SocketProtocol::TCP;
		socket->is_bound = true;
		socket->is_connected = true;
		return 0;
	}
	if (socket->type != Network::SocketType::Datagram) return -1;
	if (socket->protocol != Network::SocketProtocol::Default &&
		socket->protocol != Network::SocketProtocol::UDP) return -1;
	if (!socket->is_bound) {
		uint16 local_port = 0;
		stduint inbox_id = stduint(-1);
		if (!Devsman::AllocateUdpPort(local_port, inbox_id)) return -1;
		socket->local_ipv4.port = local_port;
		socket->udp_inbox_id = inbox_id;
		socket->flags |= SocketHandleFlagLocalAddressAuto;
		socket->protocol = Network::SocketProtocol::UDP;
		socket->is_bound = true;
	}

	socket->remote_ipv4.address = target.address;
	socket->remote_ipv4.port = target.port;
	socket->protocol = Network::SocketProtocol::UDP;
	socket->is_connected = true;
	return 0;
	#endif
}

int Filesys::SendSocket(vfs_file* file, const void* payload, stduint length, const Network::SocketAddress* address) {
	SocketHandle* socket = Filesys::GetSocket(file);
	if (!socket) return -1;
	if (socket->domain != Network::SocketDomain::IPv4) return -1;
	if (socket->type == Network::SocketType::Stream) {
		if (address) return -1;
		if (!socket->is_connected) return -1;
		if (socket->protocol != Network::SocketProtocol::TCP &&
			socket->protocol != Network::SocketProtocol::Default) return -1;
		#if (_MCCA & 0xFF00) != 0x8600
		(void)payload;
		(void)length;
		return -1;
		#else
		Network::TCPConnectionContext context{
			{ socket->local_ipv4.address, socket->local_ipv4.port },
			{ socket->remote_ipv4.address, socket->remote_ipv4.port },
		};
		return Devsman::SendTcp(context, payload, length);
		#endif
	}
	if (socket->type != Network::SocketType::Datagram) return -1;
	if (socket->protocol != Network::SocketProtocol::Default &&
		socket->protocol != Network::SocketProtocol::UDP) return -1;

	Network::SocketAddressIPv4 target = {};
	if (address) {
		if (address->domain != uint16(Network::SocketDomain::IPv4)) return -1;
		if (address->length < sizeof(Network::SocketAddressIPv4)) return -1;
		target = reinterpret_cast<const Network::SocketAddressIPv4&>(*address);
	}
	else {
		if (!socket->is_connected) return -1;
		Network::SocketWriteAddress(target, socket->remote_ipv4.address, socket->remote_ipv4.port);
	}
	if (!target.port || target.address.isZero()) return -1;

	#if (_MCCA & 0xFF00) != 0x8600
	(void)payload;
	(void)length;
	return -1;
	#else
	if (!socket->is_bound) {
		uint16 local_port = 0;
		stduint inbox_id = stduint(-1);
		if (!Devsman::AllocateUdpPort(local_port, inbox_id)) return -1;
		socket->local_ipv4.port = local_port;
		socket->udp_inbox_id = inbox_id;
		socket->flags |= SocketHandleFlagLocalAddressAuto;
		socket->protocol = Network::SocketProtocol::UDP;
		socket->is_bound = true;
	}

	const stdsint sent = Devsman::SendUdp(target.address,
		socket->local_ipv4.port, target.port, payload, length);
	return sent;
	#endif
}

int Filesys::RecvSocket(vfs_file* file, void* payload, stduint capacity,
	Network::SocketAddress* address, stduint* address_length, stduint flags) {
	SocketHandle* socket = Filesys::GetSocket(file);
	if (!socket || !socket->is_bound) return -1;
	if (socket->domain != Network::SocketDomain::IPv4) return -1;
	if (socket->type == Network::SocketType::Stream) {
		if (!socket->is_connected) return -1;
		if (socket->protocol != Network::SocketProtocol::TCP &&
			socket->protocol != Network::SocketProtocol::Default) return -1;
		#if (_MCCA & 0xFF00) != 0x8600
		(void)payload;
		(void)capacity;
		(void)address;
		(void)address_length;
		return -1;
		#else
		Network::TCPConnectionContext context{
			{ socket->local_ipv4.address, socket->local_ipv4.port },
			{ socket->remote_ipv4.address, socket->remote_ipv4.port },
		};
		if (file->f_mode & O_NONBLOCK) flags &= ~syscall_net_io_flag_wait;
		stdsint received = Devsman::ReceiveTcp(context, payload, capacity);
		if (received == 0 && (flags & syscall_net_io_flag_wait)) {
			if (!Devsman::WaitTcpReceive(context)) return 0;
			received = Devsman::ReceiveTcp(context, payload, capacity);
		}
		(void)address;
		(void)address_length;
		return received;
		#endif
	}
	if (socket->type != Network::SocketType::Datagram) return -1;
	if (socket->protocol != Network::SocketProtocol::UDP &&
		socket->protocol != Network::SocketProtocol::Default) return -1;

	#if (_MCCA & 0xFF00) != 0x8600
	(void)payload;
	(void)capacity;
	(void)address;
	(void)address_length;
	return -1;
	#else
	Network::UDPDatagramContext context{};
	if (file->f_mode & O_NONBLOCK) flags &= ~syscall_net_io_flag_wait;
	stdsint received = 0;
	for (;;) {
		received = Devsman::ReceiveUdp(socket->local_ipv4.port,
			socket->udp_inbox_id, context, payload, capacity);
		if (received > 0 && socket->is_connected &&
			!(context.source.address == socket->remote_ipv4.address &&
				context.source.port == socket->remote_ipv4.port)) {
			continue;
		}
		if (received != 0) break;
		if (!(flags & syscall_net_io_flag_wait)) break;
		if (!Devsman::WaitUdp(socket->local_ipv4.port, socket->udp_inbox_id)) return 0;
	}
	if (received <= 0) return received;

	if (address && address_length && *address_length >= sizeof(Network::SocketAddressIPv4)) {
		auto* ipv4 = reinterpret_cast<Network::SocketAddressIPv4*>(address);
		Network::SocketWriteAddress(*ipv4, context.source.address, context.source.port);
		*address_length = sizeof(Network::SocketAddressIPv4);
	}
	return received;
	#endif
}

int Filesys::Poll(vfs_file* file, stduint events, stduint* revents) {
	if (!revents) return -1;
	*revents = 0;
	if (!file || !file->f_inode) return -1;
	const stduint type = file->f_inode->i_mode & I_TYPE_MASK;

	if (type == I_NAMED_PIPE) {
		PipeChannel* chan = (PipeChannel*)file->f_inode->internal_handler;
		if (!chan) return -1;
		chan->lock.Acquire();
		if ((events & syscall_poll_in) && (!chan->buffer.is_empty() || chan->writer_count == 0)) {
			*revents |= syscall_poll_in;
		}
		if ((events & syscall_poll_out) && !chan->buffer.is_full() && chan->reader_count != 0) {
			*revents |= syscall_poll_out;
		}
		if (chan->writer_count == 0) *revents |= syscall_poll_hangup;
		if (chan->reader_count == 0) *revents |= syscall_poll_error;
		chan->lock.Release();
		return 0;
	}

	if (type != I_SOCK) {
		const int access = file->f_mode & O_ACCMODE;
		if ((events & syscall_poll_in) && access != O_WRONLY) *revents |= syscall_poll_in;
		if ((events & syscall_poll_out) && access != O_RDONLY) *revents |= syscall_poll_out;
		return 0;
	}

	SocketHandle* socket = Filesys::GetSocket(file);
	if (!socket) return -1;
	if (socket->domain != Network::SocketDomain::IPv4) return -1;
	if (socket->type == Network::SocketType::Stream) {
		if (socket->protocol != Network::SocketProtocol::TCP &&
			socket->protocol != Network::SocketProtocol::Default) return -1;
		#if (_MCCA & 0xFF00) != 0x8600
		(void)events;
		return -1;
		#else
		if ((events & syscall_poll_in) && socket->is_listening &&
			Devsman::HasTcpAccept(socket->local_ipv4.port)) {
			*revents |= syscall_poll_in;
		}
		if ((events & syscall_poll_in) && socket->is_connected) {
			Network::TCPConnectionContext context{
				{ socket->local_ipv4.address, socket->local_ipv4.port },
				{ socket->remote_ipv4.address, socket->remote_ipv4.port },
			};
			if (Devsman::HasTcpError(context)) {
				*revents |= syscall_poll_error | syscall_poll_hangup;
				return 0;
			}
			if (Devsman::HasTcpReceive(context)) *revents |= syscall_poll_in;
			if (Devsman::IsTcpReceiveClosed(context)) *revents |= syscall_poll_hangup;
		}
		if ((events & syscall_poll_out) && socket->is_connected) {
			Network::TCPConnectionContext context{
				{ socket->local_ipv4.address, socket->local_ipv4.port },
				{ socket->remote_ipv4.address, socket->remote_ipv4.port },
			};
			if (Devsman::HasTcpError(context)) {
				*revents |= syscall_poll_error | syscall_poll_hangup;
				return 0;
			}
			if (Devsman::HasTcpSendSpace(context)) *revents |= syscall_poll_out;
		}
		return 0;
		#endif
	}
	if (socket->type != Network::SocketType::Datagram) return -1;
	if (socket->protocol != Network::SocketProtocol::UDP &&
		socket->protocol != Network::SocketProtocol::Default) return -1;

	#if (_MCCA & 0xFF00) != 0x8600
	(void)events;
	return -1;
	#else
	if ((events & syscall_poll_in) && socket->is_bound &&
		Devsman::HasUdp(socket->local_ipv4.port, socket->udp_inbox_id)) {
		*revents |= syscall_poll_in;
	}
	if (events & syscall_poll_out) {
		*revents |= syscall_poll_out;
	}
	return 0;
	#endif
}

int Filesys::GetSocketAddress(vfs_file* file, bool peer,
	Network::SocketAddress* address, stduint* address_length) {
	SocketHandle* socket = Filesys::GetSocket(file);
	if (!socket || !address || !address_length) return -1;
	if (socket->domain != Network::SocketDomain::IPv4) return -1;
	if (*address_length < sizeof(Network::SocketAddressIPv4)) return -1;

	Network::SocketEndpointIPv4 endpoint{};
	if (peer) {
		if (!socket->is_connected) return -1;
		endpoint = socket->remote_ipv4;
	}
	else {
		if (!socket->is_bound) return -1;
		endpoint = socket->local_ipv4;
		#if (_MCCA & 0xFF00) == 0x8600
		if (endpoint.address.isZero() && (socket->flags & SocketHandleFlagLocalAddressAuto)) {
			syscall_net_route_ipv4_t route{};
			syscall_net_interface_ipv4_t iface{};
			if (Devsman::GetDefaultIPv4Route(&route, sizeof(route)) &&
				Devsman::GetIPv4Interface(route.link_index, &iface, sizeof(iface))) {
				for0(i, Network::IPv4AddressLength) endpoint.address.octet[i] = iface.address[i];
			}
		}
		#endif
	}
	if (!endpoint.port) return -1;

	auto* ipv4 = reinterpret_cast<Network::SocketAddressIPv4*>(address);
	Network::SocketWriteAddress(*ipv4, endpoint.address, endpoint.port);
	*address_length = sizeof(Network::SocketAddressIPv4);
	return 0;
}

int Filesys::SetSocketOption(vfs_file* file, stduint level, stduint option_name, int value) {
	SocketHandle* socket = Filesys::GetSocket(file);
	if (!socket) return -1;
	if (level != syscall_net_socket_level_socket) return -1;
	switch (option_name) {
	case syscall_net_socket_option_reuse_address:
		if (socket->is_bound) return -1;
		if (value) socket->flags |= SocketHandleFlagReuseAddress;
		else socket->flags &= ~SocketHandleFlagReuseAddress;
		return 0;
	default:
		return -1;
	}
}

int Filesys::GetSocketOption(vfs_file* file, stduint level, stduint option_name, int* value) {
	SocketHandle* socket = Filesys::GetSocket(file);
	if (!socket || !value) return -1;
	if (level != syscall_net_socket_level_socket) return -1;
	switch (option_name) {
	case syscall_net_socket_option_reuse_address:
		*value = (socket->flags & SocketHandleFlagReuseAddress) ? 1 : 0;
		return 0;
	case syscall_net_socket_option_type:
		*value = int(socket->type);
		return 0;
	case syscall_net_socket_option_error:
		*value = 0;
		#if (_MCCA & 0xFF00) == 0x8600
		if (socket->domain == Network::SocketDomain::IPv4 &&
			socket->type == Network::SocketType::Stream &&
			socket->protocol == Network::SocketProtocol::TCP &&
			socket->is_connected) {
			Network::TCPConnectionContext context{
				{ socket->local_ipv4.address, socket->local_ipv4.port },
				{ socket->remote_ipv4.address, socket->remote_ipv4.port },
			};
			if (Devsman::HasTcpError(context)) *value = ECONNRESET;
		}
		#endif
		return 0;
	default:
		return -1;
	}
}

int Filesys::ReadPipe(vfs_file* file, void* buf, stduint count) {
	PipeChannel* chan = (PipeChannel*)file->f_inode->internal_handler;
	if (!chan) return -1;

	stduint bytes_read = 0;
	byte* dst = (byte*)buf;
	
	while (bytes_read < count) {
		chan->lock.Acquire();
		
		if (chan->buffer.is_empty()) {
			if (chan->writer_count == 0) {
				// EOF
				chan->lock.Release();
				break;
			}
			// Wait blockedly: Block first (set Pended), then release lock, then schedule.
			// This avoids the race where Unblock happens between lock release and Block.
			ThreadBlock* th = Taskman::CurrentTB();
			chan->rq.Enqueue(th);
			th->Block(ThreadBlock::BlockReason::BR_RecvMsg);
			chan->lock.Release();
			Taskman::Schedule(true);
			// Woken up, loop again
			continue;
		}
		
		// Read as much as possible
		while (bytes_read < count && !chan->buffer.is_empty()) {
			int ch = chan->buffer.inn();
			if (ch == -1) break;
			dst[bytes_read++] = (byte)ch;
		}
		
		// Wake up writers (dequeue under lock to avoid corruption, then Unblock outside)
		Queue<::ThreadBlock*> wake_list;
		while (!chan->wq.isEmpty()) {
			ThreadBlock* w_th = nullptr;
			chan->wq.Dequeue(w_th);
			if (w_th) wake_list.Enqueue(w_th);
		}
		chan->lock.Release();
		
		while (!wake_list.isEmpty()) {
			ThreadBlock* w_th = nullptr;
			wake_list.Dequeue(w_th);
			if (w_th) {
				w_th->Unblock(ThreadBlock::BlockReason::BR_SendMsg);
			}
		}
	}
	
	return bytes_read;
}

int Filesys::WritePipe(vfs_file* file, const void* buf, stduint count) {
	PipeChannel* chan = (PipeChannel*)file->f_inode->internal_handler;
	if (!chan) return -1;

	stduint bytes_written = 0;
	const byte* src = (const byte*)buf;
	
	while (bytes_written < count) {
		chan->lock.Acquire();
		
		if (chan->reader_count == 0) {
			// POSIX: write to pipe with no readers -> SIGPIPE
			chan->lock.Release();
			// Send SIGPIPE to current process
			ThreadBlock* th = Taskman::CurrentTB();
			sys_kill(th->parent_process->pid, SIGPIPE, 0);
			return -1; // -EPIPE
		}
		
		if (chan->buffer.is_full()) {
			// Wait blockedly: Block first (set Pended), then release lock, then schedule.
			ThreadBlock* th = Taskman::CurrentTB();
			chan->wq.Enqueue(th);
			th->Block(ThreadBlock::BlockReason::BR_SendMsg);
			chan->lock.Release();
			Taskman::Schedule(true);
			
			continue;
		}
		
		// Write as much as possible
		stduint chunk = count - bytes_written;
		int written = chan->buffer.out((const char*)(src + bytes_written), chunk);
		bytes_written += written;
		
		// Wake up readers (dequeue under lock to avoid corruption, then Unblock outside)
		Queue<::ThreadBlock*> wake_list;
		while (!chan->rq.isEmpty()) {
			ThreadBlock* r_th = nullptr;
			chan->rq.Dequeue(r_th);
			if (r_th) wake_list.Enqueue(r_th);
		}
		chan->lock.Release();
		
		while (!wake_list.isEmpty()) {
			ThreadBlock* r_th = nullptr;
			wake_list.Dequeue(r_th);
			if (r_th) {
				r_th->Unblock(ThreadBlock::BlockReason::BR_RecvMsg);
			}
		}
	}
	
	return bytes_written;
}

int Filesys::ClosePipe(vfs_file* file) {
	PipeChannel* chan = (PipeChannel*)file->f_inode->internal_handler;
	if (!chan) return -1;
	
	chan->lock.Acquire();
	Queue<::ThreadBlock*> wake_list;
	if ((file->f_mode & O_ACCMODE) == O_RDONLY) {
		chan->reader_count--;
		if (chan->reader_count == 0) {
			// Collect blocked writers to wake after releasing lock
			while (!chan->wq.isEmpty()) {
				ThreadBlock* w_th = nullptr;
				chan->wq.Dequeue(w_th);
				if (w_th) wake_list.Enqueue(w_th);
			}
		}
	} else if ((file->f_mode & O_ACCMODE) == O_WRONLY) {
		chan->writer_count--;
		if (chan->writer_count == 0) {
			// Collect blocked readers to wake after releasing lock
			while (!chan->rq.isEmpty()) {
				ThreadBlock* r_th = nullptr;
				chan->rq.Dequeue(r_th);
				if (r_th) wake_list.Enqueue(r_th);
			}
		}
	}
	
	bool destroy = (chan->reader_count == 0 && chan->writer_count == 0);
	chan->lock.Release();
	
	// Unblock waiters outside the pipe lock to avoid lock-order inversion
	// (Unblock acquires scheduler_lock, and some paths hold scheduler_lock
	// before acquiring pipe-related Mutexes).
	while (!wake_list.isEmpty()) {
		ThreadBlock* th = nullptr;
		wake_list.Dequeue(th);
		if (th) {
			th->Unblock((file->f_mode & O_ACCMODE) == O_RDONLY ?
				ThreadBlock::BlockReason::BR_SendMsg :
				ThreadBlock::BlockReason::BR_RecvMsg);
		}
	}
	
	if (destroy) {
		delete[] (char*)chan->buffer.slice.address;
		delete chan;
		file->f_inode->internal_handler = nullptr;
	}
	
	// Normal inode clean up
	MutexLocal guard(&vfs_lock);
	if (file->f_inode) {
		// Inode ref_count is already decremented by ProcessBlock::Close,
		// so we only delete it here when its ref_count reaches 0.
		if (file->f_inode->ref_count == 0) {
			delete file->f_inode;
		}
	}
	free(file);
	return 0;
}

int Filesys::CloseSocket(vfs_file* file) {
	SocketHandle* socket = Filesys::GetSocket(file);
	if (!socket) return -1;

	bool destroy = false;
	Network::SocketDomain domain = Network::SocketDomain::Unspec;
	Network::SocketProtocol protocol = Network::SocketProtocol::Default;
	stduint udp_inbox_id = stduint(-1);
	bool is_bound = false;
	bool is_connected = false;
	bool is_listening = false;
	Network::SocketEndpointIPv4 local_ipv4 = {};
	Network::SocketEndpointIPv4 remote_ipv4 = {};
	{
		MutexLocal guard(&vfs_lock);
		destroy = file->f_inode && file->f_inode->ref_count == 0;
		if (destroy) {
			domain = socket->domain;
			protocol = socket->protocol;
			udp_inbox_id = socket->udp_inbox_id;
			is_bound = socket->is_bound;
			is_connected = socket->is_connected;
			is_listening = socket->is_listening;
			local_ipv4 = socket->local_ipv4;
			remote_ipv4 = socket->remote_ipv4;
		}
	}

	if (destroy) {
		#if (_MCCA & 0xFF00) == 0x8600
		if (domain == Network::SocketDomain::IPv4 &&
			is_bound && protocol == Network::SocketProtocol::UDP && local_ipv4.port) {
			Devsman::CloseUdpPort(local_ipv4.port, udp_inbox_id);
		}
		if (domain == Network::SocketDomain::IPv4 &&
			is_connected && !is_listening &&
			protocol == Network::SocketProtocol::TCP && local_ipv4.port) {
			Network::TCPConnectionContext context{
				{ local_ipv4.address, local_ipv4.port },
				{ remote_ipv4.address, remote_ipv4.port },
			};
			Devsman::CloseTcpConnection(context);
		}
		if (domain == Network::SocketDomain::IPv4 &&
			is_listening && protocol == Network::SocketProtocol::TCP && local_ipv4.port) {
			Devsman::CloseTcpPort(local_ipv4.port);
		}
		#endif
		MutexLocal guard(&vfs_lock);
		delete socket;
		file->f_inode->internal_handler = nullptr;
		delete file->f_inode;
	}
	free(file);
	return 0;
}
