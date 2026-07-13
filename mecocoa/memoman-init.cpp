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

byte _BUF_pagebmap[sizeof(BmMemoman)];

const stduint bmapsize = 0x100000 / 8;

// 0x0000000000000000 .. 0x0000000100000000
byte memoman_4G_00000000[bmapsize];

#if (_MCCA & 0xFF00) == 0x8600
extern memory_info_entry MemoryListData[20];
//
static stduint parse_norm(stduint addr);
stduint acpi_rsdp_addr = 0;
stduint acpi_madt_addr = 0;
stduint acpi_mcfg_addr = 0;
stduint acpi_cpu_count = 0;
uint32 acpi_cpu_lapic_ids[PCU_CORES_MAX] = {};
uint64 acpi_pcie_ecam_base = 0;
uint16 acpi_pcie_segment_group = 0;
uint8 acpi_pcie_start_bus = 0;
uint8 acpi_pcie_end_bus = 0;
bool acpi_pcie_ecam_available = false;
static uint8 acpi_rsdp_storage[sizeof(uni::ACPI::RSDP)] = {};
#endif


#if _MCCA == 0x8632

static stduint parse_grub(stduint addr);
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

static bool early_phys_range_mapped(stduint addr, stduint size) {
	if (!size) return false;
	if (addr + size < addr) return false;
	#if _MCCA == 0x8632
	if (addr < kAcpiPhysMapTop && addr + size <= kAcpiPhysMapTop) return true;
	if (addr >= kAcpiHighWindowBase &&
		addr + size <= kAcpiHighWindowBase + kAcpiHighWindowSize) return true;
	return false;
	#else
	return true;
	#endif
}

static bool rsdt_candidate_valid(stduint rsdt_addr) {
	if ((rsdt_addr & 0x3) != 0) {
		plogwarn("[ACPI] RSDT candidate %[x] is not 4-byte aligned", rsdt_addr);
	}
	if (!early_phys_range_mapped(rsdt_addr, sizeof(uni::ACPI::RSDT))) {
		plogwarn("[ACPI] Reject RSDT candidate %[x]: outside early mapped range", rsdt_addr);
		return false;
	}
	auto rsdt = reinterpret_cast<const uni::ACPI::RSDT*>(rsdt_addr);
	if (rsdt->header.length < sizeof(uni::ACPI::DescriptionHeader)) {
		plogwarn("[ACPI] Reject RSDT candidate %[x]: bad length %[x]",
			rsdt_addr, rsdt->header.length);
		return false;
	}
	if (!early_phys_range_mapped(rsdt_addr, rsdt->header.length)) {
		plogwarn("[ACPI] Reject RSDT candidate %[x]: table length %[x] outside early map",
			rsdt_addr, rsdt->header.length);
		return false;
	}
	if (!rsdt->header.isValid("RSDT")) {
		plogwarn("[ACPI] Reject RSDT candidate %[x]: header validation failed", rsdt_addr);
		return false;
	}
	return true;
}

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

static stduint scan_rsdp_range(stduint begin, stduint length, const char* source) {
	const stduint end = begin + length;
	stduint first = 0;
	for (stduint addr = begin; addr + sizeof(uni::ACPI::RSDP) <= end; addr += 16) {
		auto rsdp = reinterpret_cast<const uni::ACPI::RSDP*>(addr);
		if (MemCompare(rsdp->signature, "RSD PTR ", 8)) continue;
		ploginfo("[ACPI] Signature hit (%s) at %[x]", source, addr);
		if (!rsdp_checksum_ok(rsdp)) continue;
		log_rsdp_candidate(addr, source);
		if (!first && rsdt_candidate_valid(rsdp->rsdt_address)) first = addr;
	}
	return first;
}

static stduint find_rsdp_bios() {
	const stduint ebda_seg = *(volatile uint16*)(0x40E);
	if (ebda_seg) {
		const stduint ebda_addr = ebda_seg << 4;
		ploginfo("[ACPI] BDA[0x40E]=%[x], EBDA base=%[x]", ebda_seg, ebda_addr);
		if (auto found = scan_rsdp_range(ebda_addr, 1024, "EBDA")) return found;
	}
	else {
		plogwarn("[ACPI] EBDA segment from BDA[0x40E] is zero");
	}
	ploginfo("[ACPI] Scanning BIOS high area for RSDP: %[x]..%[x]", 0xE0000u, 0xFFFFFu);
	return scan_rsdp_range(0xE0000, 0x20000, "BIOS-HIGH");
}

