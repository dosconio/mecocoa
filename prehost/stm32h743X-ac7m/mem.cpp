#include "../../include/mecocoa.hpp"
#include <cpp/MCU/ST/STM32H7>
#include <c/prochip/CortexM7.h>
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
	{ "SDRAM", SDRAM_BANK1_BASE + SDRAM_POOL_OFF, 1024U * 1024U * 31 },
	{ "SRAM12",SRAM12_ADDR,                       64U * 1024U },
	{ "SRAM4", SRAM4_ADDR,                        32U * 1024U },
};

uni::Mempool mempool_bdma;// SRAM4

// SDRAM as Normal (32MB @0xC0000000, region 7): default map makes it Device, where an unaligned word store faults
static void _mpu_sdram_normal() {
	Reference(0xE000ED24) |= (1u << 16) | (1u << 17) | (1u << 18);// SHCSR: Mem/Bus/Usage fault enable
	Reference(0xE000ED94).rstof(0);// MPU_CTRL: disable while writing regions
	Reference(0xE000ED98) = 7;// RNR
	Reference(0xE000ED9C) = 0xC0000000;// RBAR
	Reference(0xE000EDA0) = (1u << 0) | (24u << 1) | (1u << 19) | (3u << 24);// RASR=0x03080031
	Reference(0xE000ED94) = (1u << 0) | (1u << 2);// ENABLE | PRIVDEFENA
	__DSB();
	__ISB();
	XART1.OutFormat("MPU type=%08X ctrl=%08X rbar=%08X rasr=%08X\r\n",
		(unsigned)_IMM(Reference(0xE000ED90)), (unsigned)_IMM(Reference(0xE000ED94)),
		(unsigned)_IMM(Reference(0xE000ED9C)), (unsigned)_IMM(Reference(0xE000EDA0)));
}

bool Memory::initialize(stduint eax, byte* ebx) {
	sdram_init();
	mempool0.Append(Slice{ memreg[0].base, memreg[0].size });
	mempool0.Append(Slice{ memreg[1].base, memreg[1].size });
	mempool_bdma.enable_auto_expand = false;
	mempool_bdma.Append(Slice{ memreg[2].base, memreg[2].size });
	map_ready = true;
	uni_default_allocator = &mempool;
	_mpu_sdram_normal();

	L1C.enAbleDCacheAll();
	XART1.OutFormat("CCR=%08X line=%u\r\n",
		(unsigned)_IMM(Reference(0xE000ED14)), (unsigned)L1C.getDCacheLineSize());
	return true;
}

