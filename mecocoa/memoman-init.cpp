// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#include <c/consio.h>
#include <cpp/Device/ACPI.hpp>
#include <cpp/Device/Bus/PCI.hpp>

_ESYM_C Handler_t FILE_ENTO, FILE_ENDO;

//
_PACKED(struct) memory_info_entry {
	uint64 addr;
	uint64 len;
	uint32 type;// 1 for avail, 2 for not
};// ARDS



#if (_MCCA & 0xFF00) == 0x8600
	#if CONFIG_ACPI_BOOT_TEST
	#define ACPI_BOOT_ERROR(...) plogwarn(__VA_ARGS__)
	#else
	#define ACPI_BOOT_ERROR(...) plogerro(__VA_ARGS__)
	#endif
extern memory_info_entry MemoryListData[20];
//
static stduint parse_norm(stduint addr);
stduint acpi_rsdp_addr = 0;
stduint acpi_madt_addr = 0;
stduint acpi_mcfg_addr = 0;
stduint acpi_dmar_addr = 0;
stduint acpi_cpu_count = 0;
uint32 acpi_cpu_lapic_ids[PCU_CORES_MAX] = {};
uint64 acpi_pcie_ecam_base = 0;
uint16 acpi_pcie_segment_group = 0;
uint8 acpi_pcie_start_bus = 0;
uint8 acpi_pcie_end_bus = 0;
bool acpi_pcie_ecam_available = false;
#endif

stduint parse_grub(stduint addr);


#if _MCCA == 0x8632

static constexpr stduint kAcpiPhysMapTop = 0x10000000;
static constexpr stduint kAcpiHighWindowBase = 0x3FC00000;
static constexpr stduint kAcpiHighWindowSize = 0x00400000;

#elif _MCCA == 0x8664
#if defined(_UEFI)
extern UefiData uefi_data;
#endif
#endif

#if (_MCCA & 0xFF00) == 0x8600

static bool rsdp_checksum_ok(const uni::ACPI::RSDP* rsdp) {
	if (!rsdp) return false;
	const uint8* bytes = reinterpret_cast<const uint8*>(rsdp);
	uint8 sum = 0;
	for0(i, 20) sum = (uint8)(sum + bytes[i]);
	if (sum != 0) return false;
	if (rsdp->revision >= 2) {
		sum = 0;
		uint32 length = rsdp->length < 20 ? 20 : rsdp->length;
		for (uint32 i = 0; i < length; i++) sum = (uint8)(sum + bytes[i]);
		if (sum != 0) return false;
	}
	return true;
}

static bool rsdt_candidate_valid(stduint rsdt_addr) {
	if ((rsdt_addr & 0x3) != 0) {
		plogtrac("[ACPI] RSDT candidate %[x] is not 4-byte aligned", rsdt_addr);
	}
	auto rsdt = reinterpret_cast<const uni::ACPI::RSDT*>(rsdt_addr);
	if (rsdt->header.length < sizeof(uni::ACPI::DescriptionHeader)) {
		ACPI_BOOT_ERROR("[ACPI] Reject RSDT candidate %[x]: bad length %[x]",
			rsdt_addr, rsdt->header.length);
		return false;
	}
	if (!rsdt->header.isValid("RSDT")) {
		ACPI_BOOT_ERROR("[ACPI] Reject RSDT candidate %[x]: header validation failed", rsdt_addr);
		return false;
	}
	return true;
}

#if CONFIG_ACPI_BOOT_TEST
static void dump_rsdp_bytes(stduint addr) {
	auto bytes = reinterpret_cast<const uint8*>(addr);
	ploginfo("[ACPI] RSDP raw @%[x]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
		addr,
		bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
		bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15],
		bytes[16], bytes[17], bytes[18], bytes[19]);
	ploginfo("[ACPI] RSDP ext @%[x]: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
		addr + 20,
		bytes[20], bytes[21], bytes[22], bytes[23], bytes[24], bytes[25], bytes[26], bytes[27],
		bytes[28], bytes[29], bytes[30], bytes[31], bytes[32], bytes[33], bytes[34], bytes[35]);
}