static uint32 current_bootstrap_lapic_id() {
	uint32 a = 0, b = 0, c = 0, d = 0;
	_IO_CPUID(1, 0, &a, &b, &c, &d);
	return (b >> 24) & 0xFF;
}

static void register_acpi_cpu_lapic_id(uint32 lapic_id, uint32 bsp_lapic_id) {
	for0(i, acpi_cpu_count) {
		if (acpi_cpu_lapic_ids[i] == lapic_id) return;
	}
	if (acpi_cpu_count >= PCU_CORES_MAX) {
		plogwarn("[ACPI] CPU count exceeds PCU_CORES_MAX=%u, lapic=%[x] ignored",
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
	if (!early_phys_range_mapped(rsdt_addr, sizeof(uni::ACPI::RSDT))) {
		plogwarn("[ACPI] RSDT at %[x] is outside early mapped range", rsdt_addr);
		return 0;
	}
	auto rsdt = reinterpret_cast<const uni::ACPI::RSDT*>(rsdt_addr);
	if (!early_phys_range_mapped(rsdt_addr, rsdt->header.length) ||
		rsdt->header.length < sizeof(uni::ACPI::DescriptionHeader)) {
		plogwarn("[ACPI] RSDT length %[x] at %[x] is invalid for early mapping",
			rsdt->header.length, rsdt_addr);
		return 0;
	}
	if (!rsdt->header.isValid("RSDT")) return 0;
	const stduint count = rsdt->Count();
	for0(i, count) {
		const auto& header = (*rsdt)[i];
		const stduint entry_addr = _IMM(&header);
		if (!early_phys_range_mapped(entry_addr, sizeof(uni::ACPI::DescriptionHeader))) {
			continue;
		}
		if (!early_phys_range_mapped(entry_addr, header.length) ||
			header.length < sizeof(uni::ACPI::DescriptionHeader)) {
			continue;
		}
		if (header.isValid(signature)) return entry_addr;
	}
	return 0;
}

static stduint find_table_from_xsdt(uint64 xsdt_addr, const char* signature) {
	if (!xsdt_addr) return 0;
	if (xsdt_addr > (uint64)~(stduint)0) {
		plogwarn("[ACPI] XSDT address %[x] exceeds stduint", xsdt_addr);
		return 0;
	}
	const stduint xsdt_addr_native = (stduint)xsdt_addr;
	if (!early_phys_range_mapped(xsdt_addr_native, sizeof(uni::ACPI::XSDT))) {
		plogwarn("[ACPI] XSDT at %[x] is outside mapped range", xsdt_addr);
		return 0;
	}
	auto xsdt = reinterpret_cast<const uni::ACPI::XSDT*>(xsdt_addr_native);
	if (!early_phys_range_mapped(xsdt_addr_native, xsdt->header.length) ||
		xsdt->header.length < sizeof(uni::ACPI::DescriptionHeader)) {
		plogwarn("[ACPI] XSDT length %[x] at %[x] is invalid for mapped range",
			xsdt->header.length, xsdt_addr);
		return 0;
	}
	if (!xsdt->header.isValid("XSDT")) return 0;
	const stduint count = xsdt->Count();
	for0(i, count) {
		const auto& header = (*xsdt)[i];
		const stduint entry_addr = _IMM(&header);
		if (!early_phys_range_mapped(entry_addr, sizeof(uni::ACPI::DescriptionHeader))) {
			continue;
		}
		if (!early_phys_range_mapped(entry_addr, header.length) ||
			header.length < sizeof(uni::ACPI::DescriptionHeader)) {
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
		plogwarn("[ACPI] RSDP at %[x] checksum failed", rsdp_addr);
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
	if (!early_phys_range_mapped(madt_addr, sizeof(uni::ACPI::MADT))) return;
	auto madt = reinterpret_cast<const uni::ACPI::MADT*>(madt_addr);
	if (!early_phys_range_mapped(madt_addr, madt->header.length) ||
		madt->header.length < sizeof(uni::ACPI::MADT)) {
		plogwarn("[ACPI] MADT length %[x] at %[x] is invalid for early mapping",
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
	if (!early_phys_range_mapped(mcfg_addr, sizeof(uni::ACPI::MCFG))) return;
	auto mcfg = reinterpret_cast<const uni::ACPI::MCFG*>(mcfg_addr);
	if (!early_phys_range_mapped(mcfg_addr, mcfg->header.length) ||
		mcfg->header.length < sizeof(uni::ACPI::MCFG)) {
		plogwarn("[ACPI] MCFG length %[x] at %[x] is invalid for mapped range",
			mcfg->header.length, mcfg_addr);
		return;
	}
	if (!mcfg->header.isValid("MCFG")) return;
	const stduint count = mcfg->Count();
	bool have_fallback = false;
	uint64 fallback_base = 0;
	uint16 fallback_segment = 0;
	uint8 fallback_start_bus = 0;
	uint8 fallback_end_bus = 0;
	for0(i, count) {
		const auto& allocation = (*mcfg)[i];
		ploginfo("[ACPI] MCFG[%u] ECAM=%[x] segment=%u bus=%u..%u",
			i, allocation.base_address, allocation.segment_group_number,
			allocation.start_bus_number, allocation.end_bus_number);
		if (allocation.end_bus_number < allocation.start_bus_number) continue;
		if (!have_fallback) {
			fallback_base = allocation.base_address;
			fallback_segment = allocation.segment_group_number;
			fallback_start_bus = allocation.start_bus_number;
			fallback_end_bus = allocation.end_bus_number;
			have_fallback = true;
		}
		if (allocation.segment_group_number == 0) {
			acpi_pcie_ecam_base = allocation.base_address;
			acpi_pcie_segment_group = allocation.segment_group_number;
			acpi_pcie_start_bus = allocation.start_bus_number;
			acpi_pcie_end_bus = allocation.end_bus_number;
			acpi_pcie_ecam_available = true;
			uni::PCI::SetConfigSpaceAccess(uni::PCI::ConfigSpaceAccess{
				true,
				acpi_pcie_ecam_base,
				acpi_pcie_segment_group,
				acpi_pcie_start_bus,
				acpi_pcie_end_bus
			});
			return;
		}
	}
	if (have_fallback) {
		acpi_pcie_ecam_base = fallback_base;
		acpi_pcie_segment_group = fallback_segment;
		acpi_pcie_start_bus = fallback_start_bus;
		acpi_pcie_end_bus = fallback_end_bus;
		acpi_pcie_ecam_available = true;
		uni::PCI::SetConfigSpaceAccess(uni::PCI::ConfigSpaceAccess{
			true,
			acpi_pcie_ecam_base,
			acpi_pcie_segment_group,
			acpi_pcie_start_bus,
			acpi_pcie_end_bus
		});
	}
}

static void log_acpi_topology(stduint rsdp_addr) {
	if (!rsdp_addr) {
		plogwarn("[ACPI] RSDP not found");
		return;
	}
	auto rsdp = reinterpret_cast<const uni::ACPI::RSDP*>(rsdp_addr);
	ploginfo("[ACPI] RSDP found at %[x], rev=%u, RSDT=%[x], XSDT=%[x]",
		rsdp_addr, rsdp->revision, rsdp->rsdt_address, rsdp->xsdt_address);
	if (acpi_madt_addr) {
		ploginfo("[ACPI] MADT found at %[x], CPU count=%u", acpi_madt_addr, acpi_cpu_count);
		// [s]
		// for0(i, acpi_cpu_count) {
		// 	ploginfo("[ACPI] CPU%u LAPIC ID=%[x]", i, acpi_cpu_lapic_ids[i]);
		// }
	}
	else {
		plogwarn("[ACPI] MADT not found from RSDP");
	}
	if (acpi_mcfg_addr) {
		ploginfo("[ACPI] MCFG found at %[x]", acpi_mcfg_addr);
		if (acpi_pcie_ecam_available) {
			ploginfo("[ACPI] PCIe ECAM=%[x], segment=%u, bus=%u..%u",
				acpi_pcie_ecam_base, acpi_pcie_segment_group,
				acpi_pcie_start_bus, acpi_pcie_end_bus);
		}
		else {
			plogwarn("[ACPI] MCFG present but no usable ECAM allocation found");
		}
	}
	else {
		plogwarn("[ACPI] MCFG not found from RSDP");
	}
}

#endif

#if _MCCA == 0x8664
static void parse_uefi(const MemoryMap& memory_map);

#endif


// **initialize**: fill the mbitmap
// - init
// - allocate (which be with pagemap)
// - free (which be with unmap) [TODO]
#if (_MCCA & 0xFF00) == 0x8600
bool Memory::initialize(stduint eax, byte* ebx) {
	void* mapaddr = memoman_4G_00000000;
	MemSet(mapaddr, 0, bmapsize);
	Memory::pagebmap = new (_BUF_pagebmap) BmMemoman(mapaddr, bmapsize);
	mempool.Reset();

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
		enumerate_madt_cpus(acpi_madt_addr);
		parse_mcfg(acpi_mcfg_addr);
		log_acpi_topology(acpi_rsdp_addr);
	}
	else {
		plogwarn("[ACPI] RSDP not found via BIOS scan");
	}
	_physical_allocate = Memory::physical_allocate;
	mecocoa_global->kernel_cr3 = 0x100000;
	MemSet(kernel_paging.root_level_page = (PageEntry*)0x100000, 0, 0x1000);// kernel_paging.Reset();
	kernel_paging.Map(0x00000000, 0x00000000, 0x10000000, PAGESIZE_4MB, PGPROP_present | PGPROP_writable | PGPROP_user_access);// 4MB
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
	byte bmap_FFFF[2];// 0x10 pages / 8 = 2
	Bitmap BM_FFFF(bmap_FFFF, 2);
	for0(i, 0x10) {
		bool b = Memory::pagebmap->bitof(i);
		BM_FFFF.setof(i, b);
	}
	Memory::total_memsize = Memory::pagebmap->Count();
	Memory::pagebmap->add_range(
		floorAlign(0x1000, _IMM(&FILE_ENTO)) >> 12,
		(vaultAlign(0x1000, _IMM(&FILE_ENDO)) >> 12) + 1,
		false
	);// kernel
	Memory::pagebmap->add_range(0, 0x10, false);
	Memory::pagebmap->add_range(0x78, 0x100, false);
	// - 0x78000~0x7FFFF Video Modes List
	// - 0x80000~0xFFFFF BIOS and Upper Memory Area
	
	// Protect Kernel PageDirectory and dynamically populated PageTables
	#if _MCCA == 0x8632
	stduint end_ext = vaultAlign(0x1000, _IMM(Memory::p_ext)) >> 12;
	Memory::pagebmap->add_range(0x100000 >> 12, end_ext, false);
	#endif

	map_ready = true;

	uni_default_allocator = &mem;
	// mempool (kernel heap)
	const unsigned mempool_len0 = 0x4000;
	mempool.Append(Slice{ _IMM(mem.allocate(mempool_len0)), mempool_len0 });
	uni_hostenv_allocator = &mempool;

	// paging
	#if _MCCA == 0x8664
	kernel_paging.Reset();
	kernel_paging.Map(0x00000000, 0x00000000, 0x100000000ULL * 16,
		PAGESIZE_2MB, PGPROP_present | PGPROP_writable
	);// pgsize 30 may be bad for Bochs; QEMU need map many times of 4G
	kernel_paging.Map(0x0000FFFFC0000000ull, // 0xFFFFFFFFC0000000ull,
		0x0000000000000000ull,
		0x40000000ull - _IMM1S(PAGESIZE_2MB),
		PAGESIZE_2MB, PGPROP_present | PGPROP_writable
	);// High Part
	setCR3 _IMM(kernel_paging.root_level_page);
	mecocoa_global->kernel_cr3 = _IMM(kernel_paging.root_level_page);
	if (acpi_rsdp_addr) {
		acpi_madt_addr = find_madt_from_rsdp(acpi_rsdp_addr);
		acpi_mcfg_addr = find_table_from_rsdp(acpi_rsdp_addr, "MCFG");
		enumerate_madt_cpus(acpi_madt_addr);
		parse_mcfg(acpi_mcfg_addr);
		log_acpi_topology(acpi_rsdp_addr);
	}
	else {
		plogwarn("[ACPI] RSDP not provided by UEFI loader");
	}
	#endif
	GDT_Next();


	return true;
}

#elif (_MCCA & 0xFF00) == 0x1000
_ESYM_C void _heap_ento(), _heap_endo();
bool Memory::initialize(stduint eax, byte* ebx) {
	stduint beg = vaultAlign(0x1000, _IMM(_heap_ento)) >> 12;
	stduint end = floorAlign(0x1000, _IMM(_heap_endo)) >> 12;
	end &= 0xFFFFFFFFul;
	Memory::total_memsize = (end - beg) << 12;
	void* mapaddr = memoman_4G_00000000;
	MemSet(mapaddr, 0, bmapsize);
	Memory::pagebmap = new (_BUF_pagebmap) BmMemoman(mapaddr, bmapsize);
	Memory::pagebmap->add_range(beg, end, true);
	map_ready = true;

	// Mempool
	uni_default_allocator = &mem;
	const unsigned mempool_len0 = 0x10000;
	mempool.Reset(Slice{ _IMM(mem.allocate(mempool_len0)), mempool_len0 });
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
		// outsfmt("[Memoman] 0x%[64H]..0x%[64H] : 0x%x\n\r", entry->addr, entry->addr + entry->len, entry->type);
		if (entry->type == 1) {
			stduint beg = vaultAlign(0x1000, entry->addr) >> 12;
			stduint end = floorAlign(0x1000, entry->addr + entry->len) >> 12;
			Memory::pagebmap->add_range(beg, end, true);
		}
		entry++;
	}
	return count;
}
#endif

#if _MCCA == 0x8632

static stduint parse_grub(stduint addr)
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
			// outsfmt("[Memoman] base 0x%[x]..0x%[x] : %d\n\r", (u32)entry->addr, (u32)entry->addr + (u32)entry->len, (u32)entry->type);
			count++;
			if (entry->type == MULTIBOOT_MEMORY_AVAILABLE && entry->len > 0)
			{
				stduint beg = vaultAlign(0x1000, entry->addr) >> 12;
				stduint end = floorAlign(0x1000, entry->addr + entry->len) >> 12;
				Memory::pagebmap->add_range(beg, end, true);
			}
			cast<stduint>(entry) += mtag->entry_size;
		}
	}
	return count;
}

#elif _MCCA == 0x8664


static void parse_uefi(const MemoryMap& memory_map) {
	
	// UEFI had reduced the area that the kernel used
	stduint top = _IMM(memory_map.buffer) + memory_map.map_size;
	for (stduint iter = _IMM(memory_map.buffer); iter < top; iter += memory_map.descriptor_size) {
		auto desc = reinterpret_cast<MemoryDescriptor*>(iter);
		if (MemIsAvailable((MemoryType)desc->type)) {
			// ploginfo("type = %u, %[x]..%[x], attr=0x%[x]", desc->type, desc->physical_start, desc->physical_start + desc->number_of_pages * 4096, desc->attribute);
			stduint beg = vaultAlign(0x1000, desc->physical_start) >> 12;
			if (desc->physical_start + desc->number_of_pages * 4096 >= 0x100000000ULL) {
				ploginfo("Memory %[x] .. %[x] over 4G", desc->physical_start, desc->physical_start + desc->number_of_pages * 4096);
				mempool.Append(Slice{ _IMM(desc->physical_start), desc->number_of_pages * 4096 });
			}
			else Memory::pagebmap->add_range(beg, beg + desc->number_of_pages, true);
		}
	}
	Memory::pagebmap->add_range(_IMM(memory_map.buffer) >> 12, vaultAlign(0x1000, top) >> 12, false);
}


#endif
