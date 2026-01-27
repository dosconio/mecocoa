// ASCII clang TAB4 CRLF
// Attribute: Arch(AMD64)
// AllAuthor: @ArinaMgk
// ModuTitle: UEFI Loader
// CodeStyle: UefiStyle
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#define _MCCA 0x8664
#undef ABS
#undef MIN
#undef MAX
#include <c/stdinc.h>
#include <c/format/ELF.h>

#if __BITS__ != 64
#error "This code is only for 64-bit architecture"
#endif

#define _INC_Color
enum PixelFormat {
	ARGB8888 = 0x00,// DB B,G,R,A <=> DD 0xAARRGGBB
	ABGR8888 = 0x01,// DB R,G,B,A <=> DD 0xAABBGGRR
	// ...
};
#undef ABS
#undef MIN
#undef MAX
#define MIN(d,s) ((d)>(s)?(s):(d))
#define MAX(d,s) ((d)<(s)?(s):(d))

#include <Uefi.h>

#include <Library/UefiLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/DiskIo2.h>
#include <Protocol/BlockIo.h>
#include <Guid/FileInfo.h>

#include "loader-graph.h"

unsigned f();

EFI_STATUS GetMemoryMap(struct MemoryMap* map) {
	if (map->buffer == NULL) {
		return EFI_BUFFER_TOO_SMALL;
	}
	map->map_size = map->buffer_size;
	return gBS->GetMemoryMap(
		&map->map_size,
		(EFI_MEMORY_DESCRIPTOR*)map->buffer,
		&map->map_key,
		&map->descriptor_size,
		&map->descriptor_version);
}

#define casitem(x) case x: return L#x
const CHAR16* GetMemoryTypeUnicode(EFI_MEMORY_TYPE type) {
	switch (type) {
	// casitem(EfiReservedMemoryType);
	// casitem(EfiLoaderCode);
	// casitem(EfiLoaderData);
	// casitem(EfiBootServicesCode);
	// casitem(EfiBootServicesData);
	// casitem(EfiRuntimeServicesCode);
	// casitem(EfiRuntimeServicesData);
	// casitem(EfiConventionalMemory);
	// casitem(EfiUnusableMemory);
	// casitem(EfiACPIReclaimMemory);
	// casitem(EfiACPIMemoryNVS);
	// casitem(EfiMemoryMappedIO);
	// casitem(EfiMemoryMappedIOPortSpace);
	// casitem(EfiPalCode);
	// casitem(EfiPersistentMemory);
	// casitem(EfiMaxMemoryType);
	// ---- Compiler does not support this macro ----
    case EfiReservedMemoryType: return L"EfiReservedMemoryType";
    case EfiLoaderCode: return L"EfiLoaderCode";
    case EfiLoaderData: return L"EfiLoaderData";
    case EfiBootServicesCode: return L"EfiBootServicesCode";
    case EfiBootServicesData: return L"EfiBootServicesData";
    case EfiRuntimeServicesCode: return L"EfiRuntimeServicesCode";
    case EfiRuntimeServicesData: return L"EfiRuntimeServicesData";
    case EfiConventionalMemory: return L"EfiConventionalMemory";
    case EfiUnusableMemory: return L"EfiUnusableMemory";
    case EfiACPIReclaimMemory: return L"EfiACPIReclaimMemory";
    case EfiACPIMemoryNVS: return L"EfiACPIMemoryNVS";
    case EfiMemoryMappedIO: return L"EfiMemoryMappedIO";
    case EfiMemoryMappedIOPortSpace: return L"EfiMemoryMappedIOPortSpace";
    case EfiPalCode: return L"EfiPalCode";
    case EfiPersistentMemory: return L"EfiPersistentMemory";
    case EfiMaxMemoryType: return L"EfiMaxMemoryType";
	//
    default: return L"InvalidMemoryType";
  }
}

