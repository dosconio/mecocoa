
#include "../../include/mecocoa.hpp"
#include <cpp/Device/ACPI.hpp>

#if _MCCA == 0x8632

static uint8 acpi_rsdp_storage[sizeof(uni::ACPI::RSDP)] = {};


// [OK] BIOS GRUB 32
// [? ] UEFI GRUB
stduint parse_grub(stduint addr)
{
	stduint count = 0;
	stduint size = *(uint32*)addr;
	multiboot_tag* tag = (multiboot_tag*)(addr + 8);
	multiboot_tag_mmap* mtag = nullptr;
	while (tag->type != MULTIBOOT_TAG_TYPE_END)
	{
		if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
			mtag = (multiboot_tag_mmap*)tag;
		}
		else if (tag->type == MULTIBOOT_TAG_TYPE_ACPI_OLD || tag->type == MULTIBOOT_TAG_TYPE_ACPI_NEW) {
			MemSet(acpi_rsdp_storage, 0, sizeof(acpi_rsdp_storage));
			stduint rsdp_size = tag->size > 8 ? tag->size - 8 : 0;
			if (rsdp_size > sizeof(acpi_rsdp_storage)) rsdp_size = sizeof(acpi_rsdp_storage);
			MemCopyN(acpi_rsdp_storage, (const void*)((stduint)tag + 8), rsdp_size);
			acpi_rsdp_addr = (stduint)acpi_rsdp_storage;
		}
		tag = (multiboot_tag*)(_IMM(tag) + ((tag->size + 7) & ~7));
	}
	
	if (mtag) {
		multiboot_mmap_entry* entry = mtag->entries;
		while ((u32)entry < (u32)mtag + mtag->size)
		{
			count++;
			if (entry->type == MULTIBOOT_MEMORY_AVAILABLE && entry->len > 0)
			{
				Memory::AppendAvailableRange(entry->addr, entry->addr + entry->len);
			}
			cast<stduint>(entry) += mtag->entry_size;
		}
	}
	return count;
}

#elif _MCCA == 0x8664 && defined(_UEFI)

#include "../../prehost/atx-x86-flap32/multiboot2.h"
#include <c/driver/UART.h>

struct GrubBootInfo {
	stduint info_address;
	uint32 info_size;
	stduint efi_system_table;
	stduint efi_image_handle;
	const void* framebuffer_tag;
	const void* acpi_old_tag;
	const void* acpi_new_tag;
	const void* efi_memory_map_tag;
	const void* fat_module_tag;
	uint32 module_count;
	bool boot_services_active;
};

bool ExitUefiBootServices(stduint system_table_address, stduint image_handle,
	MemoryMap& map, const char*& error);

static bool RejectGrubInfo(const char*& error, const char* message) {
	error = message;
	return false;
}

