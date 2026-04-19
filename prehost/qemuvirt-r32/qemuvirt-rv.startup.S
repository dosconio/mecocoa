# ASCII GAS/RISCV64 TAB4 LF
# Attribute: 
# AllAuthor: @ArinaMgk
# ModuTitle: Startup
# Copyright: Dosconio Mecocoa, BSD 3-Clause License
#include "../../include/qemuvirt-riscv.hpp"
#include "../qemuvirt-r32/qemuvirt.hs"
# using:
# .section .rodata
# .global HEAP_START
#    HEAP_START: SIZE_PTR _heap_start

.equ	STACK_SIZE, 4096

.global	_start

.section .text.boot

_start:
	# only use single hart(core), park harts with id != 0
	CSRR	t0, mhartid		# read current hart id
	MV		tp, t0			# keep CPU's hartid in its tp for later usage.
	BNEZ	t0, park
	# Setup stacks
	SLLI	t0, t0, 10		# shl: mul the hart id by 1024
	LA		sp, stacks + STACK_SIZE
	ADD		sp, sp, t0		# move hart-loc stack pointer to its stack space
	li      t0, 0x2000      # mstatus.FS [14:13] : Floating-point state
	csrs    mstatus, t0     # 
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
	#{} INIT_BSS
	CALL	_preprocess
	J		_entry          # Enter the kernel

park:
	WFI 	#== x86: HLT
	J		park

# ---- ---- HANDLER ---- ----

# in machine mode, PG is disabled
.globl trap_vector
.balign 4
trap_vector:
	# save context(registers).
	# mscratch now holds the original t6 (points to the NormalTaskContext of the current task)
	csrrw	t6, mscratch, t6    # xchg t6 and mscratch
	# Where it from
	csrr    t5, mstatus
	srli    t5, t5, 11
	andi    t5, t5, 0x3
	beqz    t5, .L_from_user      # U
	li      t4, 1
	beq     t5, t4, .L_from_user  # S
	j       .L_nested_trap        # M

# ================== User Trap ====================
.L_from_user:
	reg_save t6
	csrr    t5, mscratch
	STORE   t5, 30*SIZE_REG(t6)
	#
	csrr    a0, mepc
	STORE   a0, 31*SIZE_REG(t6)
	csrr    a0, mstatus
	STORE   a0, 32*SIZE_REG(t6)
	csrr    a0, satp
	STORE   a0, 33*SIZE_REG(t6)
	csrw    mscratch, t6 # PCB
	# Stack Balance
	#if   __BITS__==32
	LOAD      t0, 164(t6)
	LOAD      t1, 156(t6)
	#elif __BITS__==64
	LOAD      t0, 328(t6)
	LOAD      t1, 312(t6)
	#endif
	add     t0, t0, t1
	addi    sp, t0, -16
	#
	csrr    a0, mepc
	csrr    a1, mcause
	csrr    a2, mscratch
	call    trap_handler

# ---- for SwitchTaskContext : new task and mret ----
.globl .L_restore_user_context
.L_restore_user_context:
	csrr    t6, mscratch # not csrr t6, mscratch
	LOAD    t0, 33*SIZE_REG(t6)
	csrr    t1, satp
	BEQ     t0, t1, .L_skip_satp_restore
	csrw    satp, t0
	sfence.vma zero, zero
.L_skip_satp_restore:
	LOAD    t0, 32*SIZE_REG(t6)
	csrw    mstatus, t0
	LOAD    t0, 31*SIZE_REG(t6)
	csrw    mepc, t0
	reg_restore t6
	mret
# ================== M Trap ====================
.L_nested_trap:
	csrrw   t6, mscratch, t6
	addi    sp, sp, -SIZE_REG*36
	# Caller-saved
	STORE   ra,  1*SIZE_REG(sp)
	STORE   t0,  5*SIZE_REG(sp)
	STORE   t1,  6*SIZE_REG(sp)
	STORE   t2,  7*SIZE_REG(sp)
	STORE   a0, 10*SIZE_REG(sp)
	STORE   a1, 11*SIZE_REG(sp)
	STORE   a2, 12*SIZE_REG(sp)
	STORE   a3, 13*SIZE_REG(sp)
	STORE   a4, 14*SIZE_REG(sp)
	STORE   a5, 15*SIZE_REG(sp)
	STORE   a6, 16*SIZE_REG(sp)
	STORE   a7, 17*SIZE_REG(sp)
	STORE   t3, 28*SIZE_REG(sp)
	STORE   t4, 29*SIZE_REG(sp)
	STORE   t5, 30*SIZE_REG(sp)
	STORE   t6, 31*SIZE_REG(sp)
	#
	csrr    t0, mepc
	STORE   t0, 32*SIZE_REG(sp)
	csrr    t0, mstatus
	STORE   t0, 33*SIZE_REG(sp)
	# prepare for trap_handler
	csrr    a0, mepc
	csrr    a1, mcause
	csrr    a2, mscratch
	call    trap_handler
	#
	LOAD    t0, 32*SIZE_REG(sp)
	csrw    mepc, t0
	LOAD    t0, 33*SIZE_REG(sp)
	csrw    mstatus, t0
	#
	LOAD    ra,  1*SIZE_REG(sp)
	LOAD    t0,  5*SIZE_REG(sp)
	LOAD    t1,  6*SIZE_REG(sp)
	LOAD    t2,  7*SIZE_REG(sp)
	LOAD    a0, 10*SIZE_REG(sp)
	LOAD    a1, 11*SIZE_REG(sp)
	LOAD    a2, 12*SIZE_REG(sp)
	LOAD    a3, 13*SIZE_REG(sp)
	LOAD    a4, 14*SIZE_REG(sp)
	LOAD    a5, 15*SIZE_REG(sp)
	LOAD    a6, 16*SIZE_REG(sp)
	LOAD    a7, 17*SIZE_REG(sp)
	LOAD    t3, 28*SIZE_REG(sp)
	LOAD    t4, 29*SIZE_REG(sp)
	LOAD    t5, 30*SIZE_REG(sp)
	LOAD    t6, 31*SIZE_REG(sp)
	addi    sp, sp, SIZE_REG*36
	mret

