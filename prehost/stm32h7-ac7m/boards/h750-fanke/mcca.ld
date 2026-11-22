/* GNU GCC/ld linker script for the STM32H750 Fanke board QSPI image. */
/* The internal loader must configure W25Q256 memory mapping before entering Reset_Handler. */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(Reset_Handler)

MEMORY
{
	FLASH (rx) : ORIGIN = 0x90000000, LENGTH = 32M
	DTCM (rw) : ORIGIN = 0x20000000, LENGTH = 128K
	AXISRAM (rw) : ORIGIN = 0x24000000, LENGTH = 512K
}

PHDRS
{
	flash PT_LOAD FLAGS(5);
	ram PT_LOAD FLAGS(6);
	stack PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
	.isr_vector ORIGIN(FLASH) :
	{
		KEEP(*(.isr_vector))
	} > FLASH :flash

	.text :
	{
		. = ALIGN(4);
		*(.text .text.*)
		KEEP(*(.init))
		KEEP(*(.fini))
		*(.rodata .rodata.*)
		. = ALIGN(4);
	} > FLASH :flash

	.ARM.extab :
	{
		*(.ARM.extab* .gnu.linkonce.armextab.*)
	} > FLASH :flash

	.ARM.exidx :
	{
		__exidx_start = .;
		*(.ARM.exidx* .gnu.linkonce.armexidx.*)
		__exidx_end = .;
	} > FLASH :flash

	.preinit_array :
	{
		. = ALIGN(4);
		__preinit_array_start = .;
		KEEP(*(.preinit_array))
		__preinit_array_end = .;
	} > FLASH :flash

	.init_array :
	{
		. = ALIGN(4);
		__init_array_start = .;
		KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
		KEEP(*(.init_array))
		__init_array_end = .;
	} > FLASH :flash

	.fini_array :
	{
		. = ALIGN(4);
		__fini_array_start = .;
		KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
		KEEP(*(.fini_array))
		__fini_array_end = .;
	} > FLASH :flash

	.init.rmod ALIGN(16) :
	{
		__init_rmod_ento = .;
		KEEP(*(.init.rmod .init.rmod.*))
		__init_rmod_endo = .;
	} > FLASH :flash

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
	} > AXISRAM AT> FLASH :ram
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
	} > AXISRAM :ram

	.noinit (NOLOAD) :
	{
		. = ALIGN(8);
		*(.noinit .noinit.*)
		. = ALIGN(8);
	} > AXISRAM :ram

	.heap (NOLOAD) :
	{
		. = ALIGN(8);
		KEEP(*(.heap))
	} > AXISRAM :ram

	/* BSS_ENTO/BSS_ENDO delimit this stack, as on the Keil startup path. */
	.stack (NOLOAD) :
	{
		. = ALIGN(8);
		KEEP(*(.stack))
		__ram_image_end__ = .;
	} > AXISRAM :ram

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
ASSERT(__flash_image_end__ <= ORIGIN(FLASH) + LENGTH(FLASH), "Firmware exceeds external QSPI flash")
