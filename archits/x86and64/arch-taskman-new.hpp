
constexpr unsigned ldt_entry_cnt = 8;
alignas(16) static descriptor_t _LDT[ldt_entry_cnt];
#define FLAT_CODE_R3_PROP 0x00CFFA00
#define FLAT_DATA_R3_PROP 0x00CFF200
static uint32 current_bootstrap_lapic_id() {
	uint32 a = 0, b = 0, c = 0, d = 0;
	_IO_CPUID(1, 0, &a, &b, &c, &d);
	return (b >> 24) & 0xFF;
}
#if _MCCA == 0x8632
static void make_LDT(descriptor_t* ldt_alias, byte ring) {
	for0(i, 4) {
		ldt_alias[i]._data = (uint64(FLAT_CODE_R3_PROP) << 32) | 0x0000FFFF;
		ldt_alias[i].DPL = i;
		ldt_alias[i + 4]._data = (uint64(FLAT_DATA_R3_PROP) << 32) | 0x0000FFFF;
		ldt_alias[i + 4].DPL = i;
	}
	// : Although the stack segment is not Expand-down, the ESP always decreases.
}
#endif

static stduint getCS(stduint ring) {
	if (!ring) return (__BITS__ == 64 ? SegCo64 : SegCo32);
	if (ring == 3) return SegCoR3 | ring;
	else return 8 * (ring) + 0b100 | ring;
}
static stduint getSS(stduint ring) {
	if (!ring) return SegData;
	if (ring == 3) return SegDaR3 | ring;
	else return 8 * (4 + ring) + 0b100 + ring;
}
void Taskman::SetSegment(NormalTaskContext* ntc) {
	REG_FLAG_t flag = {};
	flag._r1 = 1, flag.IF = 1, flag.IOPL = (ntc->RING == 1 ? 0x1u : 0u);
	ntc->FLAG = cast<stduint>(flag);
	ntc->CS = getCS(ntc->RING);
	ntc->SS = getSS(ntc->RING);
	#if _MCCA == 0x8632
	ntc->DS = ntc->SS;
	ntc->ES = ntc->SS;
	ntc->FS = ntc->SS;
	ntc->GS = ntc->SS;
	#elif _MCCA == 0x8664
	ntc->DS = ntc->ES = ntc->FS = ntc->GS = nil;
	#endif
}