static void log_rsdp_candidate(stduint addr, const char* source) {
	auto rsdp = reinterpret_cast<const uni::ACPI::RSDP*>(addr);
	ploginfo("[ACPI] RSDP candidate (%s) at %[x], rev=%u, RSDT=%[x], XSDT=%[x]",
		source, addr, rsdp->revision, rsdp->rsdt_address, rsdp->xsdt_address);
	dump_rsdp_bytes(addr);
}
#endif

static stduint scan_rsdp_range(stduint begin, stduint length, const char* source) {
	const stduint end = begin + length;
	stduint first = 0;
	for (stduint addr = begin; addr + sizeof(uni::ACPI::RSDP) <= end; addr += 16) {
		auto rsdp = reinterpret_cast<const uni::ACPI::RSDP*>(addr);
		if (MemCompare(rsdp->signature, "RSD PTR ", 8)) continue;
		plogtrac("[ACPI] Signature hit (%s) at %[x]", source, addr);
		if (!rsdp_checksum_ok(rsdp)) continue;

		#if CONFIG_ACPI_BOOT_TEST
		if constexpr (0) log_rsdp_candidate(addr, source);
		#endif
		
		if (!first && rsdt_candidate_valid(rsdp->rsdt_address)) first = addr;
	}
	return first;
}

static stduint find_rsdp_bios() {
	const stduint ebda_seg = *(volatile uint16*)(0x40E);
	if (ebda_seg) {
		const stduint ebda_addr = ebda_seg << 4;
		plogtrac("[ACPI] BDA[0x40E]=%[x], EBDA base=%[x]", ebda_seg, ebda_addr);
		if (auto found = scan_rsdp_range(ebda_addr, 1024, "EBDA")) return found;
	}
	else {
		#if CONFIG_ACPI_BOOT_TEST
		plogwarn("[ACPI] EBDA segment from BDA[0x40E] is zero");
		#endif
	}
	plogtrac("[ACPI] Scanning BIOS high area for RSDP: %[x]..%[x]", 0xE0000u, 0xFFFFFu);
	return scan_rsdp_range(0xE0000, 0x20000, "BIOS-HIGH");
}

static uint32 current_bootstrap_lapic_id() {
	uint32 a = 0, b = 0, c = 0, d = 0;
	#if _MCCA == 0x8664
	_IO_CPUID(0, 0, &a, &b, &c, &d);
	const uint32 maximum_leaf = a;
	if (maximum_leaf >= 0x1Fu) {
		_IO_CPUID(0x1F, 0, &a, &b, &c, &d);
		if (b) return d;
	}
	if (maximum_leaf >= 0x0Bu) {
		_IO_CPUID(0x0B, 0, &a, &b, &c, &d);
		if (b) return d;
	}
	_IO_CPUID(0x80000000u, 0, &a, &b, &c, &d);
	if (a >= 0x8000001Eu) {
		_IO_CPUID(0x8000001Eu, 0, &a, &b, &c, &d);
		return a;
	}
	#endif
	_IO_CPUID(1, 0, &a, &b, &c, &d);
	return (b >> 24) & 0xFF;
}

static void register_acpi_cpu_lapic_id(uint32 lapic_id, uint32 bsp_lapic_id) {
	for0(i, acpi_cpu_count) {
		if (acpi_cpu_lapic_ids[i] == lapic_id) return;
	}
	if (acpi_cpu_count >= PCU_CORES_MAX) {
		ACPI_BOOT_ERROR("[ACPI] CPU count exceeds PCU_CORES_MAX=%u, lapic=%[x] ignored",
			PCU_CORES_MAX, lapic_id);
		return;
	}
	acpi_cpu_lapic_ids[acpi_cpu_count++] = lapic_id;
	if (lapic_id == bsp_lapic_id && acpi_cpu_count > 1) {
		const uint32 last = acpi_cpu_count - 1;
		const uint32 tmp = acpi_cpu_lapic_ids[0];
		acpi_cpu_lapic_ids[0] = acpi_cpu_lapic_ids[last];
		acpi_cpu_lapic_ids[last] = tmp;
	}
}

