// UTF-8 g++ TAB4 LF 
// AllAuthor: @ArinaMgk
// ModuTitle: LAPIC Timer
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"


#if _MCCA == 0x8664 && defined(_UEFI)
extern Dchain TimerManager;
void RenderFrameFlush();

__attribute__((/*interrupt, */target("general-regs-only")))// the stack is ready
void Handint_LAPICT(/*InterruptFrame* frame*/) {
	tick = tick + 1;// mecocoa_global->system_time.mic++
	sendEOI();
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
	#if _GUI_DOUBLE_BUFFER
	RenderFrameFlush();
	#endif

	Taskman::Schedule();
}
#endif
