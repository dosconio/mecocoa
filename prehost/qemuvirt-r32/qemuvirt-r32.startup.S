# ASCII GAS/RISCV64 TAB4 LF
# Attribute: 
# AllAuthor: @ArinaMgk
# ModuTitle: Startup
# Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/qemuvirt-r32.def.h"
#define LOAD		lw
#define STORE		sw
#define SIZE_REG	4
#define SIZE_PTR .word
#include "../qemuvirt-r32/qemuvirt.hs"
# using:
# .section .rodata
# .global HEAP_START
#    HEAP_START: SIZE_PTR _heap_start

.equ	STACK_SIZE, 1024

.global	_start

.text

_start:
	# only use single hart(core), park harts with id != 0
	CSRR	t0, mhartid		# read current hart id
	MV		tp, t0			# keep CPU's hartid in its tp for later usage.
	BNEZ	t0, park
	# Setup stacks
	SLLI	t0, t0, 10		# shl: mul the hart id by 1024
	LA		sp, stacks + STACK_SIZE
	ADD		sp, sp, t0		# move hart-loc stack pointer to its stack space
	# Enter the kernel
	J		_entry

park:
	WFI 	#== x86: HLT
	J		park

# ---- ---- TASKMAN ---- ----

# ---- void switch_to(struct context *next);
# a0: pointer to the context of the next task
.globl switch_to
.balign 4
switch_to:
	csrrw	t6, mscratch, t6	# swap t6 and mscratch
	# Note: the first time switch_to() is called, mscratch is initialized as zero (in sched_init())
	beqz	t6, 1f
	reg_save t6
	# Save the actual t6 register, which we swapped into mscratch
	mv		t5, t6				# t5 points to the context of current task
	csrr	t6, mscratch		# read t6 back from mscratch
	STORE	t6, 30*SIZE_REG(t5)	# save t6 with t5 as base
1:
	csrw	mscratch, a0		# switch mscratch to point to the context of the next task
	# Restore all GP registers
	mv		t6, a0
	reg_restore t6
	ret

.balign 16 # rv'sp must be 16-byte aligned
stacks:
	.skip	STACK_SIZE * MAXNUM_CPU # for all the harts stacks
	.end #EOF
