#include "../../include/mecocoa.hpp"

#if _MCCA == 0x8664 && defined(_UEFI)

#include <stddef.h>

namespace {

	struct EfiTableHeader {
		uint64_t signature;
		uint32_t revision;
		uint32_t header_size;
		uint32_t crc32;
		uint32_t reserved;
	};

	using EfiGetMemoryMap = uint64_t(__attribute__((ms_abi))*)(stduint*, void*, stduint*, stduint*, uint32_t*);
	using EfiAllocatePool = uint64_t(__attribute__((ms_abi))*)(uint32_t, stduint, void**);
	using EfiFreePool = uint64_t(__attribute__((ms_abi))*)(void*);
	using EfiExitBootServices = uint64_t(__attribute__((ms_abi))*)(void*, stduint);

	struct EfiBootServices {
		EfiTableHeader header;
		void* before_memory_map[4];
		EfiGetMemoryMap get_memory_map;
		EfiAllocatePool allocate_pool;
		EfiFreePool free_pool;
		void* before_exit_boot_services[19];
		EfiExitBootServices exit_boot_services;
	};

	struct EfiSystemTable {
		EfiTableHeader header;
		void* firmware_vendor;
		uint32_t firmware_revision;
		uint32_t reserved;
		void* before_boot_services[7];
		EfiBootServices* boot_services;
	};

	// UEFI Specification 2.10, sections 4.3 and 7.4.
	static_assert(offsetof(EfiSystemTable, boot_services) == 96);
	static_assert(offsetof(EfiBootServices, get_memory_map) == 56);
	static_assert(offsetof(EfiBootServices, allocate_pool) == 64);
	static_assert(offsetof(EfiBootServices, free_pool) == 72);
	static_assert(offsetof(EfiBootServices, exit_boot_services) == 232);

	constexpr uint64_t EfiSystemTableSignature = 0x5453595320494249ULL;
	constexpr uint64_t EfiBootServicesSignature = 0x56524553544F4F42ULL;
	constexpr uint64_t EfiInvalidParameter = 0x8000000000000002ULL;
	constexpr uint64_t EfiBufferTooSmall = 0x8000000000000005ULL;
	alignas(8) byte memory_map_storage[16 * 1024];

	bool RejectUefi(const char*& error, const char* message) {
		error = message;
		return false;
	}

}

bool ExitUefiBootServices(stduint system_table_address, stduint image_handle, MemoryMap& map, const char*& error) {
	map = {};
	error = nullptr;
	if (!system_table_address || (system_table_address & 7) || !image_handle)
		return RejectUefi(error, "invalid UEFI system table or image handle");

	const auto* system_table = (const EfiSystemTable*)system_table_address;
	if (system_table->header.signature != EfiSystemTableSignature ||
		system_table->header.header_size < sizeof(EfiSystemTable))
		return RejectUefi(error, "invalid UEFI system table");
	auto* boot_services = system_table->boot_services;
	if (!boot_services || boot_services->header.signature != EfiBootServicesSignature ||
		boot_services->header.header_size < offsetof(EfiBootServices, exit_boot_services) + sizeof(void*) ||
		!boot_services->get_memory_map || !boot_services->allocate_pool ||
		!boot_services->free_pool || !boot_services->exit_boot_services)
		return RejectUefi(error, "invalid UEFI boot services table");

	void* buffer = memory_map_storage;
	stduint capacity = sizeof(memory_map_storage);
	for (uint32_t attempt = 0; attempt < 16; ++attempt) {
		stduint map_size = capacity;
		stduint map_key = 0;
		stduint descriptor_size = 0;
		uint32_t descriptor_version = 0;
		const uint64_t status = boot_services->get_memory_map(
			&map_size, buffer, &map_key, &descriptor_size, &descriptor_version);
		if (status == EfiBufferTooSmall) {
			if (map_size > (stduint)-1 - 4096)
				return RejectUefi(error, "UEFI memory map size overflow");
			const stduint new_capacity = map_size + 4096;
			void* new_buffer = nullptr;
			if (boot_services->allocate_pool((uint32_t)MemoryType::EfiLoaderData,
				new_capacity, &new_buffer) || !new_buffer)
				return RejectUefi(error, "cannot allocate UEFI memory map buffer");
			if (buffer != memory_map_storage) boot_services->free_pool(buffer);
			buffer = new_buffer;
			capacity = new_capacity;
			continue;
		}
		if (status)
			return RejectUefi(error, "UEFI GetMemoryMap failed");
		if (!map_size || map_size > capacity ||
			descriptor_size < sizeof(MemoryDescriptor) ||
			map_size % descriptor_size)
			return RejectUefi(error, "invalid UEFI memory map descriptors");

		map.buffer_size = capacity;
		map.buffer = buffer;
		map.map_size = map_size;
		map.map_key = map_key;
		map.descriptor_size = descriptor_size;
		map.descriptor_version = descriptor_version;

		const uint64_t exit_status = boot_services->exit_boot_services(
			(void*)image_handle, map_key);
		if (!exit_status) return true;
		if (exit_status != EfiInvalidParameter)
			return RejectUefi(error, "UEFI ExitBootServices failed");
	}
	return RejectUefi(error, "UEFI memory map changed too often");
}

#endif
