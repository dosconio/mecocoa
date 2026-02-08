#ifndef _MCCA_x64
#define _MCCA_x64

#include <cpp/unisym>
using namespace uni;
#include <c/datime.h>
#include <cpp/interrupt>
#include <cpp/Device/_Video.hpp>
#include "console.hpp"
#include "memoman.hpp"
#include "taskman.hpp"

// LONG AND UEFI

struct mec_gdt {
	descriptor_t null;
	descriptor_t data;
	descriptor_t code;
	gate_t rout;
	descriptor_t co16;
	descriptor_t co64;
	descriptor_t dar3;
	descriptor_t cor3;
	descriptor_t tss;
	// descriptor_t code_r3;
	// descriptor_t data_r3;
};// on global linear area
struct mecocoa_global_t {
	uint16 ADDR_PARA0;
	uint16 ADDR_PARA1;
	uint16 ADDR_PARA2;
	uint16 ADDR_PARA3;
	volatile timeval_t system_time;
	word gdt_len;
	mec_gdt* gdt_ptr;
};
inline static mecocoa_global_t* mecocoa_global{ (mecocoa_global_t*)0x500 };

// ---- asm

_ESYM_C void tryUD();

// ---- sysinfo

int* kernel_fail(loglevel_t serious);
rostr text_brand();

// ---- handler

// volatile timeval system_time;

extern volatile stduint tick;

void Handint_XHCI(InterruptFrame* frame);
void Handint_LAPICT(InterruptFrame* frame);



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

#define mglb(x) (_IMM(x))

#endif // _MCCA_UEFI64
