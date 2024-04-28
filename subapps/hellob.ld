SECTIONS
{
  . = 0x27000;
  .text : {*(.text)}
  . = 0x26000;
  .data : {*(.data)}
  .bss : {*(.bss)}
}