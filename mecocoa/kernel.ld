SECTIONS
{
  #. = 0xC000;
  . = 0x5800;
  .text : {*(.text)}
  . = 0x5000;
  # escape USYM's 7C00
  .data : {*(.data)}
  .bss : {*(.bss)}
}