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
# .ifdef CONFIG_SYSCALL
	# https://lore.kernel.org/qemu-devel/20201223192553.332508-1-atish.patra@wdc.com/
	# For qemu version >= 6.0, exception would be raised if no PMP enty is
	# configured. So just configure one entny, which allows all the whole
	# 32-bits physical address range is R/W/X.
	# Details please refer to
	# https://gitlab.com/qemu-project/qemu/-/issues/585 or
	# https://gitee.com/unicornx/riscv-operating-system-mooc/issues/I441IC (in chinese)
	# So it's just a temporary workaround till now to not block people who
	# want to try newer qemu (>= 6.0).
	li      t0, 0xffffffff
	csrw    pmpaddr0, t0
	li      t0, 0xf
	csrw    pmpcfg0, t0
# .endif
	# ---- mstatus ----
	# schedule() in kernel will call MRET to switch to the first task, so parepare mstatus here.
	# ASSUME: that the initial value of mstatus is zero.
	# Set mstatus.MPP  to 3, so run in Machine mode after MRET.
	# Set mstatus.MPIE to 1, so MRET will enable the interrupt.
	#{TODO} set according to ctx.ring
	li	t0, 3 << 11
	csrc	mstatus, t0
	#{} INIT_BSS
	CALL	_preprocess
	J		_entry          # Enter the kernel

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
	mv		t5, t6              # t5 points to the context of current task
	csrr	t6, mscratch        # read t6 back from mscratch
	STORE	t6, 30*SIZE_REG(t5) # save t6 with t5 as base
	csrr	a0, mepc
	STORE	a0, 31*SIZE_REG(t5) # save mepc to context of current task
	csrw	mscratch, t5        # restore the context pointer into mscratch
	# call the C trap handler
	csrr	a0, mepc
	csrr	a1, mcause
	csrr	a2, mscratch
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
	csrw	mscratch, a0		# switch mscratch to point to the context of the next task
	LOAD	a1, 31*SIZE_REG(a0) # set mepc to the pc of the next task
	csrw	mepc, a1
	# Restore all GP registers
	mv		t6, a0
	reg_restore t6
	mret

#{TOMOVE}
# ---- ---- USER SYSCALL ---- ----

.global gethid
gethid:
	li a7, 1 # #define SYS_GETCOREID	1
	ecall
	ret

#

.balign 16 # rv'sp must be 16-byte aligned
stacks:
	.skip	STACK_SIZE * MAXNUM_CPU # for all the harts stacks
	.end #EOF
