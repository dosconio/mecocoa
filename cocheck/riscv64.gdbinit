set confirm off
set architecture riscv:rv64
target remote 127.0.0.1:15234
symbol-file /mnt/hgfs/_bin/mecocoa/mcca-riscv64
display/12i $pc-8
set riscv use-compressed-breakpoints yes
break *0x1000
break *0x80200000
