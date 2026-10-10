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

_ESYM_C void SvcUserReturn();

// SDRAM as Normal (32MB @0xC0000000, region 0): default map makes it Device, where an unaligned word store faults
static void _mpu_sdram_normal() {
	SCB->SHCSR |= (1u << 16) | (1u << 17) | (1u << 18);// Mem/Bus/Usage fault enable
	MPU->CTRL = MPU->CTRL & ~1u;// disable while writing regions
	MPU->RNR = 0;// region 0 is the kernel one, regions 1 and 2 follow the running task
	MPU->RBAR = 0xC0000000;
	MPU->RASR = (1u << 0) | (24u << 1) | (1u << 19) | (1u << 24);// 0x01080031, AP=001: privileged only
	MPU->CTRL = (1u << 0) | (1u << 2);// ENABLE | PRIVDEFENA
	__DSB();
	__ISB();
	XART1.OutFormat("MPU type=%08X ctrl=%08X rbar=%08X rasr=%08X\r\n",
		(unsigned)MPU->TYPE, (unsigned)MPU->CTRL, (unsigned)MPU->RBAR, (unsigned)MPU->RASR);
}

// ARMv7-M MPU: a region of 2^n bytes starts on a 2^n boundary, its size field holds n-1
static void _mpu_write_region(stduint idx, stduint base, stduint bytes, stduint ap, bool exec) {
	stduint expo = 5;// 32 bytes is the smallest region
	while ((1u << expo) < bytes) expo++;
	MPU->RNR = idx;
	MPU->RBAR = base;
	MPU->RASR = (1u << 0) | ((expo - 1) << 1) | (1u << 19) | (ap << 24) | (exec ? 0u : (1u << 28));
}

static void _mpu_disable_region(stduint idx) {
	MPU->RNR = idx;
	MPU->RASR = MPU->RASR & ~1u;
}

// regions 1 and 2 follow the running task, region 0 is the kernel one
void mpumap_task(ThreadBlock* tb) {
	static stduint mapped_image = 0, mapped_stack = 0;// one core only
	const ProcessBlock* pb = tb ? tb->parent_process : nullptr;
	const stduint image = (pb && pb->load_slices[0].length) ? pb->load_slices[0].address : 0;
	const stduint stack = image ? _IMM(tb->stack_lineaddr) : 0;
	if (image == mapped_image && stack == mapped_stack) return;
	MPU->CTRL = MPU->CTRL & ~1u;// disable while writing regions
	if (image) {
		_mpu_write_region(1, image, pb->load_slices[0].length, 3, true);// image: read, write and execute for the task
		_mpu_write_region(2, stack, tb->stack_size, 3, false);// stack: read and write, never execute
	}
	else {
		_mpu_disable_region(1);
		_mpu_disable_region(2);
	}
	MPU->CTRL = (1u << 0) | (1u << 2);// ENABLE | PRIVDEFENA
	__DSB();
	__ISB();
	mapped_image = image;
	mapped_stack = stack;
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
		(unsigned)SCB->CCR, (unsigned)L1C.getDCacheLineSize());
	return true;
}

