SECTIONS
{
  . = 0x6000;
  .text : {*(.text)}
  . = 0x8000;
  # escape USYM's 7C00
  .data : {*(.data)}
  .bss : {*(.bss)}
}