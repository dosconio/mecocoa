
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
static void SetSegment(NormalTaskContext* ntc) {
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
