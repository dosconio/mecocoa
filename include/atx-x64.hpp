#ifndef _MCCA_x64
#define _MCCA_x64

#include <cpp/unisym>
using namespace uni;
#include <c/datime.h>
#include <c/dnode.h>
#include <cpp/interrupt>
#include <cpp/Device/_Video.hpp>
#include "console.hpp"


#include "memoman.hpp"
#define PAGE_SIZE 0x1000 // lev-0-page

#include "taskman.hpp"

// ---- asm

_ESYM_C void tryUD();

// ---- sysinfo

void kernel_fail(void* serious, ...);
rostr text_brand();

// ---- handler

#define SysTickFreq 100 // 100Hz

// volatile timeval system_time;

extern volatile stduint tick;

_ESYM_C{
	// void Handint_SYSCALL_Entry(), Handint_SYSCALL();
	void Handint_XHCI_Entry(), Handint_XHCI();
	void Handint_LAPICT_Entry(), Handint_LAPICT();
}


// UEFI

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

#ifdef _UEFI
#include "../prehost/atx-x64-uefi64/atx-x64-uefi64.loader/loader-graph.h"
#endif

#define mglb(x) (_IMM(x) | 0xFFFFFFFFC0000000ull)

#endif // _MCCA_UEFI64