static stduint find_table_from_rsdt(stduint rsdt_addr, const char* signature) {
	if (!rsdt_addr) return 0;
	auto rsdt = reinterpret_cast<const uni::ACPI::RSDT*>(rsdt_addr);
	if (rsdt->header.length < sizeof(uni::ACPI::DescriptionHeader)) {
		ACPI_BOOT_ERROR("[ACPI] RSDT length %[x] at %[x] is invalid for early mapping",
			rsdt->header.length, rsdt_addr);
		return 0;
	}
	if (!rsdt->header.isValid("RSDT")) return 0;
	const stduint count = rsdt->Count();
	for0(i, count) {
		const auto& header = (*rsdt)[i];
		const stduint entry_addr = _IMM(&header);
		if (header.length < sizeof(uni::ACPI::DescriptionHeader)) {
			continue;
		}
		if (header.isValid(signature)) return entry_addr;
	}
	return 0;
}

static stduint find_table_from_xsdt(uint64 xsdt_addr, const char* signature) {
	if (!xsdt_addr) return 0;
	if (xsdt_addr > (uint64)~(stduint)0) {
		ACPI_BOOT_ERROR("[ACPI] XSDT address %[x] exceeds stduint", xsdt_addr);
		return 0;
	}
	const stduint xsdt_addr_native = (stduint)xsdt_addr;
	auto xsdt = reinterpret_cast<const uni::ACPI::XSDT*>(xsdt_addr_native);
	if (xsdt->header.length < sizeof(uni::ACPI::DescriptionHeader)) {
		ACPI_BOOT_ERROR("[ACPI] XSDT length %[x] at %[x] is invalid for mapped range",
			xsdt->header.length, xsdt_addr);
		return 0;
	}
	if (!xsdt->header.isValid("XSDT")) return 0;
	const stduint count = xsdt->Count();
	for0(i, count) {
		const auto& header = (*xsdt)[i];
		const stduint entry_addr = _IMM(&header);
		if (header.length < sizeof(uni::ACPI::DescriptionHeader)) {
			continue;
		}
		if (header.isValid(signature)) return entry_addr;
	}
	return 0;
}

static stduint find_table_from_rsdp(stduint rsdp_addr, const char* signature) {
	if (!rsdp_addr) return 0;
	auto rsdp = reinterpret_cast<const uni::ACPI::RSDP*>(rsdp_addr);
	if (!rsdp_checksum_ok(rsdp)) {
		ACPI_BOOT_ERROR("[ACPI] RSDP at %[x] checksum failed", rsdp_addr);
		return 0;
	}
	if (rsdp->revision >= 2 && rsdp->xsdt_address) {
		if (auto table = find_table_from_xsdt(rsdp->xsdt_address, signature)) return table;
	}
	return find_table_from_rsdt(rsdp->rsdt_address, signature);
}

static stduint find_madt_from_rsdp(stduint rsdp_addr) {
	return find_table_from_rsdp(rsdp_addr, "APIC");
}

static void enumerate_madt_cpus(stduint madt_addr) {
	acpi_cpu_count = 0;
	MemSet(acpi_cpu_lapic_ids, 0, sizeof(acpi_cpu_lapic_ids));
	if (!madt_addr) return;
	auto madt = reinterpret_cast<const uni::ACPI::MADT*>(madt_addr);
	if (madt->header.length < sizeof(uni::ACPI::MADT)) {
		ACPI_BOOT_ERROR("[ACPI] MADT length %[x] at %[x] is invalid for early mapping",
			madt->header.length, madt_addr);
		return;
	}
	if (!madt->header.isValid("APIC")) return;
	const uint32 bsp_lapic_id = current_bootstrap_lapic_id();
	const stduint entries_begin = madt_addr + sizeof(uni::ACPI::MADT);
	const stduint entries_end = madt_addr + madt->header.length;
	for (stduint addr = entries_begin; addr + sizeof(uni::ACPI::MADTEntry) <= entries_end;) {
		auto entry = reinterpret_cast<const uni::ACPI::MADTEntry*>(addr);
		if (entry->length < sizeof(uni::ACPI::MADTEntry)) break;
		if (addr + entry->length > entries_end) break;
		if (entry->type == 0 && entry->length >= sizeof(uni::ACPI::MADTLocalAPIC)) {
			auto lapic = reinterpret_cast<const uni::ACPI::MADTLocalAPIC*>(entry);
			if (lapic->flags & 0x1) {
				register_acpi_cpu_lapic_id(lapic->apic_id, bsp_lapic_id);
			}
		}
		else if (entry->type == 9 && entry->length >= sizeof(uni::ACPI::MADTLocalX2APIC)) {
			auto x2apic = reinterpret_cast<const uni::ACPI::MADTLocalX2APIC*>(entry);
			if (x2apic->flags & 0x1) {
				register_acpi_cpu_lapic_id(x2apic->x2apic_id, bsp_lapic_id);
			}
		}
		addr += entry->length;
	}
	if (!acpi_cpu_count) {
		register_acpi_cpu_lapic_id(bsp_lapic_id, bsp_lapic_id);
	}
}

