// ASCII g++ TAB4 LF
// AllAuthor: @dosconio, @ArinaMgk
// ModuTitle: Mecocoa Power and Processors Management
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#if (_MCCA & 0xFF00) == 0x8600
#include <cpp/Device/ACPI.hpp>
#include <c/proctrl/IAx86_64.msr.h>
#endif

static void HandleRescheduleIPI() {
	IC.SendEOI();
}

static void HandleWakeIPI() {
	IC.SendEOI();
}

extern Handler_t SMP_AP_ENTRY[];

#if (_MCCA & 0xFF00) == 0x8600 
inline uint64_t rdtsc() {
	uint32_t lo, hi;
	// The rdtsc instruction loads the 64-bit counter into EDX:EAX
	__asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | lo;
}
// A strictly deterministic delay using TSC + pause.
// It doesn't matter how long a single 'pause' takes, 
// because the timeline is guarded strictly by the constant TSC clock.
void TSC_Wait_MS(uint64_t ms) {
	// Assuming a baseline of 2.5 GHz (2,500,000 ticks per millisecond)
	// QEMU's TSC frequency usually matches your host CPU's base clock.
	const uint64_t ticks_per_ms = 2500000; 
	uint64_t total_ticks = ms * ticks_per_ms;
	uint64_t start_time = rdtsc(); // Read initial TSC
	while ((rdtsc() - start_time) < total_ticks) {
		// 'pause' here simply prevents the loop from burning execution units
		// and handles hyper-threading gracefully.
		__asm__ __volatile__("pause" ::: "memory"); 
	}
}
#endif

void Coreman::Initialize() {
	#if (_MCCA & 0xFF00) == 0x8600 && _MCCA == 0x8632
	if (!IC.getType()) {
		plogwarn("[COREMAN] Coreman is not supported, because APIC is not supported");
		return;
	}
	IC[IRQ_RESCHED_IPI].setRange(mglb(Handint_RESCHED_Entry), SegCo32);
	IC[IRQ_WAKE_IPI].setRange(mglb(Handint_WAKE_Entry), SegCo32);
	register_interrupt_handler(IRQ_RESCHED_IPI, HandleRescheduleIPI);
	register_interrupt_handler(IRQ_WAKE_IPI, HandleWakeIPI);
	const stduint ap_entry = _IMM(SMP_AP_ENTRY);
	ploginfo("[COREMAN] Entry of SMP-AP: %[x]", ap_entry);

	// Get LAPIC ID
	if (1) {
		static rostr b_type_name[] = {
			"SMP ", // "Simultaneous Multithreading",
			"Core"
		};
		uint32 i{ 0 };
		uint32 a, b, c, d;
		while (true) {
			_IO_CPUID(0x0B, i++, &a, &b, &c, &d);// 0xB Extended Topology Enumeration
			byte type_num = c >> 8 & 0xFF;
			// According to the official Intel/AMD architecture manuals, the lower 8 bits of the ECX register (i.e., c & 0xFF) returned at this point are undefined, or simply equal to the input sub-leaf number (which is 2). It is not an explicitly calculated "total number of topology levels" returned by the CPU.
			_TEMP if (!type_num) break;
			ploginfo("[COREMAN] LAPIC ID: type(%s) width(%[x]), logp(%x)",
				type_num - 1 < numsof(b_type_name) ? b_type_name[type_num - 1] : String::newFormat("0x%[8H]", type_num - 1).reference(),
				a & 0x1F,
				b & 0xFF);
			// LOGP:  for logical processor
			// Width: for different thread
		}
		ploginfo("[COREMAN] x2APIC ID level(%[x]), current logp(%[x])", c & 0xFF, d);
	}

	// Active SMP-APs
	if (IC.getType() == 2) {
		setMSR(x86MSR::APIC_ICR_LOW, 0xC4500);// Send INIT IPI to all other processors, (5)Delivery Mode
		TSC_Wait_MS(10 + 1);
		setMSR(x86MSR::APIC_ICR_LOW, 0xC4600 | (ap_entry >> PAGESIZE_4KB));
		TSC_Wait_MS(1 + 1);
		setMSR(x86MSR::APIC_ICR_LOW, 0xC4600 | (ap_entry >> PAGESIZE_4KB));
		TSC_Wait_MS(10 + 1);
	}
	else if (IC.getType() == 1) {
		IC.WriteLAPIC(0x310, 0);
		IC.WriteLAPIC(0x300, 0xC4500);
		TSC_Wait_MS(10 + 1);
		IC.WriteLAPIC(0x310, 0);
		IC.WriteLAPIC(0x300, 0xC4600 | (ap_entry >> PAGESIZE_4KB));
		TSC_Wait_MS(1 + 1);
		IC.WriteLAPIC(0x310, 0);
		IC.WriteLAPIC(0x300, 0xC4600 | (ap_entry >> PAGESIZE_4KB));
		TSC_Wait_MS(10 + 1);
	}


	#endif
}

