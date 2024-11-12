MEMORY
{
	RAM1 (rwx) : ORIGIN = 0x5000, LENGTH = 0x2C00
	RAM2 (rwx) : ORIGIN = 0x7E00, LENGTH = 0x0200
}

SECTIONS
{
  # escape USYM's 7C00~7BFF
  #. = 0xC000;
  #. = 0x5800;
  . = 0x4000;
  .text : {*(.text)}
  .data : {*(.data)}
  .bss : {*(.bss)}
  .rodata : {*(.rodata)}
}