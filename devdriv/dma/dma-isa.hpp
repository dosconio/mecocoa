#pragma once

#include <cpp/Device/DMA>

using IsaDmaDirection = uni::DMA8237Direction;
using IsaDmaReloadMode = uni::DMA8237ReloadMode;

bool IsaDma8Prepare(
	uint8 channel,
	stduint physical_address,
	uint32 length,
	IsaDmaDirection direction,
	IsaDmaReloadMode reload_mode = IsaDmaReloadMode::OneShot);

void IsaDmaMask(uint8 channel);
void IsaDmaUnmask(uint8 channel);