# ---- ---- TASKMAN ---- ----

# ---- void DirectTaskContext(struct context *next);
# a0: pointer to the context of the next task
.globl DirectTaskContext
.balign 4
DirectTaskContext:
	csrw	mscratch, a0		# mscratch -> context of the next task

	LOAD    t0, 33*SIZE_REG(a0)
	csrr    t1, satp
	BEQ     t0, t1, .L_skip_satp_direct
	csrw    satp, t0
	sfence.vma zero, zero       # Flush TLB immediately after changing satp
.L_skip_satp_direct:

	# Restore Privilege & Execution State
	LOAD    t0, 32*SIZE_REG(a0)
	csrw    mstatus, t0         # Restores MPP (Next mode: M, S, or U)

	LOAD	t0, 31*SIZE_REG(a0) # set mepc to the pc of the next task
	csrw	mepc, t0
	# Restore all GP registers
	mv		t6, a0
	reg_restore t6
mret # will enable global interrupt

.globl SwitchTaskContext
.balign 4
SwitchTaskContext: # a0: new_pb->context, a1: old_pb->context
	# Callee-saved
	addi    sp, sp, -15*SIZE_REG
	STORE   ra,   0*SIZE_REG(sp)
	STORE   gp,   1*SIZE_REG(sp)
	STORE   tp,   2*SIZE_REG(sp)
	STORE   s0,   3*SIZE_REG(sp)
	STORE   s1,   4*SIZE_REG(sp)
	STORE   s2,   5*SIZE_REG(sp)
	STORE   s3,   6*SIZE_REG(sp)
	STORE   s4,   7*SIZE_REG(sp)
	STORE   s5,   8*SIZE_REG(sp)
	STORE   s6,   9*SIZE_REG(sp)
	STORE   s7,  10*SIZE_REG(sp)
	STORE   s8,  11*SIZE_REG(sp)
	STORE   s9,  12*SIZE_REG(sp)
	STORE   s10, 13*SIZE_REG(sp)
	STORE   s11, 14*SIZE_REG(sp)
	STORE   sp, 34*SIZE_REG(a1)
	STORE   ra, 35*SIZE_REG(a1)
	#
	csrw    mscratch, a0
	LOAD    t0, 33*SIZE_REG(a0)
	csrr    t1, satp
	BEQ     t0, t1, .L_skip_satp_switch
	csrw    satp, t0
	sfence.vma zero, zero # fence.i
.L_skip_satp_switch:
	# Judge new task (kernel_ra == 0)
	LOAD    ra, 35*SIZE_REG(a0)
	beqz    ra, .L_first_time_switch
	# Old Task: Restore Kernel Stack
	LOAD    sp, 34*SIZE_REG(a0)
	# Callee-saved
	LOAD    ra,   0*SIZE_REG(sp)
	LOAD    gp,   1*SIZE_REG(sp)
	LOAD    tp,   2*SIZE_REG(sp)
	LOAD    s0,   3*SIZE_REG(sp)
	LOAD    s1,   4*SIZE_REG(sp)
	LOAD    s2,   5*SIZE_REG(sp)
	LOAD    s3,   6*SIZE_REG(sp)
	LOAD    s4,   7*SIZE_REG(sp)
	LOAD    s5,   8*SIZE_REG(sp)
	LOAD    s6,   9*SIZE_REG(sp)
	LOAD    s7,  10*SIZE_REG(sp)
	LOAD    s8,  11*SIZE_REG(sp)
	LOAD    s9,  12*SIZE_REG(sp)
	LOAD    s10, 13*SIZE_REG(sp)
	LOAD    s11, 14*SIZE_REG(sp)
	addi    sp, sp, 15*SIZE_REG
ret

.L_first_time_switch: j .L_restore_user_context
#

.balign 16 # rv'sp must be 16-byte aligned
stacks:
	.skip	STACK_SIZE * MAXNUM_CPU # for all the harts stacks
kernel_stack:
	.skip	0x1000 * 1 # for all the harts stacks
.global kernel_stack_top_cpu0
kernel_stack_top_cpu0:
	.end #EOF
