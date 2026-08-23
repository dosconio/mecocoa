#ifndef SYSCALL_POW_HPP_
#define SYSCALL_POW_HPP_

#include "syscall.hpp"

enum class PowerDeviceProper : uint32 {
	None = 0,
	GetIdentity,
	GetResourceCount,
	GetResource,
};

struct PowerDeviceIdentity {
	uint16 node_type = 0;
	uint16 bus_type = 0;
	uint16 dev_class = 0;
	uint16 vendor_id = 0;
	uint16 device_id = 0;
	uint8 revision = 0;
	uint8 class_base = 0;
	uint8 class_sub = 0;
	uint8 class_if = 0;
	uint16 pci_segment = 0;
	uint8 pci_bus = 0;
	uint8 pci_device = 0;
	uint8 pci_function = 0;
};

struct PowerDeviceResourceInfo {
	uint16 type = 0;
	uint16 flags = 0;
	uint32 index = 0;
	uint64 start = 0;
	uint64 length = 0;
	uint64 extra = 0;
};

struct PowerDeviceResourceQuery {
	uint32 index = 0;
	PowerDeviceResourceInfo resource = {};
};

enum class PowerDeviceMapFlag : uint32 {
	None = 0,
	Writable = 1 << 0,
};

struct PowerDeviceMapRequest {
	uint32 resource_type = 0;
	uint32 resource_index = 0;
	uint32 map_flags = 0;
	uint32 reserved = 0;
	uint64 offset = 0;
	uint64 length = 0;
};

struct PowerDeviceIoRequest {
	uint32 resource_type = 0;
	uint32 resource_index = 0;
	uint32 width = 4;
	uint32 reserved = 0;
	uint64 offset = 0;
	uint32 value = 0;
};

static inline stdsint PowerCallHello() {
	return (stdsint)syscall(syscall_t::POWERCALL_HELLO);
}

static inline stdsint PowerCallDevOpen(stduint node_id, stduint cls = 0, stduint flags = 0) {
	return (stdsint)syscall(syscall_t::POWERCALL_DEV_OPEN, node_id, cls, flags);
}

static inline stdsint PowerCallDevClose(stduint dev_handle) {
	return (stdsint)syscall(syscall_t::POWERCALL_DEV_CLOSE, dev_handle, 0, 0);
}

static inline stdsint PowerCallDevProper(stduint dev_handle, PowerDeviceProper proper, void* args = nullptr) {
	return (stdsint)syscall(syscall_t::POWERCALL_DEV_PROPER, dev_handle, _IMM(proper), _IMM(args));
}

static inline stdsint PowerCallDevGetIdentity(stduint dev_handle, PowerDeviceIdentity* args) {
	return PowerCallDevProper(dev_handle, PowerDeviceProper::GetIdentity, args);
}

static inline stdsint PowerCallDevGetResourceCount(stduint dev_handle) {
	return (stdsint)syscall(syscall_t::POWERCALL_DEV_PROPER, dev_handle, _IMM(PowerDeviceProper::GetResourceCount), 0);
}

static inline stdsint PowerCallDevGetResource(stduint dev_handle, PowerDeviceResourceQuery* args) {
	return PowerCallDevProper(dev_handle, PowerDeviceProper::GetResource, args);
}

static inline stdsint PowerCallDevCtrl(stduint dev_handle, stduint cmd, void* args = nullptr) {
	return (stdsint)syscall(syscall_t::POWERCALL_DEV_CTRL, dev_handle, cmd, _IMM(args));
}

static inline stdsint PowerCallDevMmap(stduint dev_handle, PowerDeviceMapRequest* args) {
	return (stdsint)syscall(syscall_t::POWERCALL_DEV_MMAP, dev_handle, _IMM(args), 0);
}

static inline stdsint PowerCallDevUmap(void* addr, stduint size, stduint flags = 0) {
	return (stdsint)syscall(syscall_t::POWERCALL_DEV_UMAP, _IMM(addr), size, flags);
}

static inline stdsint PowerCallDevIoRead(stduint dev_handle, PowerDeviceIoRequest* args) {
	return (stdsint)syscall(syscall_t::POWERCALL_DEV_IO_READ, dev_handle, _IMM(args), 0);
}

static inline stdsint PowerCallDevIoWrite(stduint dev_handle, PowerDeviceIoRequest* args) {
	return (stdsint)syscall(syscall_t::POWERCALL_DEV_IO_WRITE, dev_handle, _IMM(args), 0);
}

#endif
