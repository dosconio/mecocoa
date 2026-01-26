ENTRY(_entry)

MEMORY
{
	RAM1 (rwx) : ORIGIN = 0x10000, LENGTH = 0x70000 /* 0x10000 .. 0x80000 */
}

SECTIONS
{
	. = 0x10000;
	FILE_ENTO = .;
	/*
	.multiboot : {
		*(.multiboot)
	}
	*/
	.text : {*(.text)}
	.data : {
		*(.data)
		*(.note.gnu.build-id)
		*(.got)
		*(.eh_frame)
	}
	.bss : {
		BSS_ENTO = .;
		*(.bss)
		BSS_ENDO = .;
	}
	.rodata : {*(.rodata)}
	/* C++ only */
	. = ALIGN(8);
	.init_array : {
	__init_array_start = .;
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array))
	__init_array_end = .;
	}
	FILE_ENDO = .;
}
