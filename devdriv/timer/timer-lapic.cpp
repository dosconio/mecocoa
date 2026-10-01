// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: LAPIC Timer
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include <c/driver/timer.h>
#include <cpp/Device/ACPI.hpp>


#if _MCCA == 0x8664 && defined(_UEFI)
extern UefiData uefi_data;
void RenderFrameFlush();
_ESYM_C void R_LAPICT_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_LAPICT{
	.init = R_LAPICT_INIT,
	.name = "LAPICT",
};

void R_LAPICT_INIT() {
	ACPI::Assert(*(const ACPI::RSDP*)uefi_data.acpi_table);
	IC[IRQ_LAPICTimer].setModeRupt(mglb(Handint_LAPICT_Entry), SegCo64);
	register_interrupt_handler(IRQ_LAPICTimer, Handint_LAPICT);
	lapic_timer.Reset();
	lapic_timer.Reset(lapic_timer.Frequency / CONFIG_SysTickFreq);
	if (auto* node = Devsman::RegisterPlatformDevice("lapic-timer@0", "lapic-timer")) {
		Devsman::AddIrqResource(node, IRQ_LAPICTimer);
	}
}

__attribute__((/*interrupt, */target("general-regs-only")))// the stack is ready
void Handint_LAPICT(/*InterruptFrame* frame*/) {
	tick = tick + 1;// mecocoa_global->system_time.mic++
	IC.SendEOI(IRQ_LAPICTimer);
	Systimex::CollectExpired();

	#if _GUI_DOUBLE_BUFFER
	RenderFrameFlush();
	#endif

	Taskman::Schedule();
}
#endif
