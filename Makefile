asm=aasm
vhd=E:\vhd.vhd
vmbox=E:\software\vmbox\VBoxManage.exe
vmname=Kasa
bochd=E:\software\Bochs-2.7\bochsdbg.exe
asmattr=-I../unisym/inc/Kasha/n_ -I./include/ -I./kernel/

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
	$(bochd) -f e:/cnrv/bochsrc.bxrc

../_obj/boot.bin: ./boot/BootMecocoa.a
	$(asm) ./boot/BootMecocoa.a -o ../_obj/boot.bin ${asmattr}
../_obj/kernel.bin: ./kernel/Kernel.asm
	$(asm) ./kernel/Kernel.asm -o ../_obj/kernel.bin ${asmattr}
../_obj/Kernel32.bin: ./kernel/Kernel32.asm ./include/offset.a ./kernel/kerrout32.a ./include/routidx.a ./kernel/_debug.a
	$(asm) ./kernel/Kernel32.asm -o ../_obj/Kernel32.bin ${asmattr}
../_obj/Shell32.bin: ./subapp/Shell32.asm
	$(asm) ./subapp/Shell32.asm -o ../_obj/Shell32.bin ${asmattr}
../_obj/helloa.bin: ./subapp/helloa.asm
	$(asm) ./subapp/helloa.asm -o ../_obj/helloa.bin ${asmattr}
../_obj/hellob.bin: ./subapp/hellob.asm
	$(asm) ./subapp/hellob.asm -o ../_obj/hellob.bin ${asmattr}

