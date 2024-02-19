# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64)
# LastCheck: 20240210
# AllAuthor: @dosconio
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3

# not suitable to use $(subst ...)

.PHONY: new clean uninstall init init0 mdrivers floppy flat buildf build\
	newx

asmattr = -I${unidir}/inc/Kasha/n_ -I${unidir}/inc/naasm/n_ -I./include/
asm  = /mnt/hgfs/_bin/ELF64/aasm ${asmattr} #OPT: aasm
asmf = ${asm} -felf 
dasm = ndisasm #-u -o EntryPoint -e EntryAddress
ccc  = gcc -c -fno-builtin -nostdinc -nostdlib -fno-stack-protector\
 -fno-exceptions -fno-strict-aliasing -fno-omit-frame-pointer\
 -fno-leading-underscore -I/mnt/hgfs/unisym/inc/c -D_Linux# -fno-rtti
cc32 = gcc -m32 -c -fno-builtin -fleading-underscore -fno-pic\
 -fno-stack-protector -I/mnt/hgfs/unisym/inc/c -D_Linux
cx32 = g++ -m32 -c -fno-builtin -fleading-underscore -fno-pic\
 -fno-stack-protector -I/mnt/hgfs/unisym/inc/cpp -D_Linux
outf = mcca.img
# vhd=E:\vhd.vhd
dbgdir = /mnt/hgfs/_bin/mecocoa
dstdir = E:/PROJ/SVGN/_bin/mecocoa
unidir = /mnt/hgfs/unisym
link = ld #OPT E:\tmp\CPOSIX\bin\ld.gold.exe

KernelExt = ../_obj/FAT12_R16.obj ../_obj/ELF_R16.obj ../_obj/8259Ax86.obj \
		../_obj/floppy.obj ../_obj/iop.obj ../_obj/conio32.obj ../_obj/page.obj #../_obj/manage.obj
Shell32Ext = ../_obj/manage.obj ../_obj/conio32.obj ../_obj/iop.obj

# conio32 DEPEND-ON iop.obj

### Virtual Machine
# vmbox=E:\software\vmbox\VBoxManage.exe
vmname = Kasa
vmnamf = Kasaf
bochd = E:/software/Bochs-2.7/bochsdbg.exe
qemu = qemu-system-x86_64

floppy: buildf
	@dd if=../_obj/boot.fin of=${dbgdir}/${outf} bs=512 count=1 conv=notrunc 2>>/dev/null
	-@sudo mkdir /mnt/floppy/
	-@sudo mount -o loop ${dbgdir}/${outf} /mnt/floppy/
	-@sudo cp ../_obj/KER.APP /mnt/floppy/KER.APP
	-@sudo cp ./LICENSE /mnt/floppy/TEST.TXT
	-@sudo cp ../_obj/SHL16.APP /mnt/floppy/SHL16.APP
	-@sudo cp ../_obj/SHL32.APP /mnt/floppy/SHL32.APP
	-@sudo umount /mnt/floppy/
	@echo "Finish : Imagining Floppy Version ."

init: # for floppy
	-@rm ${dbgdir}/${outf}
	@dd if=/dev/zero of=${dbgdir}/${outf} bs=512 count=2880 2>>/dev/null
	@echo "Initial: Now the floppy version can be made."

init0: # for general disk
	@$(vmbox) closemedium disk ../_bin/mcca.vhd --delete
	@-rm ../_bin/mcca.vhd
	@$(vmbox) createhd --filename ../_bin/mcca.vhd --format VHD --size 4 --variant Fixed
	@echo "Initial: Now the pure-flat disks versions can be made."

new: uninstall clean init mdrivers floppy
	@perl ./configs/bochsdbg.pl > ${dbgdir}/bochsrc.bxrc
	@echo
	@echo "You can now debug in bochs with the following command:"
	@echo ${bochd} -f ${dstdir}/bochsrc.bxrc


### Virtual Machine (not PHONY)

run: flat
	$(vmbox) startvm $(vmname)
runf: floppy
	$(vmbox) startvm $(vmnamf)

bochs: flat
	-$(bochd) -f e:/cnrv/bochsrc.bxrc
