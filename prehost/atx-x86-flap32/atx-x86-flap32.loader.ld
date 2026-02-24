ENTRY(multiboot_entry) /* -e _start */

MEMORY
{
	RAM1 (rwx) : ORIGIN = 0x1000, LENGTH = 0x6C00 /* 0x1000 .. 0x7C00, 0x7E00 .. 0xFFFF */
}

SECTIONS
{
	. = 0x1000;
	.text : { *(.text .text.*) } > RAM1
	.data : {
		*(.data)
		*(.note.gnu.build-id)
		*(.got)
		*(.eh_frame)
	}> RAM1
	.bss : {
		BSS_ENTO = .;
		*(.bss)
		BSS_ENDO = .;
	}> RAM1
	.rodata : {*(.rodata)}> RAM1

	/* C++ only */
	. = ALIGN(8);
	.init_array : {
	__init_array_start = .;
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array))
	__init_array_end = .;
	}> RAM1

}