// ! Release resources bofore
void Coreman::Shutdown() {
	#if (_MCCA & 0xFF00) == 0x8600
	// - ACPI S5
	// - Advanced Power Management
	// ----
	static constexpr uint16 ACPI_PM1_CTL_SCI_EN = 1u << 0;
	static constexpr uint16 ACPI_PM1_CTL_SLP_EN = 1u << 13;
	static constexpr uint16 ACPI_PM1_CTL_SLP_TYPX = 7u << 10;
	auto acpi_find_table = [](stduint rsdp_addr, const char* signature) -> stduint {
		if (!rsdp_addr) return 0;
		auto rsdp = reinterpret_cast<const uni::ACPI::RSDP*>(rsdp_addr);
		if (!rsdp->isValid()) return 0;
		if (rsdp->revision >= 2 && rsdp->xsdt_address) {
			auto xsdt = reinterpret_cast<const uni::ACPI::XSDT*>(stduint(rsdp->xsdt_address));
			if (xsdt->header.isValid("XSDT")) {
				for (size_t i = 0; i < xsdt->Count(); ++i) {
					const auto& entry = xsdt->operator[](i);
					if (entry.isValid(signature)) return stduint(&entry);
				}
			}
		}
		if (rsdp->rsdt_address) {
			auto rsdt = reinterpret_cast<const uni::ACPI::RSDT*>(stduint(rsdp->rsdt_address));
			if (rsdt->header.isValid("RSDT")) {
				for (size_t i = 0; i < rsdt->Count(); ++i) {
					const auto& entry = rsdt->operator[](i);
					if (entry.isValid(signature)) return stduint(&entry);
				}
			}
		}
		return 0;
	};
	auto acpi_enable = [](const uni::ACPI::FADT& fadt) -> bool {
		if (!fadt.pm1a_cnt_blk) return false;
		if (innpw(fadt.pm1a_cnt_blk) & ACPI_PM1_CTL_SCI_EN) return true;
		if (!fadt.smi_cmd || !fadt.acpi_enable) return true;
		outpb(fadt.smi_cmd, fadt.acpi_enable);
		for (int i = 0; i < 300; ++i) {
			if ((innpw(fadt.pm1a_cnt_blk) & ACPI_PM1_CTL_SCI_EN) &&
				(!fadt.pm1b_cnt_blk || (innpw(fadt.pm1b_cnt_blk) & ACPI_PM1_CTL_SCI_EN))) {
				return true;
			}
			TSC_Wait_MS(10);
		}
		return true;
	};
	auto acpi_decode_s5 = [](const uni::ACPI::DescriptionHeader& dsdt, uint16& slp_typa, uint16& slp_typb) -> bool {
		auto aml = reinterpret_cast<const uint8*>(&dsdt + 1);
		const size_t aml_size = dsdt.length - sizeof(uni::ACPI::DescriptionHeader);
		for (size_t i = 0; i + 4 < aml_size; ++i) {
			if (StrCompareN(reinterpret_cast<const char*>(aml + i), "_S5_", 4) != 0) continue;
			if (i > 0 && aml[i - 1] != 0x08 && !(i > 1 && aml[i - 2] == '\\' && aml[i - 1] == 0x08)) continue;
			size_t p = i + 4;
			if (p >= aml_size || aml[p] != 0x12) continue;
			++p;
			if (p >= aml_size) continue;
			p += ((aml[p] & 0xC0) >> 6) + 1;
			if (p >= aml_size) continue;
			++p; // skip Package element count
			if (p >= aml_size) continue;
			if (aml[p] == 0x0A) {
				++p;
				if (p >= aml_size) continue;
			}
			const uint8 typa = aml[p++];
			if (p >= aml_size) continue;
			if (aml[p] == 0x0A) {
				++p;
				if (p >= aml_size) continue;
			}
			const uint8 typb = aml[p];
			slp_typa = uint16(typa) << 10;
			slp_typb = uint16(typb) << 10;
			return true;
		}
		return false;
	};
	if (!acpi_rsdp_addr) {
		plogwarn("[COREMAN] ACPI shutdown skipped: RSDP is unavailable");
	}
	else if (auto fadt_addr = acpi_find_table(acpi_rsdp_addr, "FACP")) {
		auto fadt = reinterpret_cast<const uni::ACPI::FADT*>(fadt_addr);
		if (fadt->header.length >= offsetof(uni::ACPI::FADT, flags) + sizeof(fadt->flags) &&
			fadt->header.isValid("FACP")) {
			uint16 slp_typa = 0;
			uint16 slp_typb = 0;
			stduint dsdt_addr = fadt->dsdt;
			if (fadt->header.length >= offsetof(uni::ACPI::FADT, x_dsdt) + sizeof(fadt->x_dsdt) && fadt->x_dsdt) {
				dsdt_addr = stduint(fadt->x_dsdt);
			}
			if (dsdt_addr) {
				auto dsdt = reinterpret_cast<const uni::ACPI::DescriptionHeader*>(dsdt_addr);
				if (dsdt->isValid("DSDT") && acpi_decode_s5(*dsdt, slp_typa, slp_typb)) {
					if (acpi_enable(*fadt)) {
						const uint16 pm1a_ctl = (innpw(fadt->pm1a_cnt_blk) & ~ACPI_PM1_CTL_SLP_TYPX) | slp_typa | ACPI_PM1_CTL_SLP_EN;
						outpw(fadt->pm1a_cnt_blk, pm1a_ctl);
						if (fadt->pm1b_cnt_blk) {
							const uint16 pm1b_ctl = (innpw(fadt->pm1b_cnt_blk) & ~ACPI_PM1_CTL_SLP_TYPX) | slp_typb | ACPI_PM1_CTL_SLP_EN;
							outpw(fadt->pm1b_cnt_blk, pm1b_ctl);
						}
						TSC_Wait_MS(1000);
					}
				}
			}
		}
	}
	plogwarn("[COREMAN] ACPI shutdown failed");
	while (1) HALT();

	#endif
}

