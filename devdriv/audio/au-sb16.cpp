// UTF-8 g++ TAB4 LF
// ModuTitle: Sound Blaster 16
// Copyright: Dosconio Mecocoa, BSD 3-Clause License

#include "../../include/mecocoa.hpp"
#include "au-sb16.hpp"
#include <c/driver/i8259A.h>
#include <cpp/Device/Audio/SoundBlaster.hpp>

#if _MCCA == 0x8632
namespace {
	volatile uint32 sound_blaster_irq_count;
	volatile bool sound_blaster_irq_test_armed;
	volatile bool sound_blaster_irq_test_passed;
	volatile bool sound_blaster_irq_storm_reported;

	uint8 SoundBlasterRead8(void*, uint16 port) {
		return innpb(port);
	}

	void SoundBlasterWrite8(void*, uint16 port, uint8 value) {
		outpb(port, value);
	}

	void SoundBlasterDelayUs(void*, uint32 microseconds) {
		for (uint32 count = 0; count < microseconds; ++count) {
			outpb(0x80, 0);
		}
	}

	const uni::SoundBlasterIo sound_blaster_io{
		.context = nullptr,
		.read8 = SoundBlasterRead8,
		.write8 = SoundBlasterWrite8,
		.delay_us = SoundBlasterDelayUs,
	};

	uni::SoundBlaster sound_blaster{
		SoundBlasterDefaultConfig.io_base,
		sound_blaster_io,
	};

	bool ArmSoundBlasterIrqTest() {
		sound_blaster.Acknowledge8BitIrq();
		sound_blaster_irq_count = 0;
		sound_blaster_irq_test_passed = false;
		sound_blaster_irq_storm_reported = false;
		sound_blaster_irq_test_armed = true;
		if (!sound_blaster.Trigger8BitIrq()) {
			sound_blaster_irq_test_armed = false;
			plogwarn("[SB16] Failed to trigger IRQ5 test");
			return false;
		}
		ploginfo("[SB16] IRQ5 test armed");
		return true;
	}

	bool StartSoundBlaster(DeviceNode* node) {
		const auto* io_resource = Devsman::FindResource(
			node, DeviceResourceType::IoPortRange, 0);
		if (!io_resource || io_resource->length < SoundBlasterDefaultConfig.io_length ||
			io_resource->start > 0xFFFF) {
			plogwarn("[SB16] Invalid or missing I/O resource");
			return false;
		}

		sound_blaster = uni::SoundBlaster(
			uint16(io_resource->start), sound_blaster_io);
		if (!sound_blaster.Probe()) {
			plogwarn("[SB16] DSP probe failed at %[16H]", uint16(io_resource->start));
			return false;
		}

		ploginfo("[SB16] DSP reset ok");
		ploginfo("[SB16] DSP version %u.%u",
			(stduint)sound_blaster.GetDspMajorVersion(),
			(stduint)sound_blaster.GetDspMinorVersion());
		return ArmSoundBlasterIrqTest();
	}
}

void Handint_SB16() {
	sound_blaster.Acknowledge8BitIrq();
	const uint32 count = sound_blaster_irq_count + 1;
	sound_blaster_irq_count = count;
	IC.SendEOI(IRQ_SB16);
	if (sound_blaster_irq_test_armed) {
		sound_blaster_irq_test_armed = false;
		sound_blaster_irq_test_passed = true;
		ploginfo("[SB16] IRQ5 test ok count=%u", (stduint)count);
	}
	else if (sound_blaster_irq_test_passed &&
		!sound_blaster_irq_storm_reported) {
		sound_blaster_irq_storm_reported = true;
		plogwarn("[SB16] Unexpected extra IRQ5 count=%u", (stduint)count);
	}
}

_ESYM_C void R_SB16_INIT();

__attribute__((section(".init.rmod")))
RMOD_LIST RMOD_LIST_SB16{
	.init = R_SB16_INIT,
	.name = "Sound Blaster 16",
};

void R_SB16_INIT() {
	auto* node = Devsman::RegisterPlatformDevice("sound-blaster");
	if (!node) return;
	if (!Devsman::AddIoPortResource(node, 0,
		SoundBlasterDefaultConfig.io_base, SoundBlasterDefaultConfig.io_length)) {
		plogwarn("[SB16] Failed to register I/O resource");
		return;
	}
	if (!Devsman::AddIrqResource(node, IRQ_SB16)) {
		plogwarn("[SB16] Failed to register IRQ resource");
		return;
	}
	if (!Devsman::AddDmaResource(node, 0, SoundBlasterDefaultConfig.dma8, 8) ||
		!Devsman::AddDmaResource(node, 1, SoundBlasterDefaultConfig.dma16, 16)) {
		plogwarn("[SB16] Failed to register DMA resources");
		return;
	}
	IC[IRQ_SB16].setRange(mglb(Handint_SB16_Entry), SegCo32);
	register_interrupt_handler(IRQ_SB16, Handint_SB16);
	if (IC.getType() == 0) i8259Master_Enable(DEV_MAS_SB16);
	if (!Devsman::RegisterDriverStarter("sb16", StartSoundBlaster)) {
		plogwarn("[SB16] Failed to register driver starter");
		return;
	}
	(void)Devsman::RegisterPlatformDevice(
		"sound-blaster", "sb16", &sound_blaster);
}
#endif
