// ASCII GAS TAB4 LF
// Mecocoa STM32H743 startup for GNU GCC/GAS, not Keil ARMASM.
// Vector order and interrupt forwarding follow stm32h743.startup.S.

	.syntax unified
	.cpu cortex-m7
	.fpu fpv5-d16
	.thumb
	.eabi_attribute 24, 1
	.eabi_attribute 25, 1

	.equ Stack_Size, 0x8000
	.equ Heap_Size, 0x200

	.section .stack, "aw", %nobits
	.balign 8
	.global BSS_ENTO, BSS_ENDO, __initial_sp
BSS_ENTO:
Stack_Mem:
	.space Stack_Size
BSS_ENDO:
__initial_sp:

	.section .heap, "aw", %nobits
	.balign 8
	.global __heap_base, __heap_limit
__heap_base:
	.space Heap_Size
__heap_limit:

	.section .isr_vector, "a", %progbits
	.balign 1024
	.global __Vectors, __Vectors_End, __Vectors_Size
	.type __Vectors, %object
__Vectors:
	.word __initial_sp // Top of Stack
	.word Reset_Handler // Reset Handler
	.word NMI_Handler // NMI Handler
	.word HardFault_Handler // Hard Fault Handler
	.word MemManage_Handler // MPU Fault Handler
	.word BusFault_Handler // Bus Fault Handler
	.word UsageFault_Handler // Usage Fault Handler
	.word 0 // Reserved
	.word 0 // Reserved
	.word 0 // Reserved
	.word 0 // Reserved
	.word SVC_Handler // SVCall Handler
	.word DebugMon_Handler // Debug Monitor Handler
	.word 0 // Reserved
	.word PendSV_Handler // PendSV Handler
	.word SysTick_Entry // SysTick Handler
	.word WWDG_IRQHandler // Window WatchDog interrupt ( wwdg1_it)
	.word PVD_AVD_IRQHandler // PVD/AVD through EXTI Line detection
	.word TAMP_STAMP_IRQHandler // Tamper and TimeStamps through the EXTI line
	.word RTC_WKUP_IRQHandler // RTC Wakeup through the EXTI line
	.word FLASH_IRQHandler // FLASH
	.word RCC_IRQHandler // RCC
	.word EXTI0_IRQHandler // EXTI Line0
	.word EXTI1_IRQHandler // EXTI Line1
	.word EXTI2_IRQHandler // EXTI Line2
	.word EXTI3_IRQHandler // EXTI Line3
	.word EXTI4_IRQHandler // EXTI Line4
	.word DMA1_Stream0_IRQHandler // DMA1 Stream 0
	.word DMA1_Stream1_IRQHandler // DMA1 Stream 1
	.word DMA1_Stream2_IRQHandler // DMA1 Stream 2
	.word DMA1_Stream3_IRQHandler // DMA1 Stream 3
	.word DMA1_Stream4_IRQHandler // DMA1 Stream 4
	.word DMA1_Stream5_IRQHandler // DMA1 Stream 5
	.word DMA1_Stream6_IRQHandler // DMA1 Stream 6
	.word ADC_IRQHandler // ADC1, ADC2
	.word FDCAN1_IT0_IRQHandler // FDCAN1 interrupt line 0
	.word FDCAN2_IT0_IRQHandler // FDCAN2 interrupt line 0
	.word FDCAN1_IT1_IRQHandler // FDCAN1 interrupt line 1
	.word FDCAN2_IT1_IRQHandler // FDCAN2 interrupt line 1
	.word EXTI9_5_IRQHandler // External Line[9:5]s
	.word TIM1_BRK_IRQHandler // TIM1 Break interrupt
	.word TIM1_UP_IRQHandler // TIM1 Update Interrupt
	.word TIM1_TRG_COM_IRQHandler // TIM1 Trigger and Commutation Interrupt
	.word TIM1_CC_IRQHandler // TIM1 Capture Compare
	.word TIM2_IRQHandler // TIM2
	.word TIM3_IRQHandler // TIM3
	.word TIM4_IRQHandler // TIM4
	.word I2C1_EV_IRQHandler // I2C1 Event
	.word I2C1_ER_IRQHandler // I2C1 Error
	.word I2C2_EV_IRQHandler // I2C2 Event
	.word I2C2_ER_IRQHandler // I2C2 Error
	.word SPI1_IRQHandler // SPI1
	.word SPI2_IRQHandler // SPI2
	.word USART1_IRQHandler // USART1
	.word USART2_IRQHandler // USART2
	.word USART3_IRQHandler // USART3
	.word EXTI15_10_IRQHandler // External Line[15:10]
	.word RTC_Alarm_IRQHandler // RTC Alarm (A and B) through EXTI Line
	.word 0 // Reserved
	.word TIM8_BRK_TIM12_IRQHandler // TIM8 Break Interrupt and TIM12 global interrupt
	.word TIM8_UP_TIM13_IRQHandler // TIM8 Update Interrupt and TIM13 global interrupt
	.word TIM8_TRG_COM_TIM14_IRQHandler // TIM8 Trigger and Commutation Interrupt and TIM14 global interrupt
	.word TIM8_CC_IRQHandler // TIM8 Capture Compare Interrupt
	.word DMA1_Stream7_IRQHandler // DMA1 Stream7
	.word FMC_IRQHandler // FMC
	.word SDMMC1_IRQHandler // SDMMC1
	.word TIM5_IRQHandler // TIM5
	.word SPI3_IRQHandler // SPI3
	.word UART4_IRQHandler // UART4
	.word UART5_IRQHandler // UART5
	.word TIM6_DAC_IRQHandler // TIM6 and DAC1&2 underrun errors
	.word TIM7_IRQHandler // TIM7
	.word DMA2_Stream0_IRQHandler // DMA2 Stream 0
	.word DMA2_Stream1_IRQHandler // DMA2 Stream 1
	.word DMA2_Stream2_IRQHandler // DMA2 Stream 2
	.word DMA2_Stream3_IRQHandler // DMA2 Stream 3
	.word DMA2_Stream4_IRQHandler // DMA2 Stream 4
	.word ETH_IRQHandler // Ethernet
	.word ETH_WKUP_IRQHandler // Ethernet Wakeup through EXTI line
	.word FDCAN_CAL_IRQHandler // FDCAN calibration unit interrupt
	.word 0 // Reserved
	.word 0 // Reserved
	.word 0 // Reserved
	.word 0 // Reserved
	.word DMA2_Stream5_IRQHandler // DMA2 Stream 5
	.word DMA2_Stream6_IRQHandler // DMA2 Stream 6
	.word DMA2_Stream7_IRQHandler // DMA2 Stream 7
	.word USART6_IRQHandler // USART6
	.word I2C3_EV_IRQHandler // I2C3 event
	.word I2C3_ER_IRQHandler // I2C3 error
	.word OTG_HS_EP1_OUT_IRQHandler // USB OTG HS End Point 1 Out
	.word OTG_HS_EP1_IN_IRQHandler // USB OTG HS End Point 1 In
	.word OTG_HS_WKUP_IRQHandler // USB OTG HS Wakeup through EXTI
	.word OTG_HS_IRQHandler // USB OTG HS
	.word DCMI_IRQHandler // DCMI
	.word 0 // Reserved
	.word RNG_IRQHandler // Rng
	.word FPU_IRQHandler // FPU
	.word UART7_IRQHandler // UART7
	.word UART8_IRQHandler // UART8
	.word SPI4_IRQHandler // SPI4
	.word SPI5_IRQHandler // SPI5
	.word SPI6_IRQHandler // SPI6
	.word SAI1_IRQHandler // SAI1
	.word LTDC_IRQHandler // LTDC
	.word LTDC_ER_IRQHandler // LTDC error
	.word DMA2D_IRQHandler // DMA2D
	.word SAI2_IRQHandler // SAI2
	.word QUADSPI_IRQHandler // QUADSPI
	.word LPTIM1_IRQHandler // LPTIM1
	.word CEC_IRQHandler // HDMI_CEC
	.word I2C4_EV_IRQHandler // I2C4 Event
	.word I2C4_ER_IRQHandler // I2C4 Error
	.word SPDIF_RX_IRQHandler // SPDIF_RX
	.word OTG_FS_EP1_OUT_IRQHandler // USB OTG FS End Point 1 Out
	.word OTG_FS_EP1_IN_IRQHandler // USB OTG FS End Point 1 In
	.word OTG_FS_WKUP_IRQHandler // USB OTG FS Wakeup through EXTI
	.word OTG_FS_IRQHandler // USB OTG FS
	.word DMAMUX1_OVR_IRQHandler // DMAMUX1 Overrun interrupt
	.word HRTIM1_Master_IRQHandler // HRTIM Master Timer global Interrupts
	.word HRTIM1_TIMA_IRQHandler // HRTIM Timer A global Interrupt
	.word HRTIM1_TIMB_IRQHandler // HRTIM Timer B global Interrupt
	.word HRTIM1_TIMC_IRQHandler // HRTIM Timer C global Interrupt
	.word HRTIM1_TIMD_IRQHandler // HRTIM Timer D global Interrupt
	.word HRTIM1_TIME_IRQHandler // HRTIM Timer E global Interrupt
	.word HRTIM1_FLT_IRQHandler // HRTIM Fault global Interrupt
	.word DFSDM1_FLT0_IRQHandler // DFSDM Filter0 Interrupt
	.word DFSDM1_FLT1_IRQHandler // DFSDM Filter1 Interrupt
	.word DFSDM1_FLT2_IRQHandler // DFSDM Filter2 Interrupt
	.word DFSDM1_FLT3_IRQHandler // DFSDM Filter3 Interrupt
	.word SAI3_IRQHandler // SAI3 global Interrupt
	.word SWPMI1_IRQHandler // Serial Wire Interface 1 global interrupt
	.word TIM15_IRQHandler // TIM15 global Interrupt
	.word TIM16_IRQHandler // TIM16 global Interrupt
	.word TIM17_IRQHandler // TIM17 global Interrupt
	.word MDIOS_WKUP_IRQHandler // MDIOS Wakeup  Interrupt
	.word MDIOS_IRQHandler // MDIOS global Interrupt
	.word JPEG_IRQHandler // JPEG global Interrupt
	.word MDMA_IRQHandler // MDMA global Interrupt
	.word 0 // Reserved
	.word SDMMC2_IRQHandler // SDMMC2 global Interrupt
	.word HSEM1_IRQHandler // HSEM1 global Interrupt
	.word 0 // Reserved
	.word ADC3_IRQHandler // ADC3 global Interrupt
	.word DMAMUX2_OVR_IRQHandler // DMAMUX Overrun interrupt
	.word BDMA_Channel0_IRQHandler // BDMA Channel 0 global Interrupt
	.word BDMA_Channel1_IRQHandler // BDMA Channel 1 global Interrupt
	.word BDMA_Channel2_IRQHandler // BDMA Channel 2 global Interrupt
	.word BDMA_Channel3_IRQHandler // BDMA Channel 3 global Interrupt
	.word BDMA_Channel4_IRQHandler // BDMA Channel 4 global Interrupt
	.word BDMA_Channel5_IRQHandler // BDMA Channel 5 global Interrupt
	.word BDMA_Channel6_IRQHandler // BDMA Channel 6 global Interrupt
	.word BDMA_Channel7_IRQHandler // BDMA Channel 7 global Interrupt
	.word COMP1_IRQHandler // COMP1 global Interrupt
	.word LPTIM2_IRQHandler // LP TIM2 global interrupt
	.word LPTIM3_IRQHandler // LP TIM3 global interrupt
	.word LPTIM4_IRQHandler // LP TIM4 global interrupt
	.word LPTIM5_IRQHandler // LP TIM5 global interrupt
	.word LPUART1_IRQHandler // LP UART1 interrupt
	.word 0 // Reserved
	.word CRS_IRQHandler // Clock Recovery Global Interrupt
	.word 0 // Reserved
	.word SAI4_IRQHandler // SAI4 global interrupt
	.word 0 // Reserved
	.word 0 // Reserved
	.word WAKEUP_PIN_IRQHandler // Interrupt for all 6 wake-up pins
