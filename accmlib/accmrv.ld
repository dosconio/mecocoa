SECTIONS 
{
	. = 0x10000;

	.text : {
		KEEP(*(.text.entry))
		*(.text .text.*)
	}

	.data : {
		. = ALIGN(16);
		__global_pointer$ = . + 0x800;
		*(.sdata .sdata.*)
		*(.data .data.*)
	}

	/* C++ only */
	. = ALIGN(8);
	.init_array : {
	__init_array_start = .;
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array))
	__init_array_end = .;
	}
	.fini_array :
	{
		__fini_array_start = .;
		KEEP (*(SORT(.fini_array.*)))
		KEEP (*(.fini_array))
		__fini_array_end = .;
	}

	.bss : {
		__bss_start = .;
		*(.sbss .sbss.*)
		*(.bss .bss.*)
		*(COMMON)
		_end = .;
	}

}