// ASCII g++ TAB4 LF
// AllAuthor: @ArinaMgk
// Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../include/mecocoa.hpp"

#if (_MCCA & 0xFF00) == 0x8600
#include "c/proctrl/IAx86_64.msr.h"
#include "c/proctrl/IAx86_64.ext.h"
#endif

static bool vm_supported = false;
bool Virtman::Initialize() {
	vm_supported = false;
	#if (_MCCA & 0xFF00) == 0x8600
	extern rostr text_cpu_factory();
	extern Procontroller_t cpu_type;
	text_cpu_factory();
	unsigned a, b, c, d;
	uint64_t val;
	switch (cpu_type) {
	case PCU_Intel:
		_IO_CPUID(1, tok_any, &a, &b, &c, &d);
		if (vm_supported = cast<cpuid_1_0_ecx>(c).vmx) {
			setCR4(getCR4() | _IMM1S(13));// enable CR4.VMXE
			// ploginfo("CR4: %[x], MSR_3A: %[x]", getCR4(), getMSR(x86MSR::FEATURE_CONTROL));
			auto fc = getMSR(x86MSR::FEATURE_CONTROL);
			if (fc & 1) {// BIT0 Locked
				if ((fc & 0b101) == 0b001) {// BIT2 Enable VMX outside SMX
					plogwarn("[VIRTUAL] VMX is disabled in the BIOS");
					return vm_supported = false;
				}
			}
			else {
				setMSR(x86MSR::FEATURE_CONTROL, fc | 0b101);
			}
			auto mem_vmxon = mempool.allocate(0x1000, PAGESIZE_4KB);
			// ploginfo("[VIRTUAL] VMXON memory: %[x]", mem_vmxon);
			if (!mem_vmxon) {
				plogerro("[VIRTUAL] VMXON memory is not valid");
				return vm_supported = false;
			}
			uint32 revision_id = getMSR(x86MSR::VMX_BASIC);
			*(volatile uint32*)mem_vmxon = revision_id;
			volatile uint64 pmem_vmxon = (uint64)mem_vmxon;
			uint8_t error_flag;
			// here: CR0  PE, NE, PG are usually required
			// ->  IA32_VMX_CR0_FIXED0/1 IA32_VMX_CR4_FIXED0/1
			_ASM volatile (
				"vmxon %[pa]\n\t"
				"setna %[err]"  // CF || ZF
				: [err] "=q" (error_flag)
				: [pa] "m"  (pmem_vmxon)
				: "cc", "memory"
				);
			if (error_flag) {
				plogerro("[VIRTUAL] VMXON failed (CF=1 or ZF=1)");//{} VMREAD
				return vm_supported = false;
			}
			plogpass("[VIRTUAL] Using Intel VT-x");
		}
		else plogwarn("[VIRTUAL] VT-x is not supported");
		return vm_supported;
	case PCU_AMD:// SVM(Secure Virtual Machine)/AMD-V
		_IO_CPUID(0x80000001, tok_any, &a, &b, &c, &d);
		if (!(c & (1 << 2))) { // ECX Bit 2: SVM
			plogwarn("[VIRTUAL] SVM (AMD-V) is not supported by this CPU");
			return vm_supported = false;
		}
		// Check Lock
		val = getMSR((x86MSR::VM_CR));
		if (val & (1 << 4)) { // Bit 4: SVMDIS
			plogwarn("[VIRTUAL] SVM is locked off in the BIOS");
			return false;
		}
		// Enable EFER.SVME
		val = getMSR(x86MSR::EFER);
		setMSR(x86MSR::EFER, val | (1 << 12)); // Bit 12: SVME
		if (!(getMSR(x86MSR::EFER) & (1 << 12))) {
			plogerro("[VIRTUAL] Failed to enable EFER.SVME");
			return false;
		}
		// Host Save Area (HSAVE), Machine use-only
		if (void* mem_hsave = mempool.allocate(0x1000, PAGESIZE_4KB))
			val = (uint64_t)mem_hsave;
		else {
			plogerro("[VIRTUAL] Failed to allocate HSAVE memory");
			return false;
		}
		setMSR(x86MSR::VM_HSAVE_PA, val);
		plogpass("[VIRTUAL] Using AMD-V/RVI");
		return vm_supported = true;
	default:
		plogwarn("[VIRTUAL] Virtual Machine is not supported");
		break;
	}

	
	#endif
	return false;
}

