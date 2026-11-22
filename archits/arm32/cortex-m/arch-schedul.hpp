// ASCII C++2x TAB4 LF
// Cortex-M scheduler switch state shared by the generic scheduler and PendSV.

#ifndef MCCA_ARCH_ARM32_CORTEX_M_ARCH_SCHEDUL_HPP_
#define MCCA_ARCH_ARM32_CORTEX_M_ARCH_SCHEDUL_HPP_

struct ThreadBlock;

enum class CortexMSwitchPhase : stduint {
	Idle,
	Pending,
	InProgress,
};

struct CortexMSwitchControl {
	volatile CortexMSwitchPhase phase = CortexMSwitchPhase::Idle;
	ThreadBlock* outgoing = nullptr;
	ThreadBlock* incoming = nullptr;
	stduint cpu_id = 0;
	volatile stduint deferred_schedule = 0;
};

static inline bool CortexMInThreadMode() {
	stduint exception_number;
	_ASM volatile("mrs %0, ipsr" : "=r"(exception_number));
	return exception_number == 0;
}

static inline void CortexMSwitchBarrier() {
	_ASM volatile("dmb" ::: "memory");
}

extern CortexMSwitchControl cortexm_switch_control;

extern "C" void CortexMTaskSwitchEnter();
extern "C" void CortexMTaskSwitchCommit();

#endif
