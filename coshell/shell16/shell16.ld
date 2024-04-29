# LF
SECTIONS
{
  . = 0xA000;
  .text : {*(.text)}
  . = 0x8000;
  .data : {*(.data)}
  .bss : {*(.bss)}
}