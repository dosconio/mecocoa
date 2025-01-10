ENTRY(entry)
SECTIONS
{
  . = 0x300000;
  .text : {*(.text)}
  .data : {*(.data)}
  .bss : {*(.bss)}
}