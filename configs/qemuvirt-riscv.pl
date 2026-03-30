my $arch = $ARGV[0] || 'r32';

print 'OUTPUT_ARCH( "riscv" )
';

print "#include \"../../include/qemuvirt-riscv.hpp\" /* The file is gen by qemuvirt-riscv.pl */ \n";

print '
ENTRY( _start )

MEMORY {
	ram   (wxa!ri) : ORIGIN = 0x80000000, LENGTH = LENGTH_RAM
}

';

# Sections
print "SECTIONS {\n";

print "	.text : {\n";
print "		PROVIDE(_text_start = .); /* as if exists `void* _text_start;` */ \n";
print '
		*(.text.boot .text .text.*)
		PROVIDE(_text_end = .);
	} >ram

	.rodata : {
		PROVIDE(_rodata_start = .);
		*(.rodata .rodata.*)
		PROVIDE(_rodata_end = .);
	} >ram

	.data : {
		. = ALIGN(4096);
		PROVIDE(_data_start = .);
		*(.sdata .sdata.*)
		*(.data .data.*)
		PROVIDE(_data_end = .);
	} >ram

	/* C++ only */
	. = ALIGN(8);
	.init_array : {
		__init_array_start = .;
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array))
		__init_array_end = .;
	} >ram

	.bss :{
		PROVIDE(_bss_start = .);
		*(.sbss .sbss.*)
		*(.bss .bss.*)
		*(COMMON)
		PROVIDE(_bss_end = .);
	} >ram

	PROVIDE(_memory_start = ORIGIN(ram));
	PROVIDE(_memory_end = ORIGIN(ram) + LENGTH(ram));

	PROVIDE(_heap_ento = _bss_end);
	PROVIDE(_heap_endo = _memory_end);

';

print "}\n";
