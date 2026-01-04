ENTRY(entry_address_tag_start) /* ENTRY(_start) */ /* -e _start */

MEMORY
{
	RAM1 (rwx) : ORIGIN = 0x8000, LENGTH = 0x70000 /* 0x7E00 .. 0x70000 */
}

SECTIONS
{
	. = 0x8000;
	.text : {*(.text)}
	.data : {
		*(.data)
		*(.note.gnu.build-id)
		*(.got)
		*(.init_array)
		*(.eh_frame)
	}
	.bss : {
		BSS_ENTO = .;
		*(.bss)
		BSS_ENDO = .;
	}
	.rodata : {*(.rodata)}

	/* Remove information from the compiler libraries */
	/DISCARD/ :
	{
		libc.a ( * )
		libm.a ( * )
		libgcc.a ( * )
	}
}