static void parse_mcfg(stduint mcfg_addr) {
	acpi_pcie_ecam_available = false;
	acpi_pcie_ecam_base = 0;
	acpi_pcie_segment_group = 0;
	acpi_pcie_start_bus = 0;
	acpi_pcie_end_bus = 0;
	uni::PCI::DisableConfigSpaceAccess();
	if (!mcfg_addr) return;
	auto mcfg = reinterpret_cast<const uni::ACPI::MCFG*>(mcfg_addr);
	if (mcfg->header.length < sizeof(uni::ACPI::MCFG)) {
		ACPI_BOOT_ERROR("[ACPI] MCFG length %[x] at %[x] is invalid for mapped range",
			mcfg->header.length, mcfg_addr);
		return;
	}
	if (!mcfg->header.isValid("MCFG")) return;
	const stduint count = mcfg->Count();
	for0(i, count) {
		const auto& allocation = (*mcfg)[i];
		plogtrac("[ACPI] MCFG[%u] ECAM=%[x] segment=%u bus=%u..%u",
			i, allocation.base_address, allocation.segment_group_number,
			allocation.start_bus_number, allocation.end_bus_number);
		if (!allocation.base_address ||
			allocation.end_bus_number < allocation.start_bus_number) continue;
		if (!uni::PCI::AddConfigSpaceAccess(uni::PCI::ConfigSpaceAccess{
			true,
			allocation.base_address,
			allocation.segment_group_number,
			allocation.start_bus_number,
			allocation.end_bus_number
		})) {
			ACPI_BOOT_ERROR("[ACPI] Ignoring duplicate or excess MCFG segment=%u bus=%u..%u",
				allocation.segment_group_number, allocation.start_bus_number,
				allocation.end_bus_number);
			continue;
		}
		if (!acpi_pcie_ecam_available || allocation.segment_group_number == 0) {
			acpi_pcie_ecam_base = allocation.base_address;
			acpi_pcie_segment_group = allocation.segment_group_number;
			acpi_pcie_start_bus = allocation.start_bus_number;
			acpi_pcie_end_bus = allocation.end_bus_number;
			acpi_pcie_ecam_available = true;
		}
	}
}

static void log_acpi_topology(stduint rsdp_addr) {
	if (!rsdp_addr) {
		ACPI_BOOT_ERROR("[ACPI] RSDP not found");
		return;
	}
	auto rsdp = reinterpret_cast<const uni::ACPI::RSDP*>(rsdp_addr);
	#if CONFIG_ACPI_BOOT_TEST
	ploginfo("[ACPI] RSDP found at %[x], rev=%u, RSDT=%[x], XSDT=%[x]",
		rsdp_addr, rsdp->revision, rsdp->rsdt_address, rsdp->xsdt_address);
	#endif
	if (acpi_madt_addr) {
		#if CONFIG_ACPI_BOOT_TEST
		ploginfo("[ACPI] MADT found at %[x], CPU count=%u", acpi_madt_addr, acpi_cpu_count);
		#endif
		// [s]
		// for0(i, acpi_cpu_count) {
		// 	ploginfo("[ACPI] CPU%u LAPIC ID=%[x]", i, acpi_cpu_lapic_ids[i]);
		// }
	}
	else {
		ACPI_BOOT_ERROR("[ACPI] MADT not found from RSDP");
	}
	if (acpi_mcfg_addr) {
		plogtrac("[ACPI] MCFG found at %[x]", acpi_mcfg_addr);
		if (acpi_pcie_ecam_available) {
			#if CONFIG_ACPI_BOOT_TEST
			ploginfo("[ACPI] PCIe ECAM=%[x], segment=%u, bus=%u..%u",
				acpi_pcie_ecam_base, acpi_pcie_segment_group,
				acpi_pcie_start_bus, acpi_pcie_end_bus);
			#endif
		}
		else {
			ACPI_BOOT_ERROR("[ACPI] MCFG present but no usable ECAM allocation found");
		}
	}
	else {
		#if CONFIG_ACPI_BOOT_TEST
		ploginfo("[ACPI]!MCFG not found from RSDP");
		#endif
	}
}