// Multiboot2 Specification 2.0, section 3.6.
bool ParseGrubBootInfo(stduint address, GrubBootInfo& info, const char*& error) {
	info = {};
	error = nullptr;
	if (!address || (address & (MULTIBOOT_INFO_ALIGN - 1)) || address >= 0x100000000ULL)
		return RejectGrubInfo(error, "invalid Multiboot2 information address");

	const uint32 total_size = *(const uint32*)address;
	if (total_size < 16 || (total_size & (MULTIBOOT_TAG_ALIGN - 1)) ||
		(uint64_t)address + total_size > 0x100000000ULL)
		return RejectGrubInfo(error, "invalid Multiboot2 information size");

	info.info_address = address;
	info.info_size = total_size;
	const stduint end = address + total_size;
	stduint cursor = address + 8;
	bool found_end = false;
	while (cursor < end) {
		if (end - cursor < sizeof(multiboot_tag))
			return RejectGrubInfo(error, "truncated Multiboot2 tag");
		const auto* tag = (const multiboot_tag*)cursor;
		if (tag->size < sizeof(multiboot_tag) || tag->size > end - cursor)
			return RejectGrubInfo(error, "invalid Multiboot2 tag size");
		const uint64_t padded_size = ((uint64_t)tag->size + 7) & ~7ULL;
		if (padded_size > end - cursor)
			return RejectGrubInfo(error, "Multiboot2 tag padding exceeds information size");
		const stduint next = cursor + (stduint)padded_size;

		switch (tag->type) {
		case MULTIBOOT_TAG_TYPE_END:
			if (tag->size != 8 || next != end)
				return RejectGrubInfo(error, "invalid Multiboot2 end tag");
			found_end = true;
			break;
		case MULTIBOOT_TAG_TYPE_MODULE: {
			if (tag->size < sizeof(multiboot_tag_module) + 1)
				return RejectGrubInfo(error, "truncated Multiboot2 module tag");
			const auto* module = (const multiboot_tag_module*)tag;
			if (module->mod_end <= module->mod_start)
				return RejectGrubInfo(error, "invalid Multiboot2 module range");
			bool terminated = false;
			for (uint32 i = 0; i < tag->size - sizeof(multiboot_tag_module); ++i) {
				if (!module->cmdline[i]) {
					terminated = true;
					break;
				}
			}
			if (!terminated)
				return RejectGrubInfo(error, "unterminated Multiboot2 module command line");
			if (!StrCompare(module->cmdline, "kdiskx64.fat")) {
				if (info.fat_module_tag)
					return RejectGrubInfo(error, "duplicate kdiskx64.fat module");
				info.fat_module_tag = tag;
			}
			++info.module_count;
			break;
		}
		case MULTIBOOT_TAG_TYPE_FRAMEBUFFER: {
			if (info.framebuffer_tag || tag->size < sizeof(multiboot_tag_framebuffer_common))
				return RejectGrubInfo(error, "invalid or duplicate Multiboot2 framebuffer tag");
			const auto* framebuffer = (const multiboot_tag_framebuffer*)tag;
			if (!framebuffer->common.framebuffer_width ||
				!framebuffer->common.framebuffer_height || !framebuffer->common.framebuffer_pitch)
				return RejectGrubInfo(error, "invalid Multiboot2 framebuffer dimensions");
			if (framebuffer->common.framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
				if (tag->size < sizeof(multiboot_tag_framebuffer_common) + 6)
					return RejectGrubInfo(error, "truncated Multiboot2 RGB framebuffer tag");
			} else if (framebuffer->common.framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED) {
				if (tag->size < sizeof(multiboot_tag_framebuffer_common) + 2)
					return RejectGrubInfo(error, "truncated Multiboot2 indexed framebuffer tag");
				const uint32 palette_size = (uint32)framebuffer->framebuffer_palette_num_colors * sizeof(multiboot_color);
				if (palette_size > tag->size - sizeof(multiboot_tag_framebuffer_common) - 2)
					return RejectGrubInfo(error, "invalid Multiboot2 framebuffer palette");
			} else if (framebuffer->common.framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_EGA_TEXT) {
				return RejectGrubInfo(error, "unknown Multiboot2 framebuffer type");
			}
			info.framebuffer_tag = tag;
			break;
		}
		case MULTIBOOT_TAG_TYPE_ACPI_OLD:
			if (info.acpi_old_tag || tag->size < 8 + 20)
				return RejectGrubInfo(error, "invalid or duplicate Multiboot2 ACPI old tag");
			info.acpi_old_tag = tag;
			break;
		case MULTIBOOT_TAG_TYPE_ACPI_NEW:
			if (info.acpi_new_tag || tag->size < 8 + 36)
				return RejectGrubInfo(error, "invalid or duplicate Multiboot2 ACPI new tag");
			info.acpi_new_tag = tag;
			break;
		case MULTIBOOT_TAG_TYPE_EFI64: {
			if (info.efi_system_table || tag->size != sizeof(multiboot_tag_efi64))
				return RejectGrubInfo(error, "invalid or duplicate Multiboot2 EFI64 system table tag");
			const auto* efi = (const multiboot_tag_efi64*)tag;
			if (!efi->pointer || (efi->pointer & 7))
				return RejectGrubInfo(error, "invalid EFI64 system table pointer");
			info.efi_system_table = (stduint)efi->pointer;
			break;
		}
		case MULTIBOOT_TAG_TYPE_EFI_MMAP: {
			if (info.efi_memory_map_tag || tag->size < sizeof(multiboot_tag_efi_mmap))
				return RejectGrubInfo(error, "invalid or duplicate Multiboot2 EFI memory map tag");
			const auto* map = (const multiboot_tag_efi_mmap*)tag;
			if (map->descr_size < sizeof(MemoryDescriptor) ||
				(tag->size - sizeof(multiboot_tag_efi_mmap)) % map->descr_size)
				return RejectGrubInfo(error, "invalid Multiboot2 EFI memory map descriptors");
			info.efi_memory_map_tag = tag;
			break;
		}
		case MULTIBOOT_TAG_TYPE_EFI_BS:
			if (info.boot_services_active || tag->size != 8)
				return RejectGrubInfo(error, "invalid or duplicate Multiboot2 EFI boot services tag");
			info.boot_services_active = true;
			break;
		case MULTIBOOT_TAG_TYPE_EFI64_IH: {
			if (info.efi_image_handle || tag->size != sizeof(multiboot_tag_efi64_ih))
				return RejectGrubInfo(error, "invalid or duplicate Multiboot2 EFI64 image handle tag");
			const auto* handle = (const multiboot_tag_efi64_ih*)tag;
			if (!handle->pointer)
				return RejectGrubInfo(error, "invalid EFI64 image handle");
			info.efi_image_handle = (stduint)handle->pointer;
			break;
		}
		default:
			break;
		}
		cursor = next;
	}
	if (!found_end) return RejectGrubInfo(error, "missing Multiboot2 end tag");
	if (!info.efi_system_table) return RejectGrubInfo(error, "missing EFI64 system table");
	if (!info.efi_image_handle) return RejectGrubInfo(error, "missing EFI64 image handle");
	if (!info.boot_services_active) return RejectGrubInfo(error, "EFI boot services already terminated");
	return true;
}

