asm=aasm
vhd=E:\vhd.vhd
vmbox=E:\software\vmbox\VBoxManage.exe
vmname=Kasa

build: ../_obj/boot.bin ../_obj/kernel.bin ../_obj/Kernel32.bin ../_obj/Shell32.bin ../_obj/helloa.bin ../_obj/hellob.bin
	ffset $(vhd) ../_obj/boot.bin 0
	ffset $(vhd) ../_obj/kernel.bin 1
	ffset $(vhd) ../_obj/Kernel32.bin 9
	ffset $(vhd) ../_obj/Shell32.bin 20
	ffset $(vhd) ../_obj/helloa.bin 50
	ffset $(vhd) ../_obj/hellob.bin 60

run: build
	$(vmbox) startvm $(vmname)

bochs: build
	E:/software/Bochs-2.7/bochsdbg.exe -f e:/cnrv/bochsrc.bxrc

../_obj/boot.bin: BOOT.a
	$(asm) BOOT.a -o ../_obj/boot.bin -I../unisym/inc/Kasha/n_
../_obj/kernel.bin: Kernel.asm
	$(asm) Kernel.asm -o ../_obj/kernel.bin -I../unisym/inc/Kasha/n_
../_obj/Kernel32.bin: Kernel32.asm
	$(asm) Kernel32.asm -o ../_obj/Kernel32.bin -I../unisym/inc/Kasha/n_
../_obj/Shell32.bin: Shell32.asm
	$(asm) Shell32.asm -o ../_obj/Shell32.bin -I../unisym/inc/Kasha/n_
../_obj/helloa.bin: helloa.asm
	$(asm) helloa.asm -o ../_obj/helloa.bin -I../unisym/inc/Kasha/n_
../_obj/hellob.bin: hellob.asm
	$(asm) hellob.asm -o ../_obj/hellob.bin -I../unisym/inc/Kasha/n_