EFI_STATUS SaveMemoryMap(struct MemoryMap* map, EFI_FILE_PROTOCOL* file) {
	EFI_STATUS status;
	CHAR8 buf[256];
	UINTN len;

	CHAR8* header =
		"Index, Type, Type(name), PhysicalStart, NumberOfPages, Attribute\n";
	len = AsciiStrLen(header);
	status = file->Write(file, &len, header);
	if (EFI_ERROR(status)) {
		return status;
	}

	Print(L"map->buffer = %08lx, map->map_size = %08lx\n",
		map->buffer, map->map_size);

	EFI_PHYSICAL_ADDRESS iter;
	int i;
	for (iter = (EFI_PHYSICAL_ADDRESS)map->buffer, i = 0;
		iter < (EFI_PHYSICAL_ADDRESS)map->buffer + map->map_size;
		iter += map->descriptor_size, i++) {
		EFI_MEMORY_DESCRIPTOR* desc = (EFI_MEMORY_DESCRIPTOR*)iter;
		len = AsciiSPrint(
			buf, sizeof(buf),
			"%u, %x, %-ls, %08lx, %lx, %lx\n",
			i, desc->Type, GetMemoryTypeUnicode(desc->Type),
			desc->PhysicalStart, desc->NumberOfPages,
			desc->Attribute & 0xffffflu);
		status = file->Write(file, &len, buf);
		if (EFI_ERROR(status)) {
		return status;
		}
	}

	return EFI_SUCCESS;
}

EFI_STATUS OpenRootDir(EFI_HANDLE image_handle, EFI_FILE_PROTOCOL** root) {
	EFI_STATUS status;
	EFI_LOADED_IMAGE_PROTOCOL* loaded_image;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs;

	status = gBS->OpenProtocol(
		image_handle,
		&gEfiLoadedImageProtocolGuid,
		(VOID**)&loaded_image,
		image_handle,
		NULL,
		EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
	if (EFI_ERROR(status)) {
		return status;
	}

	status = gBS->OpenProtocol(
		loaded_image->DeviceHandle,
		&gEfiSimpleFileSystemProtocolGuid,
		(VOID**)&fs,
		image_handle,
		NULL,
		EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
	if (EFI_ERROR(status)) {
		return status;
	}

	return fs->OpenVolume(fs, root);
}
// ---- GRAPHICS ----

EFI_STATUS OpenGOP(EFI_HANDLE image_handle, EFI_GRAPHICS_OUTPUT_PROTOCOL** gop) {
	EFI_STATUS status;
	UINTN num_gop_handles = 0;
	EFI_HANDLE* gop_handles = NULL;

	status = gBS->LocateHandleBuffer(
		ByProtocol,
		&gEfiGraphicsOutputProtocolGuid,
		NULL,
		&num_gop_handles,
		&gop_handles);
	if (EFI_ERROR(status)) {
		return status;
	}

	status = gBS->OpenProtocol(
		gop_handles[0],
		&gEfiGraphicsOutputProtocolGuid,
		(VOID**)gop,
		image_handle,
		NULL,
		EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
	if (EFI_ERROR(status)) {
		return status;
	}

	FreePool(gop_handles);

	return EFI_SUCCESS;
}

const CHAR16* GetPixelFormatUnicode(EFI_GRAPHICS_PIXEL_FORMAT fmt) {
	switch (fmt) {
		case PixelRedGreenBlueReserved8BitPerColor:
		return L"PixelRedGreenBlueReserved8BitPerColor";
		case PixelBlueGreenRedReserved8BitPerColor:
		return L"PixelBlueGreenRedReserved8BitPerColor";
		case PixelBitMask:
		return L"PixelBitMask";
		case PixelBltOnly:
		return L"PixelBltOnly";
		case PixelFormatMax:
		return L"PixelFormatMax";
		default:
		return L"InvalidPixelFormat";
	}
}

// ---- ELF64 ----

// #@@range_begin(calc_addr_func)
void CalcLoadAddressRange(Elf64_Ehdr* ehdr, UINT64* first, UINT64* last) {
	Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT64)ehdr + ehdr->e_phoff);
	*first = MAX_UINT64;
	*last = 0;
	for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
		if (phdr[i].p_type != PT_LOAD) continue;
		*first = MIN(*first, phdr[i].p_vaddr);
		*last = MAX(*last, phdr[i].p_vaddr + phdr[i].p_memsz);
	}
}
// #@@range_end(calc_addr_func)