extern uni::UART_t com1;
bool SerialCom1Available();

namespace {

GrubBootInfo grub_parsed_info;
MemoryMap grub_efi_memory_map;
alignas(16) byte grub_rsdp_storage[256];

// Multiboot2 Specification 2.0, section 3.6 (framebuffer and ACPI tags).
bool GrubFrameBuffer(const GrubBootInfo& info, FrameBufferConfig& config,
	const char*& error) {
	if (!info.framebuffer_tag)
		return RejectGrubInfo(error, "missing Multiboot2 framebuffer tag");
	const auto* framebuffer =
		(const multiboot_tag_framebuffer*)info.framebuffer_tag;
	const auto& common = framebuffer->common;
	if (common.framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB ||
		common.framebuffer_bpp != 32)
		return RejectGrubInfo(error, "unsupported Multiboot2 framebuffer mode");
	if (!common.framebuffer_addr ||
		(common.framebuffer_pitch & 3) ||
		(uint64_t)common.framebuffer_width * 4 > common.framebuffer_pitch ||
		(uint64_t)common.framebuffer_pitch * common.framebuffer_height >
			~uint64_t(0) - common.framebuffer_addr)
		return RejectGrubInfo(error, "invalid Multiboot2 framebuffer range");

	PixelFormat format;
	if (framebuffer->framebuffer_red_field_position == 16 &&
		framebuffer->framebuffer_red_mask_size == 8 &&
		framebuffer->framebuffer_green_field_position == 8 &&
		framebuffer->framebuffer_green_mask_size == 8 &&
		framebuffer->framebuffer_blue_field_position == 0 &&
		framebuffer->framebuffer_blue_mask_size == 8)
		format = PixelFormat::ARGB8888;
	else if (framebuffer->framebuffer_red_field_position == 0 &&
		framebuffer->framebuffer_red_mask_size == 8 &&
		framebuffer->framebuffer_green_field_position == 8 &&
		framebuffer->framebuffer_green_mask_size == 8 &&
		framebuffer->framebuffer_blue_field_position == 16 &&
		framebuffer->framebuffer_blue_mask_size == 8)
		format = PixelFormat::ABGR8888;
	else
		return RejectGrubInfo(error, "unsupported Multiboot2 framebuffer color masks");

	config.frame_buffer = (uint8_t*)(stduint)common.framebuffer_addr;
	config.pixels_per_scan_line = common.framebuffer_pitch / 4;
	config.horizontal_resolution = common.framebuffer_width;
	config.vertical_resolution = common.framebuffer_height;
	config.pixel_format = format;
	return true;
}

bool GrubRsdp(const GrubBootInfo& info, pureptr_t& address,
	const char*& error) {
	const auto* tag = info.acpi_new_tag ? info.acpi_new_tag : info.acpi_old_tag;
	if (!tag) return RejectGrubInfo(error, "missing Multiboot2 ACPI RSDP tag");
	const auto* header = (const multiboot_tag*)tag;
	const auto* source = (const uint8_t*)tag + sizeof(multiboot_tag);
	if (MemCompare((const char*)source, "RSD PTR ", 8))
		return RejectGrubInfo(error, "invalid Multiboot2 ACPI RSDP signature");
	const bool new_rsdp = info.acpi_new_tag != nullptr;
	const uint8_t revision = source[15];
	if (new_rsdp && revision < 2)
		return RejectGrubInfo(error, "invalid Multiboot2 ACPI RSDP revision");

	uint32_t length = 20;
	if (new_rsdp) {
		MemCopyN(&length, source + 20, sizeof(length));
		if (length < sizeof(uni::ACPI::RSDP) ||
			length > header->size - sizeof(multiboot_tag) ||
			length > sizeof(grub_rsdp_storage))
			return RejectGrubInfo(error, "invalid Multiboot2 ACPI RSDP length");
	}
	uint8_t sum = 0;
	for (uint32_t index = 0; index < 20; ++index)
		sum = (uint8_t)(sum + source[index]);
	if (sum) return RejectGrubInfo(error, "invalid Multiboot2 ACPI RSDP checksum");
	if (new_rsdp) {
		sum = 0;
		for (uint32_t index = 0; index < length; ++index)
			sum = (uint8_t)(sum + source[index]);
		if (sum) return RejectGrubInfo(error, "invalid Multiboot2 ACPI extended checksum");
	}

	MemSet(grub_rsdp_storage, 0, sizeof(grub_rsdp_storage));
	MemCopyN(grub_rsdp_storage, source, length);
	address = (pureptr_t)grub_rsdp_storage;
	return true;
}

[[noreturn]] void GrubStop(const char* message) {
	if (SerialCom1Available()) com1 << message << "\r\n";
	for (;;) {
		_ASM volatile("cli");
		_ASM volatile("hlt");
	}
}

}

