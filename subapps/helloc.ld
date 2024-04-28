SECTIONS
{
  . = 0x29000;
  .text : {*(.text)}
  . = 0x28000;
  .data : {*(.data)}
  .bss : {*(.bss)}
}