#endif

#if _MCCA == 0x8664
static void parse_uefi(const MemoryMap& memory_map);

#endif


void Memory::AppendAvailableRange(stduint beg, stduint end) {
	beg = ceilAlign(0x1000, beg);
	end = floorAlign(0x1000, end);
	if (beg >= end) return;

	#if (_MCCA & 0xFF00) == 0x8600
	// Exclude low 1MB (IVT, BDA, EBDA, Video buffer, BIOS ROM)
	if (beg < 0x100000) {
		if (end <= 0x100000) return;
		beg = 0x100000;
	}
	#endif

	// Exclude Kernel Image: FILE_ENTO .. FILE_ENDO
	if (_IMM(&FILE_ENTO) < _IMM(&FILE_ENDO)) {
		stduint k_beg = floorAlign(0x1000, _IMM(&FILE_ENTO));
		stduint k_end = ceilAlign(0x1000, _IMM(&FILE_ENDO));
		if (end <= k_beg || beg >= k_end) {
			mempool.Append(Slice{ beg, end - beg });
			Memory::total_memsize += end - beg;
		}
		else {
			if (beg < k_beg) {
				mempool.Append(Slice{ beg, k_beg - beg });
				Memory::total_memsize += k_beg - beg;
			}
			if (end > k_end) {
				mempool.Append(Slice{ k_end, end - k_end });
				Memory::total_memsize += end - k_end;
			}
		}
	}
}