extern "C" PERCORE* C_PCU_CORES_PERCORE[]; // exported for assembly use
static void _Mapping_Core_Stack(Paging& paging);
static void SetPercoreFore(stduint cpuid) {
	if (cpuid || Taskman::PCU_CORES_PERCORE[0]) return; // already initialized
	#if _MCCA == 0x8632
	Taskman::PCU_CORES = acpi_cpu_count ? minof(acpi_cpu_count, _IMM(PCU_CORES_MAX)) : 1;
	#elif _MCCA == 0x8664
	Taskman::PCU_CORES = acpi_cpu_count ? minof(acpi_cpu_count, _IMM(PCU_CORES_MAX)) : PCU_CORES_MAX;
	#else
	Taskman::PCU_CORES = PCU_CORES_MAX;
	#endif
	for0(i, LAPIC_ID_MAP_SIZE) {
		g_lapicid_to_coreid[i] = CORE_ID_INVALID;
		#if _MCCA == 0x8632
		ap_lapicid_to_coreid[i] = CORE_ID_INVALID;
		#endif
	}

	for0(i, Taskman::PCU_CORES) {
		auto percore = (PERCORE*)mem.allocate(sizeof(PERCORE), PAGESIZE_4KB);
		MemSet(percore, 0, sizeof(PERCORE));
		Taskman::PCU_CORES_PERCORE[i] = percore;
		C_PCU_CORES_PERCORE[i] = percore;
		higher_stacks[i] = (mem.allocate(0x1000, PAGESIZE_4KB));
		#if _MCCA == 0x8664
		kernel_paging.Map(
			PERCORE_VBASE + i * PERCORE_STRIDE,
			_IMM(percore),
			PERCORE_STRIDE,
			PAGESIZE_4KB,
			PGPROP_present | PGPROP_writable
		);
		#endif
		#if _MCCA == 0x8632
		ring3_iret_stacks[i] = (mem.allocate(0x1000, PAGESIZE_4KB));
		// ploginfo("ring3_iret_stacks %u: %p", i, ring3_iret_stacks[i]);
		treat<uint32>(ring3_iret_stacks[i]) = 0xdeadbeef;
		ap_ring3_iret_stack_tops[i] = (0xFFFFF000u - i * 0x1000u) + 0x1000u - 0x10u;
		ap_higher_stack_tops[i] = _IMM(higher_stacks[i]) + 0x1000 - 0x10;
		#endif
		#if _MCCA == 0x8632 || _MCCA == 0x8664
		percore->lapic_id = acpi_cpu_count ? acpi_cpu_lapic_ids[i] :
			(i == 0 ? current_bootstrap_lapic_id() : CORE_ID_INVALID);
		if (percore->lapic_id < LAPIC_ID_MAP_SIZE) {
			g_lapicid_to_coreid[percore->lapic_id] = i;
			#if _MCCA == 0x8632
			ap_lapicid_to_coreid[percore->lapic_id] = i;
			#endif
		}
		else {
			plogwarn("[COREMAN] LAPIC ID %[x] exceeds direct map size %u",
				percore->lapic_id, LAPIC_ID_MAP_SIZE);
		}
		#else
		percore->lapic_id = CORE_ID_INVALID;
		#endif
		percore->state = CoreState::Prepared;
		percore->kernel_stack = 0;
		#if _MCCA == 0x8664
		percore->tss.RSP0 = GetCoreRingStackBase(i) + HIGHER_STACK_SIZE - 8;// for user-app in cpu0
		#else
		#endif
		//{} TEMP GDT_Alloc and tss.setRange
		if (i == 0) {
			mecocoa_global->gdt_ptr->tss.setRange(mglb(&Taskman::PCU_CORES_PERCORE[i]->tss), sizeof(TSS_t) - 1);
		}

		// Set TSS
		#if _MCCA == 0x8632
		if (i == 0) {
			Taskman::PCU_CORES_PERCORE[i]->tss_selector = SegTSS0;
		}
		else {
			descriptor_t* const GDT = (descriptor_t*)mecocoa_global->gdt_ptr;
			word selector = GDT_Alloc();
			Descriptor32Set(&GDT[selector / 8], mglb(&Taskman::PCU_CORES_PERCORE[i]->tss), sizeof(TSS_t) - 1, _Dptr_TSS386_Available, 0, 0, 1, 0);
			Taskman::PCU_CORES_PERCORE[i]->tss_selector = selector;
		}

		#elif _MCCA == 0x8664
		//{} TODO

		#endif

		#if _MCCA == 0x8632
		Taskman::PCU_CORES_PERCORE[i]->tss.ESP0 = GetCoreTransitionStackTop(i);
		Taskman::PCU_CORES_PERCORE[i]->tss.SS0 = SegData;
		Taskman::PCU_CORES_PERCORE[i]->tss.ESP1 = GetCoreTransitionStackTop(i);
		Taskman::PCU_CORES_PERCORE[i]->tss.SS1 = 8 * 5 + 4 + 1;// 4:LDT 8*5:SS1 1:Ring1
		Taskman::PCU_CORES_PERCORE[i]->tss.ESP2 = GetCoreTransitionStackTop(i);
		Taskman::PCU_CORES_PERCORE[i]->tss.SS2 = 8 * 6 + 4 + 2;// 4:LDT 8*6:SS2 2:Ring2
		Taskman::PCU_CORES_PERCORE[i]->tss.LDTDptr = SegGLDT + 3; // LDT yo GDT
		Taskman::PCU_CORES_PERCORE[i]->tss.LDTLength = 8 * 8 - 1;
		Taskman::PCU_CORES_PERCORE[i]->tss.STRC_15_T = 0;
		Taskman::PCU_CORES_PERCORE[i]->tss.IO_MAP = sizeof(TSS_t) - 1;
		#endif

	}
	_Mapping_Core_Stack(kernel_paging);
	Taskman::PCU_CORES_PERCORE[0]->state = CoreState::Online;//

	#if _MCCA == 0x8632// TEMP x64 do not use LDT (no R1 and R2)
	for0(i, PCU_CORES_MAX) {
		if (Taskman::PCU_CORES_PERCORE[i]) {
			kernel_paging.Map(GetCoreRingStackBase(i), (stduint)mem.allocate(HIGHER_STACK_SIZE), HIGHER_STACK_SIZE, PAGESIZE_4KB, PGPROP_present | PGPROP_writable);
		}
	}
	make_LDT(_LDT, 3);
	const auto LDTLength = sizeof(_LDT) - 1;
	descriptor_t* const GDT = (descriptor_t*)mecocoa_global->gdt_ptr;
	Descriptor32Set(&GDT[SegGLDT / 8], mglb(&_LDT), LDTLength, _Dptr_LDT, 0, 0 /* is_sys */, 1 /* 32-b */, 0 /* not-4k */);
	#endif

	loadTask(SegTSS0);

	#if _MCCA == 0x8632
	_ASM("LLDT %w0" : : "r"(SegGLDT) : "memory");
	#endif

}
