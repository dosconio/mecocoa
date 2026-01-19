# ASCII GAS/RISCV64 TAB4 LF
# Attribute: 
# AllAuthor: @ArinaMgk
# ModuTitle: Startup
# Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/qemuvirt-r64.def.h"
#define LOAD		ld
#define STORE		sd
#define SIZE_REG	8
#define SIZE_PTR .dword
#include "../qemuvirt-r32/qemuvirt.hs"


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

# ---- ---- HANDLER ---- ----

# in machine mode
.globl trap_vector
# the trap vector base address must always be aligned on a 4-byte boundary
.balign 4
trap_vector:
	# save context(registers).
	csrrw	t6, mscratch, t6    # xchg t6 and mscratch
	reg_save t6
	# save the actual t6 register, which we swapped into mscratch
	mv	t5, t6                  # t5 points to the context of current task
	csrr	t6, mscratch        # read t6 back from mscratch
	STORE	t6, 30*SIZE_REG(t5) # save t6 with t5 as base
	csrw	mscratch, t5        # restore the context pointer into mscratch
	# call the C trap handler
	csrr	a0, mepc
	csrr	a1, mcause
	call	trap_handler # return the return address via a0
	csrw	mepc, a0
	csrr	t6, mscratch # restore context(registers).
	reg_restore t6
	mret

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
