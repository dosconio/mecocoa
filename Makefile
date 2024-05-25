# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64)
# LastCheck: 20240320
# AllAuthor: @dosconio
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3

# not suitable to use $(subst ...)

.PHONY: new uninstall clean init mdrivers floppy buildf newx min\
	init0 build new-r newx-r dbg-r #<- Harddisk Version
ulibpath?=/mnt/hgfs/unisym/lib
uincpath?=/mnt/hgfs/unisym/inc
ubinpath?=/mnt/hgfs/unisym/bin
dbgdir = /mnt/hgfs/_bin/mecocoa
dstdir = E:/PROJ/SVGN/_bin/mecocoa
unidir = /mnt/hgfs/unisym
libcdir = $(unidir)/lib/c
libadir = $(unidir)/lib/asm

asmattr = -I$(uincpath)/Kasha/n_ -I$(uincpath)/naasm/n_ -I./include/
asm  = /mnt/hgfs/_bin/ELF64/aasm $(asmattr) 
asmf = $(asm) -felf -Iinclude/
dasm = ndisasm #-u -o EntryPoint -e EntryAddress
cdef = -D_MCCAx86 -D_ARC_x86=5 -I$(uincpath) -Iinclude -fno-builtin -fno-stack-protector
ccc  = gcc -c -nostdinc -nostdlib -fno-exceptions -fno-strict-aliasing\
 -fno-omit-frame-pointer -fno-leading-underscore $(cdef) # -fno-rtti
cc32 = gcc -m32 -c -fleading-underscore -fno-pic $(cdef)
cx32 = g++ -m32 -c -fleading-underscore -fno-pic $(cdef)
outf = mcca.img
# vhd=E:\vhd.vhd
link = ld #OPT E:\tmp\CPOSIX\bin\ld.gold.exe

InstExt = -L/mnt/hgfs/_bin -lmx86
KernelExt = ../_obj/rout16.obj  ../_obj/handler.obj ../_obj/handauf.obj ../_obj/console.obj ../_obj/page.obj \
		    ../_obj/interrupt.obj ../_obj/memasm.obj $(Rout32Obj)
Rout32Obj = ../_obj/rout32.obj ../_obj/memcpl.obj 
Shell16Ext = ../_obj/rout16.obj
Shell32Ext = ../_obj/console.obj ../_obj/codebug.obj $(Rout32Obj) ../_obj/task.obj

### Virtual Machine
# vmbox=E:\software\vmbox\VBoxManage.exe
vmname = Kasa
vmnamf = Kasaf
bochd = E:/software/Bochs-2.7/bochsdbg.exe
qemu = qemu-system-x86_64



floppy: buildf
	@dd if=../_obj/boot.fin of=$(dbgdir)/$(outf) bs=512 count=1 conv=notrunc 2>>/dev/null
	-@sudo mkdir -p /mnt/floppy/
	-@sudo mount -o loop $(dbgdir)/$(outf) /mnt/floppy/
	-@sudo cp ../_obj/KER.APP /mnt/floppy/KER.APP
	-@sudo cp ./LICENSE /mnt/floppy/TEST.TXT
	@sudo cp ../_obj/SHL16.APP /mnt/floppy/SHL16.APP
	@sudo cp ../_obj/SHL32.APP /mnt/floppy/SHL32.APP
	@sudo cp ../_obj/helloa.app /mnt/floppy/HELLOA.APP
	@sudo cp ../_obj/hellob.app /mnt/floppy/HELLOB.APP
	@sudo cp ../_obj/helloc.app /mnt/floppy/HELLOC.APP
	@sudo cp /home/ayano/mecocoa/subapps/hellod/target/cargo-i686/release/hellod /mnt/floppy/HELLOD.APP
	-@sudo umount /mnt/floppy/
	@echo "Finish : Imagining Floppy Version ."

init: # for floppy
	-@rm $(dbgdir)/$(outf)
	@dd if=/dev/zero of=$(dbgdir)/$(outf) bs=512 count=2880 2>>/dev/null
	@echo "Initial: Now the floppy version can be made."

init0: # for general disk
	@$(vmbox) closemedium disk ../_bin/mcca.vhd --delete
	@-rm ../_bin/mcca.vhd
	@$(vmbox) createhd --filename ../_bin/mcca.vhd --format VHD --size 4 --variant Fixed
	@echo "Initial: Now the pure-flat disks versions can be made."

new: uninstall clean init mdrivers subapp floppy 
	@perl ./configs/bochsdbg.pl > $(dbgdir)/bochsrc.bxrc
	@echo
	@echo "You can now debug in bochs with the following command:"
	@echo $(bochd) -f $(dstdir)/bochsrc.bxrc
min: # minimum version

### Virtual Machine (not PHONY)

vmbox: floppy
	$(vmbox) startvm $(vmname)
bochs: floppy
	-$(bochd) -f e:/cnrv/bochsrcf.bxrc
qemu-cd:
	$(qemu) -hda $(dbgdir)/$(outf) -boot d -cdrom $(dbgdir)/$(outf} #{to test}
qemu:
	$(qemu) -fda $(dbgdir)/$(outf) -boot order=a

newx: new qemu

lib:
	cd $(unidir) && make mx86

###

build: # Harddisk and CD version
	#
