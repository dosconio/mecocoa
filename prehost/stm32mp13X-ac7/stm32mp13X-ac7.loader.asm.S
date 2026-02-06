
.syntax unified
.cpu cortex-a7
.fpu neon-vfpv4
.code 32
.section .text.Reset_Handler, "ax"
.align 4

.global Reset_Handler
.extern Vectors
.extern SystemInit
.extern __libc_init_array
.extern main

/* Definitions of mode bits */
.equ MODE_USR, 0x10
.equ MODE_FIQ, 0x11
.equ MODE_IRQ, 0x12
.equ MODE_SVC, 0x13
.equ MODE_ABT, 0x17
.equ MODE_UND, 0x1B
.equ MODE_SYS, 0x1F

Reset_Handler:
	/* Mask interrupts */
	CPSID   if

	/* Put any cores other than 0 to sleep */
	MRC     p15, 0, R0, c0, c0, 5    /* Read MPIDR */
	ANDS    R0, R0, #3
	
	goToSleep:
	/* Logic equivalent to ITT NE / WFINE / BNE in 32-bit ARM mode
	WFE
	CMP     R0, #0
	BNE     goToSleep
	*/
	ITT  NE
	WFINE
	BNE     goToSleep

	/* Reset SCTLR Settings */
	MRC     p15, 0, R0, c1, c0, 0    /* Read CP15 System Control register */
	BIC     R0, R0, #(0x1 << 12)     /* Clear I bit 12 to disable I Cache */
	BIC     R0, R0, #(0x1 <<  2)     /* Clear C bit  2 to disable D Cache */
	BIC     R0, R0, #0x1             /* Clear M bit  0 to disable MMU */
	BIC     R0, R0, #(0x1 << 11)     /* Clear Z bit 11 to disable branch prediction */
	BIC     R0, R0, #(0x1 << 13)     /* Clear V bit 13 to disable hivecs */
	BIC     R0, R0, #(0x1 << 29)     /* Clear AFE bit 29 to enable the full range of access permissions */
	ORR     R0, R0, #(0x1 << 30)     /* Set TE bit to take exceptions in Thumb mode */
	MCR     p15, 0, R0, c1, c0, 0    /* Write value back to CP15 System Control register */
	ISB

	/* Configure ACTLR */
	MRC     p15, 0, r0, c1, c0, 1    /* Read CP15 Auxiliary Control Register */
	ORR     r0, r0, #(1 <<  1)       /* Enable L2 prefetch hint (UNK/WI since r4p1) */
	MCR     p15, 0, r0, c1, c0, 1    /* Write CP15 Auxiliary Control Register */

	/* Set Vector Base Address Register (VBAR) to point to this application's vector table */
	LDR     R0, =Vectors
	MCR     p15, 0, R0, c12, c0, 0
	ISB

	/* Setup Stack for each exceptional mode */
	/* FIQ mode stack */
	CPS     #MODE_FIQ
	LDR     SP, =FIQ_STACK

	/* IRQ mode stack */
	CPS     #MODE_IRQ
	LDR     SP, =IRQ_STACK

	/* Supervisor mode stack */
	CPS     #MODE_SVC
	LDR     SP, =SVC_STACK

	/* Abort mode stack */
	CPS     #MODE_ABT
	LDR     SP, =ABT_STACK

	/* Undefined mode stack */
	CPS     #MODE_UND
	LDR     SP, =UND_STACK

	/* System mode stack */
	CPS     #MODE_SYS
	LDR     SP, =SYS_STACK

	/* do clear BSS and others */
	BL      SystemInit

	/* Unmask interrupts */
	CPSIE   if

	/* Initialize libc */
	BL      __libc_init_array

	/* transfer to successor */
	BL      main

	/* Safety loop */
	dead_loop: B       dead_loop
