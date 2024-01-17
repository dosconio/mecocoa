asm=aasm
vhd=E:\vhd.vhd
vmbox=E:\software\vmbox\VBoxManage.exe
vmname=Kasa

run: ../_obj/boot.bin ../_obj/kernel.bin ../_obj/Kernel32.bin ../_obj/Shell32.bin ../_obj/hello.bin
	ffset $(vhd) ../_obj/boot.bin 0
	ffset $(vhd) ../_obj/kernel.bin 1
	ffset $(vhd) ../_obj/Kernel32.bin 9
	ffset $(vhd) ../_obj/Shell32.bin 20
	ffset $(vhd) ../_obj/hello.bin 50
	$(vmbox) startvm $(vmname)
../_obj/boot.bin: BOOT.a
	$(asm) BOOT.a -o ../_obj/boot.bin -I../unisym/inc/Kasha/n_
../_obj/kernel.bin: Kernel.asm
	$(asm) Kernel.asm -o ../_obj/kernel.bin -I../unisym/inc/Kasha/n_
../_obj/Kernel32.bin: Kernel32.asm
	$(asm) Kernel32.asm -o ../_obj/Kernel32.bin -I../unisym/inc/Kasha/n_
../_obj/Shell32.bin: Shell32.asm
	$(asm) Shell32.asm -o ../_obj/Shell32.bin -I../unisym/inc/Kasha/n_
../_obj/hello.bin: hello.asm
	$(asm) hello.asm -o ../_obj/hello.bin -I../unisym/inc/Kasha/n_

