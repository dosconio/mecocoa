# ASCII Makefile TAB4 LF
# Attribute: Ubuntu
# LastCheck: 20240210
# AllAuthor: @dosconio
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3

asm  = ~/_bin/aasm #OPT: aasm
ccc  = gcc -c -fno-builtin -nostdinc -nostdlib -fno-stack-protector\
 -fno-exceptions -fno-strict-aliasing -fno-omit-frame-pointer\
 -fno-leading-underscore # -fno-rtti
outf = ../_bin/mecca.img
link = ld #OPT E:\tmp\CPOSIX\bin\ld.gold.exe

asmattr = -I../unisym/inc/Kasha/n_ -I../unisym/inc/naasm/n_ -I./include/

### Virtual Machine
# vhd=E:\vhd.vhd
# vmbox=E:\software\vmbox\VBoxManage.exe
vmname = Kasa
vmnamf = Kasaf
# bochd=E:\software\Bochs-2.7\bochsdbg.exe

# flat: build
# 	#{todo} mecca.vhd
# 	@echo "Building Flat Version ..."
# 	@ffset $(vhd) ../_obj/boot.bin 0
# 	# @ffset $(vhd) ../_obj/kernel.bin 1
# 	@ffset $(vhd) ../_obj/KER.APP 1
# 	@ffset $(vhd) ../_obj/Kernel32.bin 9
# 	@ffset $(vhd) ../_obj/Shell32.bin 20
# 	@ffset $(vhd) ../_obj/helloa.bin 50
# 	@ffset $(vhd) ../_obj/hellob.bin 60
floppy: buildf
	@dd if=../_obj/boot.fin of=${outf} bs=512 count=1 conv=notrunc
	-sudo mkdir /mnt/floppy/
	-@sudo mount -o loop ${outf} /mnt/floppy/
	-@sudo cp ../_obj/KER.APP /mnt/floppy/KER.APP
	-@sudo umount /mnt/floppy/
	@echo "Finish : Imagining Floppy Version ."

init: # for floppy
	-@rm ${outf}
	@dd if=/dev/zero of=${outf} bs=512 count=2880
	@echo "Initial: Now the floppy version can be made."

init0: # for general disk
	@$(vmbox) closemedium disk ../_bin/mecca.vhd --delete
	@-rm ../_bin/mecca.vhd
	@$(vmbox) createhd --filename ../_bin/mecca.vhd --format VHD --size 4 --variant Fixed
	@echo "Initial: Now the pure-flat disks versions can be made."

new: uninstall clean init floppy

### Virtual Machine

run: flat
	$(vmbox) startvm $(vmname)
runf: floppy
	$(vmbox) startvm $(vmnamf)

bochs: flat
	-$(bochd) -f e:/cnrv/bochsrc.bxrc
bochf: floppy
	-$(bochd) -f e:/cnrv/bochsrcf.bxrc

###

build:
	#
buildf: ../_obj/boot.fin ../_obj/KER.APP
	@echo "Finish: Building Floppy Version."

###

../_obj/headelf: ./drivers/filesys/headelf.asm
	$(asm) $< -o $@ ${asmattr}

../_obj/boot.fin: ../unisym/demo/osdev/bootstrap/bootfka.a
	@echo "Build  : Boot"
	@$(asm) $< -o $@ ${asmattr} -D_FLOPPY

../_obj/KER.APP: ../_obj/headelf ../_obj/helloc.obj ../_obj/hellod.obj
	@echo "Build  : Kernel"
	@$(link) -s -T ./cokasha/kernel.ld -e bbbb -m elf_i386 -o $@ ../_obj/helloc.obj ../_obj/hellod.obj #-Ttext 0x5000
	@dd if=../_obj/headelf of=${@} bs=16 conv=notrunc #@ffset $@ ../_obj/headelf 0

# ../_obj/Shell32.bin: ./subapps/Shell32.asm
# 	$(asm) $< -o $@ ${asmattr}
# 
# ../_obj/helloa.bin: ./subapps/helloa.asm
# 	$(asm) $< -o $@ ${asmattr}
# 
# ../_obj/hellob.bin: ./subapps/hellob.asm
# 	$(asm) $< -o $@ ${asmattr}

../_obj/helloc.obj: ./subapps/helloc.asm
	@echo "Build  : Kernel/HelloC"
	@$(asm) $< -o $@ -felf

../_obj/hellod.obj: ./subapps/hellod.c
	@echo "Build  : Kernel/HelloD"
	@${ccc} $< -o $@ -m16

###

clean:
	-@rm ../_obj/boot.fin
	-@rm ../_obj/headelf
	-@rm ../_obj/helloc.obj
	-@rm ../_obj/hellod.obj
	-@rm ../_obj/KER.APP

uninstall:
	-sudo rm -rf /mnt/floppy