// **initialize**: fill the mempool
// - init
// - allocate (which be with pagemap)
// - free (which be with unmap)
#if (_MCCA & 0xFF00) == 0x8600
bool Memory::initialize(stduint eax, byte* ebx) {
	mempool0.expand_only_from_self = true;
	mempool.Reset();
	Memory::total_memsize = 0;

	// x64 has default paging now
	#if _MCCA == 0x8632
	if (_IMM(&FILE_ENDO) >= _IMM(p_ext)) {
		p_ext = (byte*)(_IMM(&FILE_ENDO) + 0x1000);
		p_ext = (byte*)floorAlign(0x1000, _IMM(p_ext));
	}
	if (eax == MULTIBOOT2_BOOTLOADER_MAGIC) { parse_grub(_IMM(ebx)); }
	if (!acpi_rsdp_addr) {
		acpi_rsdp_addr = find_rsdp_bios();
	}
	if (acpi_rsdp_addr) {
		acpi_madt_addr = find_madt_from_rsdp(acpi_rsdp_addr);
		acpi_mcfg_addr = find_table_from_rsdp(acpi_rsdp_addr, "MCFG");
		acpi_dmar_addr = find_table_from_rsdp(acpi_rsdp_addr, "DMAR");
		enumerate_madt_cpus(acpi_madt_addr);
		parse_mcfg(acpi_mcfg_addr);
		log_acpi_topology(acpi_rsdp_addr);
	}
	else {
		ACPI_BOOT_ERROR("[ACPI] RSDP not found via BIOS scan");
	}
	#if CONFIG_ENABLE_MMU
	InitialPageRegion* x86_initial_page_plan = nullptr;
	stduint x86_initial_page_count = 0;
	stduint x86_initial_page_storage_bytes = 0;
	if (eax == MULTIBOOT2_BOOTLOADER_MAGIC &&
		!Memory::BuildInitialPagePlan(eax, ebx,
		x86_initial_page_plan, x86_initial_page_count,
		x86_initial_page_storage_bytes)) {
		plogwarn("[PAGING] Failed to prepare the x86 initial page plan");
	}
	#endif
	_physical_allocate = Memory::physical_allocate;
	mecocoa_global->kernel_cr3 = 0x100000;
	MemSet(kernel_paging.root_level_page = (PageEntry*)0x100000, 0, 0x1000);// kernel_paging.Reset();
	kernel_paging.Map(0x00000000, 0x00000000, 0x20000000 _TEMP, PAGESIZE_4MB, PGPROP_present | PGPROP_writable | PGPROP_user_access);// 4MB
	kernel_paging.Map(0x80000000, 0x00000000, 0x10000000, PAGESIZE_4MB, PGPROP_present | PGPROP_writable);
	kernel_paging.Map(kAcpiHighWindowBase, kAcpiHighWindowBase, kAcpiHighWindowSize, PAGESIZE_4MB, PGPROP_present | PGPROP_writable);
	kernel_paging.Map(0xFEC00000, 0xFEC00000, 0x00200000, PAGESIZE_4MB, PGPROP_present | PGPROP_writable);
	setCR4(getCR4() | 0x10);// CR4.PSE
	setCR3(_IMM(kernel_paging.root_level_page));
	PagingEnable();
	#endif
	GDT_Init();

	switch (eax)
	{
	case 'ANIF':
		#ifndef _UEFI
		ebx = (byte*)(stduint)call_ladder(R16FN_SMAP);
		#endif
		parse_norm(_IMM(ebx));
		#if CONFIG_ENABLE_MMU && _MCCA == 0x8632
		if (!Memory::BuildInitialPagePlan(eax, ebx,
			x86_initial_page_plan, x86_initial_page_count,
			x86_initial_page_storage_bytes)) {
			plogwarn("[PAGING] Failed to prepare the x86 initial page plan");
		}
		#endif
		break;
		#if _MCCA == 0x8632
	case MULTIBOOT2_BOOTLOADER_MAGIC:
		// parse_grub(_IMM(ebx));
		break;
		#endif

	#if _MCCA == 0x8664 && defined(_UEFI)
	case 'UEFI':
		acpi_rsdp_addr = _IMM(uefi_data.acpi_table);
		parse_uefi(*reinterpret_cast<MemoryMap*>(ebx));
		break;
	#endif
	default:
		return false;
	}

	map_ready = true;

	uni_default_allocator = &mempool;
	uni_hostenv_allocator = &mempool;

	#if CONFIG_ENABLE_MMU && (defined(_ARC_x86) || defined(_ARC_x64))
	if (!uni::EnablePagingPAT()) {
		plogwarn("[PAGING] PAT is unavailable");
	}
	#endif

	#if CONFIG_ENABLE_MMU && _MCCA == 0x8632
	if (x86_initial_page_plan) {
		if (!Memory::MapInitialPagePlan(kernel_paging,
			x86_initial_page_plan, x86_initial_page_count)) {
			plogwarn("[PAGING] Failed to apply the x86 initial page plan");
		}
		mempool.deallocate(x86_initial_page_plan,
			x86_initial_page_storage_bytes);
	}
	#endif

	// paging
	#if _MCCA == 0x8664
	kernel_paging.Reset();
	bool use_1g_page = Paging::isSupportedSize(PAGESIZE_1GB);
	auto mapping_range0_top = 0x100000'000ULL * (use_1g_page ? 0x800 : 16);
	kernel_paging.Map(0x00000000, 0x00000000, mapping_range0_top,
		use_1g_page ? PAGESIZE_1GB : PAGESIZE_2MB,
		PGPROP_present | PGPROP_writable
	);// pgsize 30 may be bad for Bochs; QEMU need map many times of 4G
	const auto mapping_video = _IMM(uefi_data.frame_buffer_config.frame_buffer);
	if (mapping_video +
		uefi_data.frame_buffer_config.pixels_per_scan_line *
		uefi_data.frame_buffer_config.vertical_resolution >= mapping_range0_top) {
		kernel_paging.Map(mapping_video, mapping_video,
			ceilAlign(_IMM1S(PAGESIZE_2MB), uefi_data.frame_buffer_config.pixels_per_scan_line *
				uefi_data.frame_buffer_config.vertical_resolution), PAGESIZE_2MB, PGPROP_present | PGPROP_writable
		);
	}
	kernel_paging.Map(0x0000FFFFC0000000ull, // 0xFFFFFFFFC0000000ull,
		0x0000000000000000ull,
		0x40000000ull - _IMM1S(PAGESIZE_2MB),
		PAGESIZE_2MB, PGPROP_present | PGPROP_writable
	);// High Part
	#if CONFIG_ENABLE_MMU && defined(_UEFI)
	InitialPageRegion* uefi_initial_page_plan = nullptr;
	stduint uefi_initial_page_count = 0;
	stduint uefi_initial_page_storage_bytes = 0;
	if (!Memory::BuildInitialPagePlan(eax, ebx,
		uefi_initial_page_plan, uefi_initial_page_count,
		uefi_initial_page_storage_bytes) ||
		!Memory::MapInitialPagePlan(kernel_paging,
			uefi_initial_page_plan, uefi_initial_page_count)) {
		plogwarn("[PAGING] Failed to apply the UEFI initial page plan");
	}
	if (uefi_initial_page_plan)
		mempool.deallocate(uefi_initial_page_plan,
			uefi_initial_page_storage_bytes);
	#endif
	setCR3 _IMM(kernel_paging.root_level_page);
	mecocoa_global->kernel_cr3 = _IMM(kernel_paging.root_level_page);
	if (acpi_rsdp_addr) {
		acpi_madt_addr = find_madt_from_rsdp(acpi_rsdp_addr);
		acpi_mcfg_addr = find_table_from_rsdp(acpi_rsdp_addr, "MCFG");
		acpi_dmar_addr = find_table_from_rsdp(acpi_rsdp_addr, "DMAR");
		enumerate_madt_cpus(acpi_madt_addr);
		parse_mcfg(acpi_mcfg_addr);
		log_acpi_topology(acpi_rsdp_addr);
	}
	else {
		ACPI_BOOT_ERROR("[ACPI] RSDP not provided by UEFI loader");
	}
	#endif
	GDT_Next();

	#if (_MCCA & 0xFF00) == 0x8600
	#undef ACPI_BOOT_ERROR
	void* dma_phys = mempool.allocate(0x10000, 16, 16);
	if (dma_phys && stduint(dma_phys) + 0x10000 <= 0x01000000) {
		DmaLowPoolInit(stduint(dma_phys));
		ploginfo("[MEM] ISA DMA 64KB low pool reserved at %[x]", (stduint)dma_phys);
	}
	else {
		plogwarn("[MEM] Failed to reserve ISA DMA low pool (<16MB)");
	}
	#endif

	Iova::ProbeVtd(acpi_dmar_addr);
	// mempool0.dump_available();

	return true;
}

