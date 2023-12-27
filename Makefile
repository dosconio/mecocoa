asm=aasm
vhd=E:\vhd.vhd
vmbox=E:\software\vmbox\VBoxManage.exe
vmname=Kasa

run: ../_obj/boot.bin ../_obj/kernel.bin ../_obj/test.bin
	ffset $(vhd) ../_obj/boot.bin 0
	ffset $(vhd) ../_obj/kernel.bin 1
	ffset $(vhd) ../_obj/test.bin 9
	$(vmbox) startvm $(vmname)
../_obj/boot.bin: BOOT.a
	$(asm) BOOT.a -o ../_obj/boot.bin -I../unisym/inc/Kasha/n_
../_obj/kernel.bin: Kernel.asm
	$(asm) Kernel.asm -o ../_obj/kernel.bin -I../unisym/inc/Kasha/n_
../_obj/test.bin: test.asm
	$(asm) test.asm -o ../_obj/test.bin -I../unisym/inc/Kasha/n_

