#ifndef _QEMU_VIRT_RISCV32_DEF_
#define _QEMU_VIRT_RISCV32_DEF_

// QEMU RISC-V Virt machine with 16550a UART and VirtIO MMIO

/* 
 * maximum number of CPUs
 * see https://github.com/qemu/qemu/blob/master/include/hw/riscv/virt.h
 * #define VIRT_CPUS_MAX 8
 */
#define MAXNUM_CPU 8

#define LENGTH_RAM 128*1024*1024


#endif /* _QEMU_VIRT_RISCV32_DEF_ */
