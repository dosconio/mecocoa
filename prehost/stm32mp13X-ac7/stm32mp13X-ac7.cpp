#define _DEBUG

#include <cpp/MCU/ST/STM32MP13>
#include <c/driver/RCC/RCC-registers.hpp>
#include <c/proctrl/ARM.h>
#include <c/msgface.h>


extern "C" {

	void Default_Handler(void) {
		erro("");
	}
	
	// Internal References
	void Vectors(void) __attribute__((naked, section("RESET")));
	void Reset_Handler(void) __attribute__((naked, target("arm")));

	// ExcRupt Handler
	void Undef_Handler(void) __attribute__((weak, alias("Default_Handler")));
	void SVC_Handler(void) __attribute__((weak, alias("Default_Handler")));
	void PAbt_Handler(void) __attribute__((weak, alias("Default_Handler")));
	void DAbt_Handler(void) __attribute__((weak, alias("Default_Handler")));
	void Rsvd_Handler(void) __attribute__((weak, alias("Default_Handler")));
	void IRQ_Handler(void) __attribute__((weak, alias("Default_Handler")));
	void FIQ_Handler(void) __attribute__((weak, alias("Default_Handler")));

	void Vectors(void) {
		__asm__ volatile(
			".align 7                                         \n"
			"LDR    PC, =Reset_Handler                        \n"
			"LDR    PC, =Undef_Handler                        \n"
			"LDR    PC, =SVC_Handler                          \n"
			"LDR    PC, =PAbt_Handler                         \n"
			"LDR    PC, =DAbt_Handler                         \n"
			"LDR    PC, =Rsvd_Handler                         \n"
			"LDR    PC, =IRQ_Handler                          \n"
			"LDR    PC, =FIQ_Handler                          \n"
			);
	}


// ABOVE: Thumb Mode
// BELOW: ARM 32b Mode
	void Reset_Handler(void) {
		__asm__ volatile(
			".code 32                                        \n"
			/* Mask interrupts */
			"CPSID   if                                      \n"

			/* Put any cores other than 0 to sleep */
			"MRC     p15, 0, R0, c0, c0, 5                   \n"  /* Read MPIDR */
			"ANDS    R0, R0, #3                              \n"
			"goToSleep:                                      \n"
			"ITT  NE                                         \n"  /* Needed when in Thumb mode for following WFINE instruction */
			"WFINE                                           \n"
			"BNE     goToSleep                               \n"

			/* Reset SCTLR Settings */
			"MRC     p15, 0, R0, c1, c0, 0                   \n"  /* Read CP15 System Control register */
			"BIC     R0, R0, #(0x1 << 12)                    \n"  /* Clear I bit 12 to disable I Cache */
			"BIC     R0, R0, #(0x1 <<  2)                    \n"  /* Clear C bit  2 to disable D Cache */
			"BIC     R0, R0, #0x1                            \n"  /* Clear M bit  0 to disable MMU */
			"BIC     R0, R0, #(0x1 << 11)                    \n"  /* Clear Z bit 11 to disable branch prediction */
			"BIC     R0, R0, #(0x1 << 13)                    \n"  /* Clear V bit 13 to disable hivecs */
			"BIC     R0, R0, #(0x1 << 29)                    \n"  /* Clear AFE bit 29 to enable the full range of access permissions */
			"ORR     R0, R0, #(0x1 << 30)                    \n"  /* Set TE bit to take exceptions in Thumb mode */
			"MCR     p15, 0, R0, c1, c0, 0                   \n"  /* Write value back to CP15 System Control register */
			"ISB                                             \n"

			/* Configure ACTLR */
			"MRC     p15, 0, r0, c1, c0, 1                   \n"  /* Read CP15 Auxiliary Control Register */
			"ORR     r0, r0, #(1 <<  1)                      \n"  /* Enable L2 prefetch hint (UNK/WI since r4p1) */
			"MCR     p15, 0, r0, c1, c0, 1                   \n"  /* Write CP15 Auxiliary Control Register */

			/* Set Vector Base Address Register (VBAR) to point to this application's vector table */
			"LDR    R0, =Vectors                             \n"
			"MCR    p15, 0, R0, c12, c0, 0                   \n"
			"ISB                                             \n"

			/* Setup Stack for each exceptional mode */
			/*~ msr cpsr_c,#(DISABLE_IRQ|DISABLE_FIQ|FIQ_MOD) */
			"CPS    %[fiq_mode]                              \n"
			"LDR    SP, =FIQ_STACK                           \n"
			"CPS    %[irq_mode]                              \n"
			"LDR    SP, =IRQ_STACK                           \n"
			"CPS    %[svc_mode]                              \n"
			"LDR    SP, =SVC_STACK                           \n"
			"CPS    %[abt_mode]                              \n"
			"LDR    SP, =ABT_STACK                           \n"
			"CPS    %[und_mode]                              \n"
			"LDR    SP, =UND_STACK                           \n"
			"CPS    %[sys_mode]                              \n"
			"LDR    SP, =SYS_STACK                           \n"

			/* do clear BSS and others */
			"BL     SystemInit                               \n"

			/* Unmask interrupts */
			"CPSIE  if                                       \n"

			/* Initialize libc */
			"BL __libc_init_array \n"
			/* transfer to successor */
			"BL     main                                     \n"
			::[usr_mode] "M" (_PCUMODE_USR),
			[fiq_mode] "M" (_PCUMODE_FIQ),
			[irq_mode] "M" (_PCUMODE_IRQ),
			[svc_mode] "M" (_PCUMODE_SVC),
			[abt_mode] "M" (_PCUMODE_ABT),
			[und_mode] "M" (_PCUMODE_UND),
			[sys_mode] "M" (_PCUMODE_SYS));
	}

}
