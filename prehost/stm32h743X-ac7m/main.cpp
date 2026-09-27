
#include "../../include/mecocoa.hpp"

_ESYM_C void mecocoa();

int main()
{
	mecocoa();
	erro();
}

void erro(const char* str) {
	// LEDR.setMode(GPIOMode::OUT);
	while (true) {
		// LEDR.Toggle();
		for(volatile unsigned i{0}; i < 1000000; i++){}
	}
}
