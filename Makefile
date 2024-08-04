# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64)
# LastCheck: 20240320
# AllAuthor: @dosconio
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3

# not suitable to use $(subst ...)

.PHONY: new uninstall clean init mdrivers floppy buildf newx min\
	init0 build new-r newx-r dbg-r #<- Harddisk Version

uincpath=/mnt/hgfs/unisym/inc
ulibpath=/mnt/hgfs/unisym/lib
ubinpath=/mnt/hgfs/SVGN/_bin

dbgdir = $(ubinpath)/mecocoa
dstdir = E:/PROJ/SVGN/_bin/mecocoa
unidir = /mnt/hgfs/unisym
libcdir = $(unidir)/lib/c
libadir = $(unidir)/lib/asm
objpath = /home/ayano/_obj/mccax86

asmattr = -I$(uincpath)/Kasha/n_ -I$(uincpath)/naasm/n_ -I./include/
asm  = $(ubinpath)/ELF64/aasm $(asmattr) 
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

InstExt = -L$(ubinpath) -lmx86
KernelExt = $(objpath)/rout16.obj  $(objpath)/handler.obj $(objpath)/handauf.obj $(objpath)/console.obj $(objpath)/page.obj \
		    $(objpath)/interrupt.obj $(objpath)/memasm.obj $(Rout32Obj) $(objpath)/kernel-x86-m32.obj
Rout32Obj = $(objpath)/rout32.obj $(objpath)/memcpl.obj 
Shell16Ext = $(objpath)/rout16.obj
Shell32Ext = $(objpath)/console.obj $(objpath)/codebug.obj $(Rout32Obj) $(objpath)/task.obj

### Virtual Machine
# vmbox=E:\software\vmbox\VBoxManage.exe
vmname = Kasa
vmnamf = Kasaf
bochd = E:/software/Bochs-2.7/bochsdbg.exe
qemu = qemu-system-x86_64



floppy: buildf
	@dd if=$(objpath)/boot.fin of=$(dbgdir)/$(outf) bs=512 count=1 conv=notrunc 2>>/dev/null
	-@sudo mkdir -p /mnt/floppy/
	-@sudo mount -o loop $(dbgdir)/$(outf) /mnt/floppy/
	-@sudo cp $(objpath)/KER.APP /mnt/floppy/KER.APP
	-@sudo cp ./LICENSE /mnt/floppy/TEST.TXT
	@sudo cp $(objpath)/SHL16.APP /mnt/floppy/SHL16.APP
	@sudo cp $(objpath)/SHL32.APP /mnt/floppy/SHL32.APP
	@sudo cp $(objpath)/helloa.app /mnt/floppy/HELLOA.APP
	@sudo cp $(objpath)/hellob.app /mnt/floppy/HELLOB.APP
	@sudo cp $(objpath)/helloc.app /mnt/floppy/HELLOC.APP
	@sudo cp ./subapps/hellod/target/cargo-i686/release/hellod /mnt/floppy/HELLOD.APP
	-@sudo umount /mnt/floppy/
	@echo "Finish : Imagining Floppy Version ."

init: # for floppy
	@mkdir -p $(objpath)
	-@rm $(dbgdir)/$(outf)
	@dd if=/dev/zero of=$(dbgdir)/$(outf) bs=512 count=2880 2>>/dev/null
	@echo "Initial: Now the floppy version can be made."

init0: # for general disk
	@mkdir -p $(objpath)
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
	$(qemu) -drive format=raw,file=$(dbgdir)/$(outf),if=floppy -boot order=a

newx: new qemu

lib:
	cd $(unidir) && make mx86

###

build: # Harddisk and CD version
	#
buildf: $(objpath)/boot.fin $(objpath)/KER.APP $(objpath)/SHL16.APP $(objpath)/SHL32.APP
	@echo "Finish : Building Floppy Version."

mdrivers:
	@echo "Build  : Drivers except libraries"
	@$(cc32) ./mecocoa/graphic.c                -o $(objpath)/console.obj
	@$(cc32) ./mecocoa/handler/interrupt.c      -o $(objpath)/interrupt.obj
	@$(asmf) ./mecocoa/memory/paging.asm        -o $(objpath)/page.obj
	@$(asmf) ./mecocoa/handler/handler.asm      -o $(objpath)/handler.obj
	@$(cc32) ./mecocoa/handler/handler.c        -o $(objpath)/handauf.obj
	@$(asmf) ./cocheck/codebug.asm              -o $(objpath)/codebug.obj
	@$(asmf) ./mecocoa/memory/memoman.asm       -o $(objpath)/memasm.obj
	@$(cc32) ./mecocoa/memory/memoman.c         -o $(objpath)/memcpl.obj
	@$(cc32) ./mecocoa/multask/multask.c        -o $(objpath)/task.obj


