// ASCII g++ TAB4 LF
// Cortex-M scheduler context-switch commit hooks called by PendSV.

#include "../../../include/mecocoa.hpp"

#if defined(_ARCH_ARM_ProfileM)

CortexMSwitchControl cortexm_switch_control;

extern "C" void CortexMTaskSwitchEnter() {
	cortexm_switch_control.phase = CortexMSwitchPhase::InProgress;
	CortexMSwitchBarrier();
	mpumap_task(cortexm_switch_control.incoming);
}

extern "C" void CortexMTaskSwitchCommit() {
	const stduint cpuid = cortexm_switch_control.cpu_id;
	ThreadBlock* const outgoing = cortexm_switch_control.outgoing;
	Taskman::current_thread(cpuid) = cortexm_switch_control.incoming;
	if (outgoing) outgoing->just_schedule = 0;
	CortexMSwitchBarrier();
	cortexm_switch_control.phase = CortexMSwitchPhase::Idle;
}

#endif
