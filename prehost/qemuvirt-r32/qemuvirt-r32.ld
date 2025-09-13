SECTIONS {
  .text 0x80000000 : {
    /mnt/hgfs/tmp/obj/mcca-qemuvirt-r32/qemuvirt-r32.startup.o(.text)
  }
  .text : { *(.text*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) }
}
