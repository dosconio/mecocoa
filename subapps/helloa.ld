SECTIONS
{
  . = 0x25000;
  .text : {*(.text)}
  . = 0x24000;
  .data : {*(.data)}
  .bss : {*(.bss)}
}