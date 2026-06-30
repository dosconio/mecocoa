ENTRY(multiboot_entry) /* ENTRY(_start) */ /* -e _start */

MEMORY
{
	RAM0 (rwx) : ORIGIN =     0x7E00, LENGTH = 0x100
	RAM1 (rwx) : ORIGIN =     0x8000, LENGTH = 0x70000
	RAM2 (rwx) : ORIGIN = 0x00101000, LENGTH = 0x10000000
}

SECTIONS
{
	. = 0x8000;
	FILE_ENTO = .;
	/*
	.multiboot : {
		*(.multiboot)
	}
	*/

	.text : {
		*mcca-atx-x86-flap32.grub.o(.text .text.*)
	} > RAM0

	/* MultiCore */
	.text : {
		*mcca-atx-x86-flap32-ladder.o(.text .text.*)
		*(.text .text.*)
	} > RAM1
	.rodata : {
		*(.rodata) 
		*(.rodata.*)
	} > RAM1
	.data : {
		*(.data)
		*(.data.*)
		*(.got)
	} > RAM1


	.boot_stack : {
		. = ALIGN(16);
		*(.boot_stack)
	} > RAM1
	
	/* MACRO-CORE */
	. = ALIGN(8);
	.init.rmod : {
		__init_rmod_ento = .;
		KEEP(*(SORT(.init.rmod.*)))
		KEEP(*(.init.rmod))
		__init_rmod_endo = .;
	} > RAM1
	/* C++ only */
	. = ALIGN(8);
	.init_array : {
	__init_array_start = .;
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array))
	__init_array_end = .;
	} > RAM1
	.fini_array :
	{
		__fini_array_start = .;
		KEEP (*(SORT(.fini_array.*)))
		KEEP (*(.fini_array))
		__fini_array_end = .;
	} > RAM1

	.extdata : {
		KEEP(*(.extdata))
		*(.ext.freetype)
		*(.ext.freetype.code)
		*(.ext.freetype.vcode)
	} > RAM2

	.bss : {
		. = ALIGN(16);
		BSS_ENTO = .;
		*(.bss .bss.*)
		*(COMMON)
		BSS_ENDO = .;
	} > RAM2

	/* Remove information from the compiler libraries */
	/DISCARD/ :
	{
		*(.note.gnu.build-id)
		*(.eh_frame)
		*(.comment)
		/* libc.a ( * ) */
		/* libm.a ( * ) */
		/* libgcc.a ( * ) */
	}
	FILE_ENDO = .;
}
