
#ENTRY(_start)
BASE_ADDRESS = 0x8002C000;
SECTIONS
{
	/* Load the kernel at this address: "." means the current address */
	. = BASE_ADDRESS;
	start = .;
	.text : {
		stext = .;
		*(.text .text.*)
		. = ALIGN(4K);
		etext = .;
	}
	.rodata : {
		srodata = .;
		*(.rodata .rodata.*)
		. = ALIGN(4K);
		erodata = .;
	}
	.data : {
		sdata = .;
		*(.data .data.*)
		edata = .;
	}
	.stack : {
		*(.bss.stack)
	}
	.bss : {
		sbss = .;
		*(.bss .bss.*)
		ebss = .;
	}
	PROVIDE(end = .);
}
