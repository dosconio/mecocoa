#ifndef SYSCALL_POW_HPP_
#define SYSCALL_POW_HPP_

#include <c/stdinc.h>
#include "syscall.hpp"
#include "taskman.com.hpp"

enum class PwcallDeviceProper : uint32 {
	None = 0,
	GetIdentity,
	GetResourceCount,
	GetResource,
};

enum class PwcallDeviceResourceType : uint16 {
	None = 0,
	PciBarMmio,
	PciBarIo,
	IoPortRange,
	IrqLine,
	PciBridgeBusRange,
	UsbLocation,
	UsbEndpoint,
	DmaChannel,
};

struct PwcallDeviceIdentity {
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

struct PwcallDeviceResourceInfo {
	uint16 type = 0;
	uint16 flags = 0;
	uint32 index = 0;
	uint64 start = 0;
	uint64 length = 0;
	uint64 extra = 0;
};

struct PwcallDeviceResourceQuery {
	uint32 index = 0;
	PwcallDeviceResourceInfo resource = {};
};

enum class PwcallDeviceMapFlag : uint32 {
	None = 0,
	Writable = 1 << 0,
};

struct PwcallDeviceMapRequest {
	uint32 resource_type = 0;
	uint32 resource_index = 0;
	uint32 map_flags = 0;
	uint32 reserved = 0;
	uint64 offset = 0;
	uint64 length = 0;
};

struct PwcallDeviceIoRequest {
	uint32 resource_type = 0;
	uint32 resource_index = 0;
	uint32 width = 4;
	uint32 reserved = 0;
	uint64 offset = 0;
	uint32 value = 0;
};

enum class PwcallDeviceDmaMapFlag : uint32 {
	None = 0,
	Writable = 1 << 0,
};

struct PwcallDeviceDmaMapRequest {
	uint64 physical = 0;
	uint64 length = 0;
	uint64 address = 0;
	uint32 map_flags = 0;
	uint32 reserved = 0;
};

enum class PwcallDevicePublishCommand : uint32 {
	None = 0,
	Started,
	FramebufferAperture,
};

struct PwcallDeviceFramebufferAperture {
	uint32 resource_type = 0;
	uint32 resource_index = 0;
	uint32 reserved = 0;
	uint32 flags = 0;
	uint64 start = 0;
	uint64 length = 0;
};

namespace Powercall {

	static inline stdsint Hello() {
		return (stdsint)syscall(syscall_t::POWERCALL_HELLO);
	}

	static inline stdsint DevOpen(stduint node_id, stduint cls = 0, stduint flags = 0) {
		return (stdsint)syscall(syscall_t::POWERCALL_DEV_OPEN, node_id, cls, flags);
	}

	static inline stdsint DevClose(stduint dev_handle) {
		return (stdsint)syscall(syscall_t::POWERCALL_DEV_CLOSE, dev_handle, 0, 0);
	}

	static inline stdsint DevProper(stduint dev_handle, PwcallDeviceProper proper, void* args = nullptr) {
		return (stdsint)syscall(syscall_t::POWERCALL_DEV_PROPER, dev_handle, _IMM(proper), _IMM(args));
	}

	static inline stdsint DevGetIdentity(stduint dev_handle, PwcallDeviceIdentity* args) {
		return DevProper(dev_handle, PwcallDeviceProper::GetIdentity, args);
	}

	static inline stdsint DevGetResourceCount(stduint dev_handle) {
		return (stdsint)syscall(syscall_t::POWERCALL_DEV_PROPER, dev_handle, _IMM(PwcallDeviceProper::GetResourceCount), 0);
	}

	static inline stdsint DevGetResource(stduint dev_handle, PwcallDeviceResourceQuery* args) {
		return DevProper(dev_handle, PwcallDeviceProper::GetResource, args);
	}

	static inline stdsint DevCtrl(stduint dev_handle, stduint cmd, void* args = nullptr) {
		return (stdsint)syscall(syscall_t::POWERCALL_DEV_CTRL, dev_handle, cmd, _IMM(args));
	}

	static inline stdsint DevMmap(stduint dev_handle, PwcallDeviceMapRequest* args) {
		return (stdsint)syscall(syscall_t::POWERCALL_DEV_MMAP, dev_handle, _IMM(args), 0);
	}

	static inline stdsint DevUmap(void* addr, stduint size, stduint flags = 0) {
		return (stdsint)syscall(syscall_t::POWERCALL_DEV_UMAP, _IMM(addr), size, flags);
	}

	static inline stdsint DevIoRead(stduint dev_handle, PwcallDeviceIoRequest* args) {
		return (stdsint)syscall(syscall_t::POWERCALL_DEV_IO_READ, dev_handle, _IMM(args), 0);
	}

	static inline stdsint DevIoWrite(stduint dev_handle, PwcallDeviceIoRequest* args) {
		return (stdsint)syscall(syscall_t::POWERCALL_DEV_IO_WRITE, dev_handle, _IMM(args), 0);
	}

	static inline stdsint DevPublish(stduint dev_handle, PwcallDevicePublishCommand cmd, void* args = nullptr) {
		return (stdsint)syscall(syscall_t::POWERCALL_DEV_PUBLISH, dev_handle, _IMM(cmd), _IMM(args));
	}

	static inline stdsint DevWait(stduint dev_handle, stduint timeout = 0, stduint flags = 0) {
		return (stdsint)syscall(syscall_t::POWERCALL_DEV_WAIT, dev_handle, timeout, flags);
	}

	static inline stdsint DevAck(stduint dev_handle, stduint event, stduint flags = 0) {
		return (stdsint)syscall(syscall_t::POWERCALL_DEV_ACK, dev_handle, event, flags);
	}

	static inline stdsint DevDmaAlloc(stduint dev_handle, stduint size, stduint flags = 0) {
		return (stdsint)syscall(syscall_t::POWERCALL_DEV_DMA_ALLOC, dev_handle, size, flags);
	}

	static inline stdsint DevDmaFree(stduint dma_handle) {
		return (stdsint)syscall(syscall_t::POWERCALL_DEV_DMA_FREE, dma_handle, 0, 0);
	}

	static inline stdsint DevDmaMap(stduint dma_handle, PwcallDeviceDmaMapRequest* args, stduint flags = 0) {
		return (stdsint)syscall(syscall_t::POWERCALL_DEV_DMA_MAP, dma_handle, _IMM(args), flags);
	}

	static inline stdsint SysComm(stduint op, stduint to, CommMsg* msg) {
		return (stdsint)syscall(syscall_t::COMM, op, to, _IMM(msg));
	}

}

#endif
