
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

#endif
