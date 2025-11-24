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

.balign 16 # rv'sp must be 16-byte aligned
stacks:
	.skip	STACK_SIZE * MAXNUM_CPU # for all the harts stacks
	.end #EOF
