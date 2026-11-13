#include "../../include/mecocoa.hpp"
#include <cpp/MCU/ST/STM32H7>
#include <c/prochip/CortexM7.h>
#include "_openedv/unicfg.hpp"

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
	MPU->RASR = (1u << 0) | (24u << 1) | (1u << 19) | (1u << 17) | (1u << 16) | (1u << 24);// 0x010A0031, AP=001: privileged only, Normal WBWA
	MPU->RNR = 4;// the frame buffer stays non-cacheable: LTDC reads it continuously
	MPU->RBAR = 0xC0000000;
	MPU->RASR = (1u << 0) | (19u << 1) | (1u << 19) | (1u << 24);// 1MB, TEX=001,C=0,B=0: Normal non-cacheable
	MPU->CTRL = (1u << 0) | (1u << 2);// ENABLE | PRIVDEFENA
	__DSB();
	__ISB();
	XART1.OutFormat("MPU type=%08X ctrl=%08X rbar=%08X rasr=%08X\r\n",
		(unsigned)MPU->TYPE, (unsigned)MPU->CTRL, (unsigned)MPU->RBAR, (unsigned)MPU->RASR);
}

static void _mpu_disable_region(stduint idx) {
	MPU->RNR = idx;
	MPU->RASR = MPU->RASR & ~1u;
}

namespace {
	constexpr uint32 H7MpuTraceCapacity = 8;
	struct H7MpuTraceEntry {
		uint32 serial, phase, thread, tid, context;
		uint32 image, length, stack, size, psp, ipsr, icsr;
		uint32 image_base, image_attr, stack_base, stack_attr;
	};
	static_assert(sizeof(H7MpuTraceEntry) == 64, "Keep the MPU trace bounded");
	volatile H7MpuTraceEntry h7_mpu_trace[H7MpuTraceCapacity]{};
	uint32 h7_mpu_trace_next = 0, h7_mpu_trace_count = 0, h7_mpu_trace_serial = 0;

	struct H7MpuWriteProbe {
		uint32 serial, phase, expected_base, expected_attr;
		uint32 selected, previous_base, previous_attr;
		uint32 selected_after_base, base_after_write;
		uint32 final_selected, final_base, final_attr;
	};
	static_assert(sizeof(H7MpuWriteProbe) == 48, "Keep MPU write probes bounded");
	volatile H7MpuWriteProbe h7_mpu_writes[3]{};

	// Single-core recorder; callers keep interrupts masked until the entry is complete.
	volatile H7MpuTraceEntry& BeginH7MpuTrace(ThreadBlock* tb, stduint image, stduint length, stduint stack, stduint size) {
		auto& entry = h7_mpu_trace[h7_mpu_trace_next];
		h7_mpu_trace_next = (h7_mpu_trace_next + 1) % H7MpuTraceCapacity;
		if (h7_mpu_trace_count < H7MpuTraceCapacity) ++h7_mpu_trace_count;
		entry.phase = 1;
		entry.serial = ++h7_mpu_trace_serial;
		entry.thread = _IMM(tb);
		entry.tid = tb ? tb->tid : ~0u;
		entry.context = tb ? _IMM(&tb->context) : 0;
		entry.image = image;
		entry.length = length;
		entry.stack = stack;
		entry.size = size;
		uint32 psp, ipsr;
		_ASM volatile("mrs %0, psp" : "=r"(psp));
		_ASM volatile("mrs %0, ipsr" : "=r"(ipsr));
		entry.psp = psp;
		entry.ipsr = ipsr;
		entry.icsr = SCB->ICSR;
		entry.image_base = entry.image_attr = entry.stack_base = entry.stack_attr = 0;
		return entry;
	}

