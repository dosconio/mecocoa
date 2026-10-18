ENTRY(multiboot_entry) /* ENTRY(_start) */ /* -e _start */

MEMORY
{
	RAM0 (rwx) : ORIGIN =     0x7E00, LENGTH = 0x100
	RAM1 (rwx) : ORIGIN =     0x8000, LENGTH = 0x70000
	RAM2 (rwx) : ORIGIN = 0x00101000, LENGTH = 0x10000000
}

SECTIONS
{
	. = 0x8000;
	FILE_ENTO = .;
	/*
	.multiboot : {
		*(.multiboot)
	}
	*/

	.text : {
		*mcca-atx-x86-flap32.grub.o(.text .text.*)
	} > RAM0

	/* Network driver */
	.net : {
		*/mcca-atx-x86-flap32/devsman-net.o(.text .text.* .rodata .rodata.* .data .data.* .got)
	} > RAM2
	/* Graphics service */
	.graf : {
		*/mcca-atx-x86-flap32/graphic.o(.text .text.* .rodata .rodata.* .data .data.* .got)
	} > RAM2
	/* FreeType */
	.ft : {
		*/mcca-atx-x86-flap32/freetype.o(.text .text.* .rodata .rodata.* .data .data.* .got)
		depends/freetype/x86/libfreetype.a:(.data .data.* .data.rel.ro .data.rel.ro.* .got)
	} > RAM2
	/* Bitmap fonts */
	.font : {
		*/libm32d.a:_cc_font-8x5.o(.data .data.* .rodata .rodata.*)
		*/libm32d.a:_cc_font-16x8.o(.data .data.* .rodata .rodata.*)
	} > RAM2
	/* Ring1 drivers */
	.drv : {
		*/mcca-atx-x86-flap32/e1000.o(.text .text.* .rodata .rodata.* .data .data.* .got)
		*/mcca-atx-x86-flap32/au-sb16.o(.text .text.* .rodata .rodata.* .data .data.* .got)
		*/mcca-atx-x86-flap32/au-buzzer.o(.text .text.* .rodata .rodata.* .data .data.* .got)
		*/mcca-atx-x86-flap32/devsman-au.o(.text .text.* .rodata .rodata.* .data .data.* .got)
		*/mcca-atx-x86-flap32/nvmedisk.o(.text .text.* .rodata .rodata.* .data .data.* .got)
		*/mcca-atx-x86-flap32/scsidisk.o(.text .text.* .rodata .rodata.* .data .data.* .got)
		*/mcca-atx-x86-flap32/video-vmware.o(.text .text.* .rodata .rodata.* .data .data.* .got)
		*/mcca-atx-x86-flap32/syscall-pow.o(.text .text.* .rodata .rodata.* .data .data.* .got)
	} > RAM2
	/* Paging */
	.page : {
		*/libm32d.a:_cx_Paging.o(.text .text.* .rodata .rodata.* .data .data.* .got)
		*/mcca-atx-x86-flap32/memoman-page.o(.text .text.* .rodata .rodata.* .data .data.* .got)
	} > RAM2

	/* MultiCore */
	.text : {
		*mcca-atx-x86-flap32-ladder.o(.text .text.*)
		*/mcca-atx-x86-flap32/schedul.o(.text .text.*)
	} > RAM1
	.rodata : {
		*/mcca-atx-x86-flap32/schedul.o(.rodata .rodata.*)
	} > RAM1
	.data : {
		*/mcca-atx-x86-flap32/schedul.o(.data .data.*)
		*/mcca-atx-x86-flap32/schedul.o(.got)
	} > RAM1

	/* Everything else lives above 1 MiB, where 32-bit addressing is unrestricted.
	   RAM1 only keeps the AP trampoline (it starts in real mode) and schedul.o,
	   which holds AP_Main and the core-id tables the trampoline reads. */
	.text : {
		*(.text .text.*)
	} > RAM2
	.rodata : {
		*(.rodata)
		*(.rodata.*)
	} > RAM2
	.data : {
		*(.data)
		*(.data.*)
		*(.got)
	} > RAM2

	
	/* MACRO-CORE */
	. = ALIGN(8);
	.init.rmod : {
		__init_rmod_ento = .;
		*/mcca-atx-x86-flap32/e1000.o(.init.rmod)
		*/mcca-atx-x86-flap32/video-vmware.o(.init.rmod)
		KEEP(*(SORT(.init.rmod.*)))
		KEEP(*(.init.rmod))
		__init_rmod_endo = .;
	} > RAM1
	/* C++ only */
	. = ALIGN(8);
	.init_array : {
	__init_array_start = .;
		*/mcca-atx-x86-flap32/e1000.o(.init_array .init_array.*)
		*/mcca-atx-x86-flap32/video-vmware.o(.init_array .init_array.*)
		KEEP(*(SORT(.init_array.*)))
		KEEP(*(.init_array))
	__init_array_end = .;
	} > RAM1
	.fini_array :
	{
		__fini_array_start = .;
		*/mcca-atx-x86-flap32/e1000.o(.fini_array .fini_array.*)
		*/mcca-atx-x86-flap32/video-vmware.o(.fini_array .fini_array.*)
		KEEP (*(SORT(.fini_array.*)))
		KEEP (*(.fini_array))
		__fini_array_end = .;
	} > RAM2

	.boot_stack : {
		. = ALIGN(16);
		*(.boot_stack)
	} > RAM2
	.extdata : {
		KEEP(*(.extdata))
		*/mcca-atx-x86-flap32/freetype.o(.ext.freetype)
		*/mcca-atx-x86-flap32/freetype.o(.ext.freetype.code)
		*/mcca-atx-x86-flap32/freetype.o(.ext.freetype.vcode)
		*(.ext.freetype)
		*(.ext.freetype.code)
		*(.ext.freetype.vcode)
	} > RAM2
	.bss : {
		. = ALIGN(16);
		BSS_ENTO = .;
		*(.bss .bss.*)
		*(COMMON)
		BSS_ENDO = .;
	} > RAM2

	/* Remove information from the compiler libraries */
	/DISCARD/ :
	{
		*(.note.gnu.build-id)
		*(.eh_frame)
		*(.comment)
		/* libc.a ( * ) */
		/* libm.a ( * ) */
		/* libgcc.a ( * ) */
	}
	FILE_ENDO = .;
}
