// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: LAPIC Timer
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include <c/driver/timer.h>
#include <cpp/Device/ACPI.hpp>


#if _MCCA == 0x8664 && defined(_UEFI)
extern UefiData uefi_data;
extern Dchain TimerManager;
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
	lapic_timer.Reset(lapic_timer.Frequency / SysTickFreq);
	if (auto* node = Devsman::RegisterPlatformDevice("lapic-timer@0", "lapic-timer")) {
		Devsman::AddIrqResource(node, IRQ_LAPICTimer);
	}
}

__attribute__((/*interrupt, */target("general-regs-only")))// the stack is ready
void Handint_LAPICT(/*InterruptFrame* frame*/) {
	tick = tick + 1;// mecocoa_global->system_time.mic++
	IC.SendEOI(IRQ_LAPICTimer);
	{
		extern Spinlock timer_lock;
		SpinlockLocal guard(&timer_lock);
		while (TimerManager.Root()) {
			auto crt = treat<MsgTimer>(TimerManager.Root()->offs);
			if (tick >= crt.timeout) {
				TimerManager.Remove(TimerManager.Root());
				if (crt.hand)
					crt.hand((pureptr_t)crt.timeout, crt.iden);// realtime process
				else {
					message_queue.Enqueue(SysMessage{ SysMessage::RUPT_TIMER, crt });
				}
			}
			else break;
		}
	}

	#if _GUI_DOUBLE_BUFFER
	RenderFrameFlush();
	#endif

	Taskman::Schedule();
}
#endif
