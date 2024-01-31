### BSD3-Opensrc Mecocoa @dosconio ###
# Build Requirements: Unisym Kit and Windows Host System

asm=aasm
vhd=E:\vhd.vhd
vmbox=E:\software\vmbox\VBoxManage.exe
bochd=E:\software\Bochs-2.7\bochsdbg.exe

vmname=Kasa
vmnamf=Kasaf

asmattr=-I../unisym/inc/Kasha/n_ -I../unisym/inc/naasm/n_ -I./include/ -I./kernel/


flat: build
	#{todo} mecca.vhd
	@echo "Building Flat Version ..."
	@ffset $(vhd) ../_obj/boot.bin 0
	@ffset $(vhd) ../_obj/kernel.bin 1
	@ffset $(vhd) ../_obj/Kernel32.bin 9
	@ffset $(vhd) ../_obj/Shell32.bin 20
	@ffset $(vhd) ../_obj/helloa.bin 50
	@ffset $(vhd) ../_obj/hellob.bin 60

floppy: buildf
	@echo "Building Floppy Version ..."
	@dd if=../_obj/boot.f.bin of=../_bin/mecca.img bs=512 count=1 conv=notrunc
	#{TEMP} Add Kernel.bin into .img manually
	#{TEMP} Kernel32 and Subapps on Hdisk
	##-sudo mkdir /mnt/floppy/

init:
	# Initialize the output image file
	-rm ../_bin/mecca.img
	@dd if=/dev/zero of=../_bin/mecca.img bs=512 count=2880
	@$(vmbox) closemedium disk ../_bin/mecca.vhd --delete
	#-rm ../_bin/mecca.vhd
	@$(vmbox) createhd --filename ../_bin/mecca.vhd --format VHD --size 4 --variant Fixed

###

run: flat
	$(vmbox) startvm $(vmname)

runf: floppy
	$(vmbox) startvm $(vmnamf)

bochs: flat
	-$(bochd) -f e:/cnrv/bochsrc.bxrc

bochf: floppy
	-$(bochd) -f e:/cnrv/bochsrcf.bxrc

###

build: ../_obj/boot.bin ../_obj/kernel.bin ../_obj/Kernel32.bin ../_obj/Shell32.bin ../_obj/helloa.bin ../_obj/hellob.bin
	@echo "Building Components ..."

buildf: ../_obj/boot.f.bin ../_obj/kernel.f.bin ../_obj/Kernel32.bin ../_obj/Shell32.bin ../_obj/helloa.bin ../_obj/hellob.bin
	@echo "Building Components for Floppy ..."

###

../_obj/boot.bin: ../unisym/demo/osdev/bootstrap/bootfka.a
	$(asm) ../unisym/demo/osdev/bootstrap/bootfka.a -o ../_obj/boot.bin ${asmattr}
../_obj/boot.f.bin: ../unisym/demo/osdev/bootstrap/bootfka.a
	$(asm) ../unisym/demo/osdev/bootstrap/bootfka.a -o ../_obj/boot.f.bin ${asmattr} -D_FLOPPY

# kernel.bin(Normal), KER.APP(Floppy Identifier)
../_obj/kernel.bin: ./kernel/Kernel.asm
	$(asm) ./kernel/Kernel.asm -o ../_obj/kernel.bin ${asmattr}
../_obj/kernel.f.bin: ./kernel/Kernel.asm
	$(asm) ./kernel/Kernel.asm -o ../_obj/kernel.f.bin ${asmattr} -D_FLOPPY

../_obj/Kernel32.bin: ./kernel/Kernel32.asm ./include/offset.a ./kernel/kerrout32.a ./include/routidx.a ./kernel/_debug.a
	$(asm) ./kernel/Kernel32.asm -o ../_obj/Kernel32.bin ${asmattr}

../_obj/Shell32.bin: ./subapp/Shell32.asm
	$(asm) ./subapp/Shell32.asm -o ../_obj/Shell32.bin ${asmattr}

../_obj/helloa.bin: ./subapp/helloa.asm
	$(asm) ./subapp/helloa.asm -o ../_obj/helloa.bin ${asmattr}

../_obj/hellob.bin: ./subapp/hellob.asm
	$(asm) ./subapp/hellob.asm -o ../_obj/hellob.bin ${asmattr}

###

clean:
	-rm ../_obj/boot.bin
	-rm ../_obj/boot.f.bin
	-rm ../_obj/kernel.bin
	-rm ../_obj/kernel.f.bin
	-rm ../_obj/Kernel32.bin
	-rm ../_obj/Shell32.bin
	-rm ../_obj/helloa.bin
	-rm ../_obj/hellob.bin



