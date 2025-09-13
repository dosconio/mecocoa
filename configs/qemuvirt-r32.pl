print "SECTIONS {\n";
print "  .text 0x80000000 : {\n";
print "    $ENV{'uobjpath'}/mcca-qemuvirt-r32/qemuvirt-r32.startup.o(.text)\n";
print "  }\n";
print "  .text : { *(.text*) }\n";
print "  .data : { *(.data*) }\n";
print "  .bss : { *(.bss*) }\n";
print "}\n";