###

$(objpath)/boot.fin: $(unidir)/demo/osdev/bootstrap/bootfka.a
	@echo "Build  : Boot"
	@$(asm) $< -o $@ -D_FLOPPY

# Kernel
$(objpath)/KER.APP: ./mecocoa/kernel.asm
	@echo "Build  : Routines of Kernel"
	@$(asmf) ./mecocoa/routine/rout16.asm -o $(objpath)/rout16.obj
	@$(asmf) ./mecocoa/routine/rout32.asm -o $(objpath)/rout32.obj
	@echo "Build  : Kernel"
	@$(asm) $< -o $(objpath)/kernel.obj -D_FLOPPY -felf
	@$(cc32) ./mecocoa/kernel-x86-m32.c -o $(objpath)/kernel-x86-m32.obj
	@$(link) -s -T ./mecocoa/kernel.ld -e HerMain -m elf_i386 -o $@ $(objpath)/kernel.obj $(KernelExt) $(InstExt)

$(objpath)/SHL16.APP: ./coshell/shell16/shell16.c
	@echo "Build  : Shell16"
	@$(ccc) $< -o $(objpath)/shell16.obj -m16
	@$(link) -s -T ./coshell/shell16/shell16.ld -e main -m elf_i386 -o $@ \
		$(objpath)/shell16.obj $(Shell16Ext)

$(objpath)/SHL32.APP: ./coshell/shell32/shell32.cpp
	@echo "Build  : Shell32"
	@$(cx32) $(<) -o $(objpath)/shell32.obj
	@$(link) -s -T ./coshell/shell32/shell32.ld -e _main -m elf_i386 -o $@ \
		$(objpath)/shell32.obj $(Shell32Ext) $(InstExt)

#{TOIN} subapps/
subapp: helloa hellob helloc hellod
	@echo "Build  : Subapps Finished"
helloa: ./subapps/helloa/helloa.asm
	@echo "Build  : Subapps/helloa"
	@$(asmf) $< -o $(objpath)/helloa.elf
	@$(link) -s -T ./subapps/helloa/helloa.ld -e _start -m elf_i386 -o $(objpath)/helloa.app $(objpath)/helloa.elf -L$(ubinpath)/mecocoa -lmccausr-x86
hellob: ./subapps/hellob/hellob.c
	@echo "Build  : Subapps/hellob"
	@$(cc32) $< -o $(objpath)/hellob.obj
	@$(link) -s -T ./subapps/hellob/hellob.ld -m elf_i386 -o $(objpath)/hellob.app\
	 $(objpath)/hellob.obj -L$(ubinpath)/mecocoa -lmccausr-x86
helloc: ./subapps/helloc/helloc.cpp
	@echo "Build  : Subapps/helloc"
	@$(cx32) $< -o $(objpath)/helloc.obj
	@$(link) -s -T ./subapps/helloc/helloc.ld -m elf_i386 -o $(objpath)/helloc.app\
	 $(objpath)/helloc.obj -L$(ubinpath)/mecocoa -lmccausr-x86
hellod:
	@echo "Build  : Subapps/hellod"
	@cd subapps/hellod/ && cargo build --release --target ../../configs/Rust/target/cargo-i686.json

new-r: 
	@make -f makefil/riscv64.make clean
	@clear & make -f makefil/riscv64.make new

newx-r: 
	@make -f makefil/riscv64.make clean
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
	-@rm -rf $(objpath)/mccausr/*
	@mkdir -p $(objpath)/mccausr
	@$(asmf) ./userkit/lib/cocoapp.asm -o $(objpath)/mccausr/mccausr.obj
	-rm -rf $(ubinpath)/mecocoa/libmccausr-x86.a
	ar -rcs $(ubinpath)/mecocoa/libmccausr-x86.a $(objpath)/mccausr/*

all: new new-r
	@echo "Finish : All Finished"
clean:
	-@rm -rf $(objpath)/*

uninstall:
	-@sudo rm -rf /mnt/floppy