buildf: ../_obj/boot.fin ../_obj/KER.APP ../_obj/SHL16.APP ../_obj/SHL32.APP
	@echo "Finish : Building Floppy Version."

mdrivers:
	@echo "Build  : Drivers except libraries"
	@$(cc32) ./mecocoa/console/console.c        -o ../_obj/console.obj
	@$(cc32) ./mecocoa/interrupt/interrupt.c    -o ../_obj/interrupt.obj
	@$(asmf) ./mecocoa/memory/paging.asm        -o ../_obj/page.obj
	@$(asmf) ./mecocoa/handler/handler.asm      -o ../_obj/handler.obj
	@$(cc32) ./mecocoa/handler/handler.c        -o ../_obj/handauf.obj
	@$(asmf) ./cocheck/codebug.asm              -o ../_obj/codebug.obj
	@$(asmf) ./mecocoa/memory/memoman.asm       -o ../_obj/memasm.obj
	@$(cc32) ./mecocoa/memory/memoman.c         -o ../_obj/memcpl.obj
	@$(cc32) ./mecocoa/multask/multask.c        -o ../_obj/task.obj


###

../_obj/boot.fin: $(unidir)/demo/osdev/bootstrap/bootfka.a
	@echo "Build  : Boot"
	@$(asm) $< -o $@ -D_FLOPPY

# Kernel
../_obj/KER.APP: ./mecocoa/kernel.asm
	@echo "Build  : Routines of Kernel"
	@$(asmf) ./mecocoa/routine/rout16.asm -o ../_obj/rout16.obj
	@$(asmf) ./mecocoa/routine/rout32.asm -o ../_obj/rout32.obj
	@echo "Build  : Kernel"
	@$(asm) $< -o ../_obj/kernel.obj -D_FLOPPY -felf
	@$(cc32) ./mecocoa/kernel-x86-m32.c -o ../_obj/kernel-x86-m32.obj
	@$(link) -s -T ./mecocoa/kernel.ld -e HerMain -m elf_i386 -o $@ ../_obj/kernel.obj $(KernelExt) $(InstExt)

../_obj/SHL16.APP: ./coshell/shell16/shell16.c
	@echo "Build  : Shell16"
	@$(ccc) $< -o ../_obj/shell16.obj -m16
	@$(link) -s -T ./coshell/shell16/shell16.ld -e main -m elf_i386 -o $@ \
		../_obj/shell16.obj $(Shell16Ext)

../_obj/SHL32.APP: ./coshell/shell32/shell32.cpp
	@echo "Build  : Shell32"
	@$(cx32) $(<) -o ../_obj/shell32.obj
	@$(link) -s -T ./coshell/shell32/shell32.ld -e _main -m elf_i386 -o $@ \
		../_obj/shell32.obj $(Shell32Ext) $(InstExt)

#{TOIN} subapps/
subapp: helloa hellob helloc hellod
	@echo "Build  : Subapps Finished"
helloa: ./subapps/helloa/helloa.asm
	@echo "Build  : Subapps/helloa"
	@$(asmf) $< -o ../_obj/helloa.elf
	@$(link) -s -T ./subapps/helloa/helloa.ld -e _start -m elf_i386 -o ../_obj/helloa.app ../_obj/helloa.elf -L/mnt/hgfs/_bin/mecocoa -lmccausr-x86
hellob: ./subapps/hellob/hellob.c
	@echo "Build  : Subapps/hellob"
	@$(cc32) $< -o ../_obj/hellob.obj
	@$(link) -s -T ./subapps/hellob/hellob.ld -m elf_i386 -o ../_obj/hellob.app\
	 ../_obj/hellob.obj -L/mnt/hgfs/_bin/mecocoa -lmccausr-x86
helloc: ./subapps/helloc/helloc.cpp
	@echo "Build  : Subapps/helloc"
	@$(cx32) $< -o ../_obj/helloc.obj
	@$(link) -s -T ./subapps/helloc/helloc.ld -m elf_i386 -o ../_obj/helloc.app\
	 ../_obj/helloc.obj -L/mnt/hgfs/_bin/mecocoa -lmccausr-x86
hellod:
	@echo "Build  : Subapps/hellod"
	@cd subapps/hellod/ && cargo build --release --target ../../configs/Rust/target/cargo-i686.json

new-r: 
	@make -f makefil/riscv64.make clean
	@clear & make -f makefil/riscv64.make new

newx-r: 
	#-make -f makefil/riscv64.make clean
	@clear & make -f makefil/riscv64.make newx LOG=trace
dbg-r:
	@make -f makefil/riscv64.make debug
	@make -f makefil/riscv64.make dbgend
###
config:
	@echo "ULIB $(ulibpath)"
	@echo "UINC $(uincpath)"
	@echo "UBIN $(ubinpath)"
usrlib:
	-@rm -rf ../_obj/mccausr/*
	@mkdir -p ../_obj/mccausr
	@$(asmf) ./userkit/lib/cocoapp.asm -o ../_obj/mccausr/mccausr.obj
	-rm -rf /mnt/hgfs/_bin/mecocoa/libmccausr-x86.a
	ar -rcs /mnt/hgfs/_bin/mecocoa/libmccausr-x86.a ../_obj/mccausr/*

all: new new-r
	@echo "Finish : All Finished"
clean:
	-@rm -rf ../_obj/mcca

uninstall:
	-@sudo rm -rf /mnt/floppy
