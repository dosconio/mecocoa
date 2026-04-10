#ifndef _QEMU_VIRT_RISCV_
#define _QEMU_VIRT_RISCV_
#ifndef _DEV_GNU_AS
#include <c/stdinc.h>
#endif

// ---- ---- ---- ---- qemuvirt ---- ---- ---- ----
#if __BITS__==32
// QEMU RISC-V Virt machine with 16550a UART and VirtIO MMIO

#define MCAUSE_MASK_INTERRUPT _IMM(0x80000000)
#define MCAUSE_MASK_ECODE     _IMM(0x7FFFFFFF)

#define LOAD		lw
#define STORE		sw
#define SIZE_REG	4
#define SIZE_PTR .word

#elif __BITS__==64
// QEMU RISC-V Virt machine with 16550a UART and VirtIO MMIO

#define MCAUSE_MASK_INTERRUPT _IMM(0x8000000000000000)
#define MCAUSE_MASK_ECODE     _IMM(0x7FFFFFFFFFFFFFFF)

#define LOAD		ld
#define STORE		sd
#define SIZE_REG	8
#define SIZE_PTR .dword

#endif

/*
 * maximum number of CPUs
 * see https://github.com/qemu/qemu/blob/master/include/hw/riscv/virt.h
 * #define VIRT_CPUS_MAX 8
 */
#define MAXNUM_CPU 8

#define LENGTH_RAM 128*1024*1024



#endif