	void FinishH7MpuTrace(volatile H7MpuTraceEntry& entry, bool reused) {
		const uint32 saved_region = MPU->RNR;
		MPU->RNR = 1;
		__DSB();
		entry.image_base = MPU->RBAR & ~0x1Fu;
		entry.image_attr = MPU->RASR;
		MPU->RNR = 2;
		__DSB();
		entry.stack_base = MPU->RBAR & ~0x1Fu;
		entry.stack_attr = MPU->RASR;
		MPU->RNR = saved_region;
		__DSB();
		entry.phase = reused ? 3 : 2;
	}
}

// ARMv7-M MPU: a region of 2^n bytes starts on a 2^n boundary, its size field holds n-1
static void _mpu_write_region(stduint idx, stduint base, stduint bytes, stduint ap, bool exec) {
	if (!bytes || bytes > (1u << 31)) return;// an untrustworthy length would spin the size search below forever
	stduint expo = 5;// 32 bytes is the smallest region
	while (expo < 31 && (1u << expo) < bytes) expo++;
	base &= ~((1u << expo) - 1u);// the base must be aligned to the region size: round it down, the size still covers the range
	const uint32 attributes = (1u << 0) | ((expo - 1) << 1) | (1u << 19) | (1u << 17) | (1u << 16) | (ap << 24) | (exec ? 0u : (1u << 28));// TEX=001,C=1,B=1: the same Normal WBWA as region 0, a different type for the same RAM lets stale dirty lines write back over the task data
	auto* probe = idx >= 1 && idx <= 3 ? &h7_mpu_writes[idx - 1] : nullptr;
	if (probe) {
		probe->phase = 1;
		probe->serial = h7_mpu_trace_serial;
		probe->expected_base = base;
		probe->expected_attr = attributes;
		probe->selected = probe->previous_base = probe->previous_attr = 0;
		probe->selected_after_base = probe->base_after_write = 0;
		probe->final_selected = probe->final_base = probe->final_attr = 0;
	}
	MPU->RNR = idx;
	if (probe) {
		probe->selected = MPU->RNR;
		probe->previous_base = MPU->RBAR;
		probe->previous_attr = MPU->RASR;
		probe->phase = 2;
	}
	MPU->RBAR = base;
	if (probe) {
		probe->selected_after_base = MPU->RNR;
		probe->base_after_write = MPU->RBAR;
		probe->phase = 3;
	}
	MPU->RASR = attributes;
	if (probe) {
		probe->final_selected = MPU->RNR;
		probe->final_base = MPU->RBAR;
		probe->final_attr = MPU->RASR;
		probe->phase = 4;
	}
}

// Called only by the fault reporter, with interrupts masked; never copy the ring onto the stack.
void DumpH7MpuTrace() {
	XART1.OutFormat("H7MPU trace count=%u phase=1:incomplete,2:updated,3:reused\r\n", (unsigned)h7_mpu_trace_count);
	for (uint32 i = 0; i < h7_mpu_trace_count; ++i) {
		const auto& entry = h7_mpu_trace[(h7_mpu_trace_next + H7MpuTraceCapacity - h7_mpu_trace_count + i) % H7MpuTraceCapacity];
		XART1.OutFormat("H7MPU #%u phase=%u tid=%u tb=%08X ctx=%08X ipsr=%u icsr=%08X psp=%08X\r\n",
			(unsigned)entry.serial, (unsigned)entry.phase, (unsigned)entry.tid, (unsigned)entry.thread,
			(unsigned)entry.context, (unsigned)entry.ipsr, (unsigned)entry.icsr, (unsigned)entry.psp);
		XART1.OutFormat("  want image=%08X len=%08X stack=%08X size=%08X got r1=%08X/%08X r2=%08X/%08X\r\n",
			(unsigned)entry.image, (unsigned)entry.length, (unsigned)entry.stack, (unsigned)entry.size,
			(unsigned)entry.image_base, (unsigned)entry.image_attr, (unsigned)entry.stack_base, (unsigned)entry.stack_attr);
	}
	for (uint32 region = 1; region <= 3; ++region) {
		const auto& probe = h7_mpu_writes[region - 1];
		if (!probe.phase) continue;
		XART1.OutFormat("H7MPUW #%u region=%u step=%u want=%08X/%08X\r\n",
			(unsigned)probe.serial, (unsigned)region, (unsigned)probe.phase,
			(unsigned)probe.expected_base, (unsigned)probe.expected_attr);
		XART1.OutFormat("  select=%u before=%08X/%08X basewrite=%u/%08X final=%u/%08X/%08X\r\n",
			(unsigned)probe.selected, (unsigned)probe.previous_base, (unsigned)probe.previous_attr,
			(unsigned)probe.selected_after_base, (unsigned)probe.base_after_write,
			(unsigned)probe.final_selected, (unsigned)probe.final_base, (unsigned)probe.final_attr);
	}
}

