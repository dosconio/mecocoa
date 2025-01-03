ENTRY(_start) /* -e _start */

MEMORY
{
	RAM1 (rwx) : ORIGIN = 0x1E00, LENGTH = 0x5E00 /* .. 0x7C00 */
}

SECTIONS
{
	. = 0x1E00;
	.text : {*(.text)}
	.data : {*(.data)}
	.bss : {
		_BSS_ENTO = .;
		*(.bss)
		_BSS_ENDO = .;
	}
	.rodata : {*(.rodata)}
}