// #@@range_begin(copy_segm_func)
void CopyLoadSegments(Elf64_Ehdr* ehdr) {
	Elf64_Phdr* phdr = (Elf64_Phdr*)((UINT64)ehdr + ehdr->e_phoff);
	for (Elf64_Half i = 0; i < ehdr->e_phnum; ++i) {
		if (phdr[i].p_type != PT_LOAD) continue;

		UINT64 segm_in_file = (UINT64)ehdr + phdr[i].p_offset;
		CopyMem((VOID*)phdr[i].p_vaddr, (VOID*)segm_in_file, phdr[i].p_filesz);

		UINTN remain_bytes = phdr[i].p_memsz - phdr[i].p_filesz;
		SetMem((VOID*)(phdr[i].p_vaddr + phdr[i].p_filesz), remain_bytes, 0);
	}
}
// #@@range_end(copy_segm_func)

//

#define expect(status,str) if (EFI_ERROR(status))\
	{ Print(L"%s: %r at loader.c(%u)\n", str, status, __LINE__); while (1); }

_ESYM_C EFI_STATUS EFIAPI UefiMain(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* SystemTable) {
	Print((const CHAR16*)L"Ciallo, Mecocoa~ __BITS=%u\n", sizeof(void*) << 3);
	EFI_FILE_PROTOCOL* root_dir;
	EFI_STATUS status;
	
	if (f() != 520) {
		Print(L"%s at loader.c(%u)\n", "Failed to call C++", __LINE__); while (1);
	}

	CHAR8 memmap_buf[4096 * 4];
	struct MemoryMap memmap = { sizeof(memmap_buf), memmap_buf, 0, 0, 0, 0 };
	status = GetMemoryMap(&memmap);
	expect(status, L"Failed at GetMemoryMap");
	status = OpenRootDir(image_handle, &root_dir);
	expect(status, L"Failed at OpenRootDir");
	EFI_FILE_PROTOCOL* memmap_file;
	status = root_dir->Open(
		root_dir, &memmap_file, L"\\memmap",
		EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
	if (EFI_ERROR(status)) {
		Print(L"Failed to open file '\\memmap': %r\n", status);
		Print(L"Ignored.\n");
	} else {
		status = SaveMemoryMap(&memmap, memmap_file);
		expect(status, L"Failed at SaveMemoryMap");
		status = memmap_file->Close(memmap_file);
		expect(status, L"Failed to close memory map");
	}

	_Comment(GOP);
	EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
	status = OpenGOP(image_handle, &gop);
	expect(status, L"Failed at OpenGOP");
	Print(L"Resolution: %ux%u, Pixel Format: %s, %u pixels/line\n",
		gop->Mode->Info->HorizontalResolution,
		gop->Mode->Info->VerticalResolution,
		GetPixelFormatUnicode(gop->Mode->Info->PixelFormat),
		gop->Mode->Info->PixelsPerScanLine);
	Print(L"Frame Buffer: 0x%0lx - 0x%0lx, Size: %lu bytes\n",
		gop->Mode->FrameBufferBase,
		gop->Mode->FrameBufferBase + gop->Mode->FrameBufferSize,
		gop->Mode->FrameBufferSize);

	struct FrameBufferConfig config = {
		(UINT8*)gop->Mode->FrameBufferBase,
		gop->Mode->Info->PixelsPerScanLine,
		gop->Mode->Info->HorizontalResolution,
		gop->Mode->Info->VerticalResolution,
		0
	};
	switch (gop->Mode->Info->PixelFormat) {
	case PixelRedGreenBlueReserved8BitPerColor:
		config.pixel_format = ABGR8888;
		break;
	case PixelBlueGreenRedReserved8BitPerColor:
		config.pixel_format = ARGB8888;
		break;
	default:
		Print(L"Unimplemented pixel format: %d\n", gop->Mode->Info->PixelFormat);
		while (1);
	}

	_Comment(read kernel)
	EFI_FILE_PROTOCOL* kernel_file;
	status = root_dir->Open(
		root_dir, &kernel_file, L"\\kernel.elf",
		EFI_FILE_MODE_READ, 0);
	expect(status, L"Failed to open file '\\kernel.elf'");
	UINTN file_info_size = sizeof(EFI_FILE_INFO) + sizeof(CHAR16) * 12;
	UINT8 file_info_buffer[file_info_size];
	status = kernel_file->GetInfo(
		kernel_file, &gEfiFileInfoGuid,
		&file_info_size, file_info_buffer);
	expect(status, L"Failed to get file information");
	EFI_FILE_INFO* file_info = (EFI_FILE_INFO*)file_info_buffer;
	UINTN kernel_file_size = file_info->FileSize;

	//EFI_PHYSICAL_ADDRESS kernel_base_addr = 0x100000;
	VOID* kernel_buffer;
	status = gBS->AllocatePool(EfiLoaderData, kernel_file_size, &kernel_buffer);
	expect(status, L"Failed to allocate pool");
	status = kernel_file->Read(kernel_file, &kernel_file_size, (VOID*)kernel_buffer);
	expect(status, L"Error");
	


	UINT64 entry_addr = *(UINT64*)(kernel_buffer + 24);
	Elf64_Ehdr* kernel_ehdr = (Elf64_Ehdr*)kernel_buffer;
	UINT64 kernel_first_addr, kernel_last_addr;
	CalcLoadAddressRange(kernel_ehdr, &kernel_first_addr, &kernel_last_addr);
	UINTN num_pages = (kernel_last_addr - kernel_first_addr + 0xfff) / 0x1000;
	status = gBS->AllocatePages(AllocateAddress, EfiLoaderData,
		num_pages, &kernel_first_addr);
	expect(status, L"Failed to allocate pages");
	// ---
	CopyLoadSegments(kernel_ehdr);
	Print(L"Kernel: 0x%0lx - 0x%0lx\n", kernel_first_addr, kernel_last_addr);
	status = gBS->FreePool(kernel_buffer);
	expect(status, L"Failed to free pool");

	Print(L"Kernel entry address: 0x%0lx\n", entry_addr);
// entry_addr = 0x101000;

	_Comment(exit bs) {
		EFI_STATUS status;
		status = gBS->ExitBootServices(image_handle, memmap.map_key);
		if (EFI_ERROR(status)) {
			status = GetMemoryMap(&memmap);
			expect(status, L"Failed to get memory map");
			status = gBS->ExitBootServices(image_handle, memmap.map_key);
			expect(status, L"Could not exit boot service");
		}
	}

	typedef void (*entry_t)(UefiData*);
	UefiData uefi_data;
	uefi_data.frame_buffer_config = config;
	uefi_data.memory_map = memmap;

	((entry_t)entry_addr)(&uefi_data);

	SystemTable->ConOut->OutputString(SystemTable->ConOut, L"\n\rOyasuminasaiii~\n\r");// Unreachable
	while (1);
	return 0;
}

/*
[EDK2/Conf/tools_def.txt]
2904| DEBUG_CLANGDWARF_X64_CC_FLAGS         = DEF(CLANG38_ALL_CC_FLAGS) -m64 "-DEFIAPI=__attribute__((ms_abi))" -mno-red-zone -mcmodel=small -fpie -Oz -flto DEF(CLANG38_X64_TARGET) -g  -I$(uincpath)
*/