// regions 1 and 2 follow the running task, region 0 is the kernel one
void mpumap_task(ThreadBlock* tb) {
	static stduint mapped_image = 0, mapped_stack = 0, mapped_size = 0, mapped_levl = 0;// one core only
	const ProcessBlock* pb = tb ? tb->parent_process : nullptr;
	const stduint image = (pb && pb->load_slices[0].length) ? pb->load_slices[0].address : 0;
	const stduint stack = image ? _IMM(tb->stack_lineaddr) : 0;
	const stduint size = tb ? tb->stack_size : 0;
	const stduint levl = tb ? _IMM(tb->stack_levladdr) : 0;
	if (image == mapped_image && stack == mapped_stack && size == mapped_size && levl == mapped_levl) {
		const bool trace_if = IC.TryMaskInterrupt();
		auto& trace = BeginH7MpuTrace(tb, image, image ? pb->load_slices[0].length : 0, stack, size);
		FinishH7MpuTrace(trace, true);
		if (trace_if) IC.enInterrupt(true);
		return;
	}
	// the regions must describe the task that is about to run, so no exception may reprogram them in between
	const bool old_if = IC.TryMaskInterrupt();
	auto& trace = BeginH7MpuTrace(tb, image, image ? pb->load_slices[0].length : 0, stack, size);
	// Keep region 0 active: disabling the MPU changes a live SDRAM stack from WBWA to Device (ARM DUI0646).
	__DSB();
	if (image) {
		_mpu_write_region(1, image, pb->load_slices[0].length, 3, true);// image: read, write and execute for the task
		_mpu_write_region(2, stack, size, 3, false);// stack: read and write, never execute
	}
	else {
		_mpu_disable_region(1);
		_mpu_disable_region(2);
	}
	// region 3: a no-access guard at the bottom of this kernel stack.
	if (levl) {
		_mpu_write_region(3, levl, 32, 0, false);
	}
	else {
		_mpu_disable_region(3);
	}
	__DSB();
	__ISB();
	mapped_image = image;
	mapped_stack = stack;
	mapped_size = size;
	mapped_levl = levl;
	FinishH7MpuTrace(trace, false);
	if (old_if) IC.enInterrupt(true);
}

void sdram_init();
bool Memory::initialize(stduint eax, byte* ebx) {
	sdram_init();
	mempool0.Append(Slice{ memreg[0].base, memreg[0].size });
	mempool0.Append(Slice{ memreg[1].base, memreg[1].size });
	mempool_bdma.enable_auto_expand = false;
	mempool_bdma.Append(Slice{ memreg[2].base, memreg[2].size });
	map_ready = true;
	uni_default_allocator = &mempool;
	uni_hostenv_allocator = &mempool;
	_mpu_sdram_normal();
	L1C.enAbleICacheAll();// I-Cache only: it caches flash fetches, and DMA never touches instruction traffic

	L1C.enAbleDCacheAll();// SDRAM is write-back; the frame buffer stays non-cacheable
	__DSB();
	__ISB();
	XART1.OutFormat("CCR=%08X line=%u ic=%u dc=%u\r\n",
		(unsigned)SCB->CCR, (unsigned)L1C.getDCacheLineSize(),
		(unsigned)((SCB->CCR >> 17) & 1), (unsigned)((SCB->CCR >> 16) & 1));
	return true;
}

