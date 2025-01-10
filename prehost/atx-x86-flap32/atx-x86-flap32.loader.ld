ENTRY(_start) /* -e _start */

MEMORY
{
	RAM1 (rwx) : ORIGIN = 0x1E00, LENGTH = 0x5E00 /* .. 0x7C00 */
}

SECTIONS
{
	. = 0x1E00;
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


}