__Vectors_End:
	.size __Vectors, .-__Vectors
	.set __Vectors_Size, __Vectors_End-__Vectors

	.section .text.Reset_Handler, "ax", %progbits
	.balign 4
	.global Reset_Handler
	.type Reset_Handler, %function
	.thumb_func
Reset_Handler:
	bl	SystemInit
	dsb
	isb

// Keil __main performs this initialization on the ARMASM path.
	ldr	r0, =__data_load__
	ldr	r1, =__data_start__
	ldr	r2, =__data_end__
.Lcopy_data:
	cmp	r1, r2
	bhs	.Lzero_begin
	ldr	r3, [r0], #4
	str	r3, [r1], #4
	b	.Lcopy_data
.Lzero_begin:
	ldr	r1, =__bss_start__
	ldr	r2, =__bss_end__
	movs	r3, #0
.Lzero_bss:
	cmp	r1, r2
	bhs	.Lpreinit_begin
	str	r3, [r1], #4
	b	.Lzero_bss

// Run initialization arrays in linker order; no newlib startup is linked.
.Lpreinit_begin:
	ldr	r4, =__preinit_array_start
	ldr	r5, =__preinit_array_end
.Lpreinit_loop:
	cmp	r4, r5
	bhs	.Linit_begin
	ldr	r3, [r4], #4
	blx	r3
	b	.Lpreinit_loop
