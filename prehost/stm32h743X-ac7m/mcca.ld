/* GNU GCC/ld linker script for Mecocoa STM32H743, not a Keil scatter file. */
/* Internal SRAM holds startup data; external memory remains owned by mem.cpp. */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(Reset_Handler)

MEMORY
{
	FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 2M
	DTCM (rw) : ORIGIN = 0x20000000, LENGTH = 128K
	AXISRAM (rw) : ORIGIN = 0x24000000, LENGTH = 512K
}

SECTIONS
{
	.isr_vector ORIGIN(FLASH) :
	{
		KEEP(*(.isr_vector))
	} > FLASH

	.text :
	{
		. = ALIGN(4);
		*(.text .text.*)
		KEEP(*(.init))
		KEEP(*(.fini))
		*(.rodata .rodata.*)
		. = ALIGN(4);
	} > FLASH

	.ARM.extab :
	{
		*(.ARM.extab* .gnu.linkonce.armextab.*)
	} > FLASH

	.ARM.exidx :
	{
		__exidx_start = .;
		*(.ARM.exidx* .gnu.linkonce.armexidx.*)
		__exidx_end = .;
	} > FLASH

	.preinit_array :
	{
		. = ALIGN(4);
		__preinit_array_start = .;
		KEEP(*(.preinit_array))
		__preinit_array_end = .;
	} > FLASH

	.init_array :
	{
		. = ALIGN(4);
		__init_array_start = .;
		KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
		KEEP(*(.init_array))
		__init_array_end = .;
	} > FLASH

	.fini_array :
	{
		. = ALIGN(4);
		__fini_array_start = .;
		KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
		KEEP(*(.fini_array))
		__fini_array_end = .;
	} > FLASH

	.init.rmod ALIGN(16) :
	{
		__init_rmod_ento = .;
		KEEP(*(.init.rmod .init.rmod.*))
		__init_rmod_endo = .;
	} > FLASH

	/* Match the current Keil map: ordinary data and both boot stacks use AXI SRAM. */
	.data :
	{
		. = ALIGN(8);
		__data_start__ = .;
		*(.data .data.*)
		*(.sdata .sdata.*)
		*(.got .got.*)
		. = ALIGN(8);
		__data_end__ = .;
	} > AXISRAM AT> FLASH
	__data_load__ = LOADADDR(.data);
	__flash_image_end__ = LOADADDR(.data) + SIZEOF(.data);

	.bss (NOLOAD) :
	{
		. = ALIGN(8);
		__bss_start__ = .;
		*(.bss .bss.*)
		*(.sbss .sbss.*)
		*(COMMON)
		. = ALIGN(8);
		__bss_end__ = .;
	} > AXISRAM

	.noinit (NOLOAD) :
	{
		. = ALIGN(8);
		*(.noinit .noinit.*)
		. = ALIGN(8);
	} > AXISRAM

	.heap (NOLOAD) :
	{
		. = ALIGN(8);
		KEEP(*(.heap))
	} > AXISRAM

	/* BSS_ENTO/BSS_ENDO delimit this stack, as on the Keil startup path. */
	.stack (NOLOAD) :
	{
		. = ALIGN(8);
		KEEP(*(.stack))
		__ram_image_end__ = .;
	} > AXISRAM

	.ARM.attributes 0 :
	{
		*(.ARM.attributes)
	}

	/DISCARD/ :
	{
		*(.note.GNU-stack)
	}
}

ASSERT(ADDR(.isr_vector) == ORIGIN(FLASH), "Vector table must start at the flash base")
ASSERT(SIZEOF(.isr_vector) <= 1024, "Vector table exceeds its 1024-byte alignment")
ASSERT((__initial_sp & 7) == 0, "Initial MSP must be 8-byte aligned")
ASSERT((__data_load__ & 3) == 0 && (__data_start__ & 3) == 0 && (__data_end__ & 3) == 0, "Data copy requires word alignment")
ASSERT((__bss_start__ & 3) == 0 && (__bss_end__ & 3) == 0, "BSS clear requires word alignment")
ASSERT((__init_rmod_endo - __init_rmod_ento) % 16 == 0, "ARM32 RMOD_LIST entries must occupy 16 bytes")
ASSERT(__ram_image_end__ <= ORIGIN(AXISRAM) + LENGTH(AXISRAM), "Startup data exceeds AXI SRAM")
ASSERT(__flash_image_end__ <= ORIGIN(FLASH) + LENGTH(FLASH), "Firmware exceeds internal flash")