bochf: floppy
	-$(bochd) -f e:/cnrv/bochsrcf.bxrc

qemud:
	$(qemu) -hda ${dbgdir}/${outf} -boot d -cdrom ${dbgdir}/${outf} #{to test}
qemu:
	${qemu} -fda ${dbgdir}/${outf} -boot order=a

newx: new qemu

###

build:
	#
buildf: ../_obj/boot.fin ../_obj/KER.APP ../_obj/SHL16.APP ../_obj/SHL32.APP
	@echo "Finish : Building Floppy Version."

mdrivers:
	@echo "Build  : Drivers except libraries"
	@$(asmf) ${unidir}/lib/asm/x86/filesys/FAT12.asm        -o ../_obj/FAT12_R16.obj
	@$(asmf) ${unidir}/lib/asm/x86/filefmt/ELF.asm          -o ../_obj/ELF_R16.obj  
	@$(asmf) ${unidir}/lib/asm/x86/interrupt/x86_i8259A.asm -o ../_obj/8259Ax86.obj 
	@$(asmf) ${unidir}/lib/asm/x86/disk/floppy.asm          -o ../_obj/floppy.obj   
	@$(asmf) ${unidir}/lib/asm/x86/inst/ioport.asm          -o ../_obj/iop.obj      
	@$(asmf) ${unidir}/lib/asm/x86/inst/manage.x86.asm      -o ../_obj/manage.obj
	@$(cc32) ./drivers/conio/conio32.c   -o ../_obj/conio32.obj
	@$(asmf) ./drivers/memory/paging.asm -o ../_obj/page.obj

###

../_obj/headelf: ./drivers/filesys/headelf.asm
	@$(asm) $< -o $@

../_obj/boot.fin: ${unidir}/demo/osdev/bootstrap/bootfka.a
	@echo "Build  : Boot"
	@$(asm) $< -o $@ -D_FLOPPY

# Kernel
../_obj/kernel.obj: ./cokasha/kernel.asm
	@$(asm) $< -o $@ -D_FLOPPY -felf
../_obj/KER.APP: ../_obj/headelf ../_obj/kernel.obj #../_obj/helloc.obj ../_obj/hellod.obj
	@echo "Build  : Kernel"
	@$(link) -s -T ./cokasha/kernel.ld -e HerMain -m elf_i386 -o $@ ../_obj/kernel.obj ${KernelExt}
	@dd if=../_obj/headelf of=${@} bs=16 conv=notrunc 2>>/dev/null #@ffset $@ ../_obj/headelf 0 

../_obj/SHL16.APP: ./coshell/shell16.c ./drivers/library/libdbg.asm
	@echo "Build  : Shell16"
	@${ccc} $< -o ../_obj/shell16.obj -m16
	@${asm} ./drivers/library/libdbg.asm -o ../_obj/libdbg.obj -felf
	@$(link) -s -T ./coshell/shell16.ld -e main -m elf_i386 -o $@ \
		../_obj/shell16.obj ../_obj/libdbg.obj

../_obj/SHL32.APP: ./coshell/shell32.cpp
	@echo "Build  : Shell32"
	@${cx32} ${<} -o ../_obj/shell32.obj
	@$(link) -s -T ./coshell/shell32.ld -e _main -m elf_i386 -o $@ \
		../_obj/shell32.obj ${Shell32Ext}

# ../_obj/helloa.bin: ./subapps/helloa.asm
# 	$(asm) $< -o $@

# ../_obj/hellob.bin: ./subapps/hellob.asm
# 	$(asm) $< -o $@

###

clean:
	-@rm ../_obj/boot.fin
	-@rm ../_obj/headelf
	-@rm ../_obj/kernel.obj
	-@rm ${KernelExt}
	-@rm ../_obj/KER.APP
	-@rm ../_obj/shell16.obj
	-@rm ../_obj/libdbg.obj
	-@rm ../_obj/SHL16.APP
	-@rm ../_obj/shell32.obj
	-@rm ../_obj/manage.obj
	-@rm ../_obj/SHL32.APP

uninstall:
	-@sudo rm -rf /mnt/floppy
