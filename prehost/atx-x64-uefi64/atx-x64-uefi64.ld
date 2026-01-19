ENTRY(_entry)

MEMORY
{
	RAM (rwx) : ORIGIN = 0x100000, LENGTH = 0x100000
}

PHDRS
{
	text PT_LOAD;
	data PT_LOAD;
	bss PT_LOAD;
	rodata PT_LOAD;
}

SECTIONS
{
	. = 0x100000;
	.text : {
		*(.text)
	} > RAM :text
	
	.data : {
		*(.data)
		*(.note.gnu.build-id)
		*(.got)
		*(.init_array)
		*(.eh_frame)
	} > RAM :data
	
	.bss : {
		BSS_ENTO = .;
		*(.bss)
		BSS_ENDO = .;
	} > RAM :bss
	
	.rodata : {
		*(.rodata)
	} > RAM :rodata

	/* C++ only */
	. = ALIGN(8);
	.init_array : {
	__init_array_start = .;
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array))
	__init_array_end = .;
	} > RAM :data
}