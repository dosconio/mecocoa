#pragma once

#include <c/stdinc.h>

struct BuzzerOutput {
	uint32 frequency_hz;
	uint32 duration_ms;
};

// Submit one tone through the buzzer device O interface.
// frequency_hz == 0 stops the current tone.
// duration_ms == 0 keeps the tone active until another request arrives.
bool BuzzerWrite(const BuzzerOutput& output);
void BuzzerBell(void* context);