#elif (_MCCA & 0xFF00) == 0x1000
_ESYM_C void _heap_ento(), _heap_endo();
bool Memory::initialize(stduint eax, byte* ebx) {
	stduint beg = vaultAlign(0x1000, _IMM(_heap_ento));
	stduint end = floorAlign(0x1000, _IMM(_heap_endo));
	Memory::total_memsize = end - beg;
	mempool0.expand_only_from_self = true;
	mempool.Reset(Slice{ beg, end - beg });
	map_ready = true;

	// Mempool
	uni_default_allocator = &mempool;
	uni_hostenv_allocator = &mempool;

	return true;
}
#endif


// ---- parse* ---- //
#if (_MCCA & 0xFF00) == 0x8600
static stduint parse_norm(stduint addr) {
	memory_info_entry* entry = (memory_info_entry*)addr;
	stduint count = 0;
	for0(i, numsof(MemoryListData)) {
		if (!entry->addr && !entry->len) break;
		count++;
		if (entry->type == 1 && entry->len > 0) {
			Memory::AppendAvailableRange(entry->addr, entry->addr + entry->len);
		}
		entry++;
	}
	return count;
}
#endif

#if _MCCA == 0x8632



#elif _MCCA == 0x8664


static void parse_uefi(const MemoryMap& memory_map) {
	stduint top = _IMM(memory_map.buffer) + memory_map.map_size;
	for (stduint iter = _IMM(memory_map.buffer); iter < top; iter += memory_map.descriptor_size) {
		auto desc = reinterpret_cast<MemoryDescriptor*>(iter);
		if (MemIsAvailable((MemoryType)desc->type)) {
			stduint beg = desc->physical_start;
			stduint len = desc->number_of_pages * 4096;
			Memory::AppendAvailableRange(beg, beg + len);
		}
	}
}


#endif
