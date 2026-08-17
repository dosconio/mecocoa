#pragma once

#include <c/stdinc.h>

enum class IsaDmaDirection : uint8 {
	MemoryToDevice,
	DeviceToMemory,
};

bool IsaDma8Prepare(
	uint8 channel,
	stduint physical_address,
	uint32 length,
	IsaDmaDirection direction);

void IsaDmaMask(uint8 channel);
void IsaDmaUnmask(uint8 channel);
