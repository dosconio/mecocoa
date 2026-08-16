// UTF-8 g++ TAB4 LF
// ModuTitle: PC Speaker Buzzer
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include "au-buzzer.hpp"

#if _MCCA == 0x8632
bool PIT_SetChannel2Frequency(uint32 frequency_hz);

namespace {
	constexpr uint32 BuzzerBellFrequencyHz = 440;
	constexpr uint32 BuzzerBellDurationMs = 100;

	Spinlock buzzer_lock;
	stduint buzzer_generation;
	DeviceNode* buzzer_node;

	void BuzzerStopTimer(pureptr_t, stduint generation) {
		SpinlockLocal guard(&buzzer_lock);
		if (generation != buzzer_generation) return;
		byte gate = innpb(PORT_SPEAKER);
		outpb(PORT_SPEAKER, byte(gate & ~0x03));
	}

	stdsint BuzzerSend(DeviceNode* node, const void* buf, stduint count,
		stduint idx, stduint flags) {
		(void)node;
		if (!buf || count != sizeof(BuzzerOutput) || idx || flags) return -1;

		const auto& output = *static_cast<const BuzzerOutput*>(buf);
		stduint generation;
		{
			SpinlockLocal guard(&buzzer_lock);
			byte gate = innpb(PORT_SPEAKER);
			if (!output.frequency_hz) {
				buzzer_generation++;
				outpb(PORT_SPEAKER, byte(gate & ~0x03));
				return stdsint(count);
			}
			if (!PIT_SetChannel2Frequency(output.frequency_hz)) return -1;
			outpb(PORT_SPEAKER, byte(gate | 0x03));
			generation = ++buzzer_generation;
		}

		if (output.duration_ms) {
			stduint timeout = stduint(
				(uint64(output.duration_ms) * CONFIG_SysTickFreq + 999) / 1000);
			SysTimer::Append(timeout, generation, (_tocall_ft)BuzzerStopTimer);
		}
		return stdsint(count);
	}

	const DeviceNodeOps buzzer_ops{
		.read = nullptr,
		.send = BuzzerSend,
		.ctrl = nullptr,
	};
}

bool BuzzerWrite(const BuzzerOutput& output) {
	if (!buzzer_node) return false;
	return Devsman::Send(buzzer_node, &output, sizeof(output)) ==
		(stdsint)sizeof(output);
}

void BuzzerBell(void*) {
	const BuzzerOutput output{
		.frequency_hz = BuzzerBellFrequencyHz,
		.duration_ms = BuzzerBellDurationMs,
	};
	(void)BuzzerWrite(output);
}

_ESYM_C void R_BUZZER_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_BUZZER{
	.init = R_BUZZER_INIT,
	.name = "Buzzer",
};

void R_BUZZER_INIT() {
	if (auto* node = Devsman::RegisterPlatformDevice("buzzer", "buzzer")) {
		Devsman::AddIoPortResource(node, 0, PORT_SPEAKER, 1);
		Devsman::SetOps(node, &buzzer_ops);
		buzzer_node = node;
	}
}
#endif
