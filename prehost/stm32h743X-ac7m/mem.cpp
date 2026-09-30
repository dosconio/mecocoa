#include "../../include/mecocoa.hpp"
#include <cpp/MCU/ST/STM32H7>
#include "_openedv/SDRAM.hpp"

// SDRAM：外部 SDRAM 1MB 段（避开 0x0 起的测试区）
#define SDRAM_POOL_OFF 0x100000
// SRAM12 / SRAM4 / ITCM：scatter 未占用，直接用固定地址
#define SRAM12_ADDR ((stduint)0x30000000)
#define SRAM4_ADDR  ((stduint)0x38000000)
#define ITCM_ADDR   ((stduint)0x00000000)


struct MemRegion {
	const char* name;
	stduint base;
	stduint size;
};
static const MemRegion memreg[] = {
	{ "SDRAM", SDRAM_BANK1_BASE + SDRAM_POOL_OFF, 1024U * 1024U },
	{ "SRAM12",SRAM12_ADDR,                       64U * 1024U },
	{ "SRAM4", SRAM4_ADDR,                        32U * 1024U },
};

uni::Mempool mempool_bdma;// SRAM4


bool Memory::initialize(stduint eax, byte* ebx) {
	sdram_init();
	mempool0.Append(Slice{ memreg[0].base, memreg[0].size });
	mempool0.Append(Slice{ memreg[1].base, memreg[1].size });
	mempool_bdma.enable_auto_expand = false;
	mempool_bdma.Append(Slice{ memreg[2].base, memreg[2].size });
	return true;
}

