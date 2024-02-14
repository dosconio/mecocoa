SECTIONS
{
  . = 0xB000;
  .text : {*(.text)}
  . = 0xC000;
  .data : {*(.data)}
  .bss : {*(.bss)}
}