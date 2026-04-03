#define _DEV_GNU_AS
#include "../../include/qemuvirt-riscv.hpp"

.global syscall_bridge
syscall_bridge:
	ecall
ret

.section .text.entry, "ax", @progbits
.global _start
.type _start, @function
_start:

# Disable Relaxation and Initialize Global Pointer (gp)
	.option push
	.option norelax
	la gp, __global_pointer$
	.option pop

# Stack
	andi sp, sp, -16

# BSS
	la t0, __bss_start
	la t1, _end
	bgeu t0, t1, .L_bss_done
.L_bss_loop:
	sw zero, 0(t0)      # if RV64 and aligned, can use sd
	addi t0, t0, 4
	bltu t0, t1, .L_bss_loop
.L_bss_done:
	# call _preprocess
.L_init_done:
	#li a0, 0    #{} argc = 0
	#li a1, 0    #{} argv = NULL
	call main
	call exit   #(exit_code)
.L_dead: 	j .L_dead
.size _start, . - _start