// ! Release resources bofore
void Coreman::Reboot() {
	#if (_MCCA & 0xFF00) == 0x8600
	// - ACPI Reset
	// - 8042
	// - Triple Fault
	// ----
	auto acpi_find_table = [](stduint rsdp_addr, const char* signature) -> stduint {
		if (!rsdp_addr) return 0;
		auto rsdp = reinterpret_cast<const uni::ACPI::RSDP*>(rsdp_addr);
		if (!rsdp->isValid()) return 0;
		if (rsdp->revision >= 2 && rsdp->xsdt_address) {
			auto xsdt = reinterpret_cast<const uni::ACPI::XSDT*>(stduint(rsdp->xsdt_address));
			if (xsdt->header.isValid("XSDT")) {
				for (size_t i = 0; i < xsdt->Count(); ++i) {
					const auto& entry = xsdt->operator[](i);
					if (entry.isValid(signature)) return stduint(&entry);
				}
			}
		}
		if (rsdp->rsdt_address) {
			auto rsdt = reinterpret_cast<const uni::ACPI::RSDT*>(stduint(rsdp->rsdt_address));
			if (rsdt->header.isValid("RSDT")) {
				for (size_t i = 0; i < rsdt->Count(); ++i) {
					const auto& entry = rsdt->operator[](i);
					if (entry.isValid(signature)) return stduint(&entry);
				}
			}
		}
		return 0;
	};
	auto write_reset_reg = [](const uni::ACPI::GenericAddress& reg, uint8 value) -> bool {
		if (!reg.address || !reg.bit_width) return false;
		switch (reg.address_space) {
		case 0:
			switch (reg.bit_width) {
			case 8:
				*reinterpret_cast<volatile uint8*>(stduint(reg.address)) = value;
				return true;
			case 16:
				*reinterpret_cast<volatile uint16*>(stduint(reg.address)) = value;
				return true;
			case 32:
				*reinterpret_cast<volatile uint32*>(stduint(reg.address)) = value;
				return true;
			default:
				return false;
			}
		case 1:
			switch (reg.bit_width) {
			case 8:
				outpb(uint16(reg.address), value);
				return true;
			case 16:
				outpw(uint16(reg.address), value);
				return true;
			case 32:
				outpd(uint16(reg.address), value);
				return true;
			default:
				return false;
			}
		default:
			return false;
		}
	};
	if (acpi_rsdp_addr) {
		if (auto fadt_addr = acpi_find_table(acpi_rsdp_addr, "FACP")) {
			auto fadt = reinterpret_cast<const uni::ACPI::FADT*>(fadt_addr);
			if (fadt->header.length >= offsetof(uni::ACPI::FADT, reset_value) + sizeof(fadt->reset_value) &&
				fadt->header.isValid("FACP") &&
				write_reset_reg(fadt->reset_reg, fadt->reset_value)) {
				TSC_Wait_MS(500);
			}
		}
	}
	for (int i = 0; i < 0x10000; ++i) {
		if ((innpb(0x64) & 0x02) == 0) break;
	}
	outpb(0x64, 0xFE);
	TSC_Wait_MS(500);
	loadIDT(0, 0);
	_ASM volatile("int3");
	HALT();
	while (1);

	#endif
}
