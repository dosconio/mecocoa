OUTPUT_ARCH( "aarch64" )
ENTRY( _start )
MEMORY {
 ram (wxa!ri) : ORIGIN = 0x80000, LENGTH = 896M  /* raspi3 1GB RAM, keep some for GPU */
}
SECTIONS {
 .text : {
  PROVIDE(_text_start = .);
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
  . += 32 * 1024;
  PROVIDE(_bss_end = .);
 } >ram
 PROVIDE(_memory_start = ORIGIN(ram));
 PROVIDE(_memory_end = ORIGIN(ram) + LENGTH(ram));
 PROVIDE(_heap_ento = _bss_end);
 PROVIDE(_heap_endo = _memory_end);
}
_bss_size = (_bss_end - _bss_start)>>3;
