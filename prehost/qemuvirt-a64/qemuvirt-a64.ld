ENTRY(_entry)

MEMORY
{
	RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 0x10000000  /* 256MB */
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
	. = 0x40000000;
	FILE_ENTO = .;
	.text : {
		*(.text.rupt) *(.text.boot) *(.text)
	} > RAM :text
	
	.data : {
		*(.data)
		*(.note.gnu.build-id)
		*(.got)
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

	. = ALIGN(8);
	. += 0x1000;
	stack_top = .;

	FILE_ENDO = .;
}