.Linit_begin:
	ldr	r4, =__init_array_start
	ldr	r5, =__init_array_end
.Linit_loop:
	cmp	r4, r5
	bhs	.Lmain
	ldr	r3, [r4], #4
	blx	r3
	b	.Linit_loop
.Lmain:
	bl	main
.Lmain_returned:
	cpsid	i
	wfi
	b	.Lmain_returned
	.size Reset_Handler, .-Reset_Handler
	.ltorg

	.section .text.Default_Handler, "ax", %progbits
	.balign 4
	.type Default_Handler, %function
	.thumb_func
Default_Handler:
	mov	r0, lr
	mrs	r1, ipsr
	b	interrupt_dispatcher
	.size Default_Handler, .-Default_Handler

// ARMv7-M exception entry supplies EXC_RETURN in LR; forward it unchanged.
	.macro default_irq name
	.weak \name
	.thumb_set \name, Default_Handler
	.endm

	default_irq NMI_Handler
	default_irq MemManage_Handler
	default_irq BusFault_Handler
	default_irq UsageFault_Handler
	default_irq SVC_Handler
	default_irq DebugMon_Handler
	default_irq PendSV_Handler
	default_irq WWDG_IRQHandler
	default_irq PVD_AVD_IRQHandler
	default_irq TAMP_STAMP_IRQHandler
	default_irq RTC_WKUP_IRQHandler
	default_irq FLASH_IRQHandler
	default_irq RCC_IRQHandler
	default_irq EXTI0_IRQHandler
	default_irq EXTI1_IRQHandler
	default_irq EXTI2_IRQHandler
	default_irq EXTI3_IRQHandler
	default_irq EXTI4_IRQHandler
	default_irq DMA1_Stream0_IRQHandler
	default_irq DMA1_Stream1_IRQHandler
	default_irq DMA1_Stream2_IRQHandler
	default_irq DMA1_Stream3_IRQHandler
	default_irq DMA1_Stream4_IRQHandler
	default_irq DMA1_Stream5_IRQHandler
	default_irq DMA1_Stream6_IRQHandler
	default_irq ADC_IRQHandler
	default_irq FDCAN1_IT0_IRQHandler
	default_irq FDCAN2_IT0_IRQHandler
	default_irq FDCAN1_IT1_IRQHandler
	default_irq FDCAN2_IT1_IRQHandler
	default_irq EXTI9_5_IRQHandler
	default_irq TIM1_BRK_IRQHandler
	default_irq TIM1_UP_IRQHandler
	default_irq TIM1_TRG_COM_IRQHandler
	default_irq TIM1_CC_IRQHandler
	default_irq TIM2_IRQHandler
	default_irq TIM3_IRQHandler
	default_irq TIM4_IRQHandler
	default_irq I2C1_EV_IRQHandler
	default_irq I2C1_ER_IRQHandler
	default_irq I2C2_EV_IRQHandler
	default_irq I2C2_ER_IRQHandler
	default_irq SPI1_IRQHandler
	default_irq SPI2_IRQHandler
	default_irq USART1_IRQHandler
	default_irq USART2_IRQHandler
	default_irq USART3_IRQHandler
	default_irq EXTI15_10_IRQHandler
	default_irq RTC_Alarm_IRQHandler
	default_irq TIM8_BRK_TIM12_IRQHandler
	default_irq TIM8_UP_TIM13_IRQHandler
	default_irq TIM8_TRG_COM_TIM14_IRQHandler
	default_irq TIM8_CC_IRQHandler
	default_irq DMA1_Stream7_IRQHandler
	default_irq FMC_IRQHandler
	default_irq TIM5_IRQHandler
	default_irq SPI3_IRQHandler
	default_irq UART4_IRQHandler
	default_irq UART5_IRQHandler
	default_irq TIM6_DAC_IRQHandler
	default_irq TIM7_IRQHandler
	default_irq DMA2_Stream0_IRQHandler
	default_irq DMA2_Stream1_IRQHandler
	default_irq DMA2_Stream2_IRQHandler
	default_irq DMA2_Stream3_IRQHandler
	default_irq DMA2_Stream4_IRQHandler
	default_irq ETH_IRQHandler
	default_irq ETH_WKUP_IRQHandler
	default_irq FDCAN_CAL_IRQHandler
	default_irq DMA2_Stream5_IRQHandler
	default_irq DMA2_Stream6_IRQHandler
	default_irq DMA2_Stream7_IRQHandler
	default_irq USART6_IRQHandler
	default_irq I2C3_EV_IRQHandler
	default_irq I2C3_ER_IRQHandler
	default_irq OTG_HS_EP1_OUT_IRQHandler
	default_irq OTG_HS_EP1_IN_IRQHandler
	default_irq OTG_HS_WKUP_IRQHandler
	default_irq OTG_HS_IRQHandler
	default_irq DCMI_IRQHandler
	default_irq RNG_IRQHandler
	default_irq FPU_IRQHandler
	default_irq UART7_IRQHandler
	default_irq UART8_IRQHandler
	default_irq SPI4_IRQHandler
	default_irq SPI5_IRQHandler
	default_irq SPI6_IRQHandler
	default_irq SAI1_IRQHandler
	default_irq LTDC_IRQHandler
	default_irq LTDC_ER_IRQHandler
	default_irq DMA2D_IRQHandler
	default_irq SAI2_IRQHandler
	default_irq QUADSPI_IRQHandler
	default_irq LPTIM1_IRQHandler
	default_irq CEC_IRQHandler
	default_irq I2C4_EV_IRQHandler
	default_irq I2C4_ER_IRQHandler
	default_irq SPDIF_RX_IRQHandler
	default_irq OTG_FS_EP1_OUT_IRQHandler
	default_irq OTG_FS_EP1_IN_IRQHandler
	default_irq OTG_FS_WKUP_IRQHandler
	default_irq OTG_FS_IRQHandler
	default_irq DMAMUX1_OVR_IRQHandler
	default_irq HRTIM1_Master_IRQHandler
	default_irq HRTIM1_TIMA_IRQHandler
	default_irq HRTIM1_TIMB_IRQHandler
	default_irq HRTIM1_TIMC_IRQHandler
	default_irq HRTIM1_TIMD_IRQHandler
	default_irq HRTIM1_TIME_IRQHandler
	default_irq HRTIM1_FLT_IRQHandler
	default_irq DFSDM1_FLT0_IRQHandler
	default_irq DFSDM1_FLT1_IRQHandler
	default_irq DFSDM1_FLT2_IRQHandler
	default_irq DFSDM1_FLT3_IRQHandler
	default_irq SAI3_IRQHandler
	default_irq SWPMI1_IRQHandler
	default_irq TIM15_IRQHandler
	default_irq TIM16_IRQHandler
	default_irq TIM17_IRQHandler
	default_irq MDIOS_WKUP_IRQHandler
	default_irq MDIOS_IRQHandler
	default_irq JPEG_IRQHandler
	default_irq MDMA_IRQHandler
	default_irq SDMMC2_IRQHandler
	default_irq HSEM1_IRQHandler
	default_irq ADC3_IRQHandler
	default_irq DMAMUX2_OVR_IRQHandler
	default_irq BDMA_Channel0_IRQHandler
	default_irq BDMA_Channel1_IRQHandler
	default_irq BDMA_Channel2_IRQHandler
	default_irq BDMA_Channel3_IRQHandler
	default_irq BDMA_Channel4_IRQHandler
	default_irq BDMA_Channel5_IRQHandler
	default_irq BDMA_Channel6_IRQHandler
	default_irq BDMA_Channel7_IRQHandler
	default_irq COMP1_IRQHandler
	default_irq LPTIM2_IRQHandler
	default_irq LPTIM3_IRQHandler
	default_irq LPTIM4_IRQHandler
	default_irq LPTIM5_IRQHandler
	default_irq LPUART1_IRQHandler
	default_irq CRS_IRQHandler
	default_irq SAI4_IRQHandler
	default_irq WAKEUP_PIN_IRQHandler

	.section .note.GNU-stack, "", %progbits
