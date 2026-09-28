#include "../../include/mecocoa.hpp"
#include <cpp/MCU/ST/STM32H7>

extern "C" char _IDN_BOARD[16] {"STM32H743IIT6"};

_ESYM_C void mecocoa();
extern OstreamTrait* con0_out;

void _idle() {
	GPIN& LEDB = GPIOB[ 0];
	LEDB.setMode(GPIOMode::OUT);
	while (true) {
		LEDB.Toggle();
		for(volatile unsigned i{0}; i < 1000000; i++){}
	}
}

int main()
{
	con0_out = &XART1;
	if (!RCC.setClock(SysclkSource::HSE)) erro();
	mecocoa();
	_idle();
	erro();
}

void erro(const char* str) {
	GPIN& LEDR = GPIOB[ 1];
	LEDR.setMode(GPIOMode::OUT);
	while (true) {
		LEDR.Toggle();
		for(volatile unsigned i{0}; i < 1000000; i++){}
	}
}

//

bool Memory::initialize(stduint eax, byte* ebx) { return true; _TODO }

OstreamTrait* con0_out = 0;

ThreadBlock* PCU_CORES_current_thread[PCU_CORES_MAX];

stduint Taskman::getID() { return _TEMP 0; } // H743 only

void ThreadBlock::Block(BlockReason reason) {}
void ThreadBlock::Unblock(BlockReason reason) {}

void Taskman::SleepAndRelease(Spinlock* lk) {}
