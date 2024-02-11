SECTIONS
{
  . = 0x6000;
  .text : {*(.text)}
  . = 0x5000;
  .data : {*(.data)}
  .bss : {*(.bss)}
}