bool R_COM1_ENTO();
extern "C" stduint grub_boot_info;

extern "C" void mecocoa(const UefiData& boot_data);

extern "C" void grub_bootstrap() {
	R_COM1_ENTO();
	const char* error = nullptr;
	if (!ParseGrubBootInfo(grub_boot_info, grub_parsed_info, error))
		GrubStop(error);
	UefiData boot_data = {};
	if (!GrubFrameBuffer(grub_parsed_info,
		boot_data.frame_buffer_config, error) ||
		!GrubRsdp(grub_parsed_info, boot_data.acpi_table, error))
		GrubStop(error);
	if (!grub_parsed_info.fat_module_tag)
		GrubStop("missing kdiskx64.fat module");
	const auto* fat_module =
		(const multiboot_tag_module*)grub_parsed_info.fat_module_tag;
	boot_data.fatvhd_addr = (pureptr_t)(stduint)fat_module->mod_start;
	boot_data.fatvhd_size = fat_module->mod_end - fat_module->mod_start;
	boot_data.boot_info_addr = (pureptr_t)grub_parsed_info.info_address;
	boot_data.boot_info_size = grub_parsed_info.info_size;
	if (!ExitUefiBootServices(grub_parsed_info.efi_system_table,
		grub_parsed_info.efi_image_handle, grub_efi_memory_map, error))
		GrubStop(error);
	boot_data.memory_map = grub_efi_memory_map;
	mecocoa(boot_data);
	GrubStop("mecocoa returned");
}

#endif
