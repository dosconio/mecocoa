#include "console.hpp"
#include "memoman.hpp"

#ifndef _MCCA_UEFI64
#define _MCCA_UEFI64



// others

extern FrameBufferConfig config_graph;

struct MemoryMap {
	stduint buffer_size;
	void*   buffer;
	stduint map_size;
	stduint map_key;
	stduint descriptor_size;
	uint32  descriptor_version;
};

struct MemoryDescriptor {
	uint32_t type;
	uintptr_t physical_start;
	uintptr_t virtual_start;
	uint64_t number_of_pages;
	uint64_t attribute;
};

#ifdef __cplusplus
enum class MemoryType {
	EfiReservedMemoryType,
	EfiLoaderCode,
	EfiLoaderData,
	EfiBootServicesCode,
	EfiBootServicesData,
	EfiRuntimeServicesCode,
	EfiRuntimeServicesData,
	EfiConventionalMemory,
	EfiUnusableMemory,
	EfiACPIReclaimMemory,
	EfiACPIMemoryNVS,
	EfiMemoryMappedIO,
	EfiMemoryMappedIOPortSpace,
	EfiPalCode,
	EfiPersistentMemory,
	EfiMaxMemoryType
};

inline bool operator==(uint32_t lhs, MemoryType rhs) {
	return lhs == static_cast<uint32_t>(rhs);
}

inline bool operator==(MemoryType lhs, uint32_t rhs) {
	return rhs == lhs;
}

// memory available transferred from UEFI
inline bool MemIsAvailable(MemoryType memory_type) {
  return
    memory_type == MemoryType::EfiBootServicesCode ||
    memory_type == MemoryType::EfiBootServicesData ||
    memory_type == MemoryType::EfiConventionalMemory;
}

const int UEFIPageSize = 0x1000;

#endif

_ESYM_C
void setCSSS(uint16 cs, uint16 ss);
_ESYM_C
void setDSAll(uint16 value);




#endif // _MCCA_UEFI64
