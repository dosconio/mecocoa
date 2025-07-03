# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64) Shell(Bash)
# AllAuthor: @dosconio
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3


iden=mccax86.img
boot=$(ubinpath)/boot-x86.bin
dstdir=D:/bin/I686/mecocoa
outs=$(ubinpath)/I686/mecocoa/$(iden)
mnts=/mnt/floppy
arch=atx-x86-flap32
flag=-D_MCCA=0x8632 -D_ARC_x86=5

qemu=qemu-system-x86_64
bochd=E:/software/Bochs-2.7/bochsdbg.exe

CXF=-fno-rtti -fno-exceptions -fno-unwind-tables -static -nostdlib -fno-pic #-nodefaultlibs #
CXW=-Wno-builtin-declaration-mismatch -Wno-volatile
CX=g++ -I$(uincpath) -c $(flag) -m32 $(CXF) $(CXW) -std=c++2a 

ker_mod=$(uobjpath)/mcca-$(arch)/*

cppfile=$(wildcard mecocoa/*.cpp)
cppobjs=$(patsubst %cpp, %o, $(cppfile))
sudokey=k
elf_loader=mcca-$(arch).loader.elf
elf_kernel=mcca-$(arch).elf

build: clean $(cppobjs)
	@echo "MK mecocoa $(arch) real16 support"
	aasm prehost/$(arch)/atx-x86-real16.asm -felf -o $(uobjpath)/mcca-$(arch)/mcca-$(arch)-elf16.o
	@echo "MK mecocoa $(arch) loader"
	g++ -I$(uincpath) $(flag) -m32 prehost/$(arch)/$(arch).loader.cpp prehost/$(arch)/$(arch).auf.cpp -o $(ubinpath)/$(elf_loader) -L$(ubinpath) -lm32d $(CXF) \
		-T prehost/$(arch)/$(arch).loader.ld  \
		-nostartfiles -Os
	strip --strip-all $(ubinpath)/$(elf_loader)
	@echo "MK mecocoa $(arch)"
	@$(CX) -c prehost/$(arch)/$(arch).cpp -o $(ubinpath)/mcca-$(arch)-main.elf
	g++ -I$(uincpath) $(flag) -m32 $(ker_mod) prehost/$(arch)/$(arch).cpp prehost/$(arch)/$(arch).auf.cpp -o $(ubinpath)/$(elf_kernel) -L$(ubinpath) -lm32d $(CXF) \
		-T prehost/$(arch)/$(arch).ld  \
		-nostartfiles -O0 \
		-Wl,-Map=$(ubinpath)/$(elf_kernel).map
	strip --strip-all $(ubinpath)/$(elf_kernel)
	ffset $(ubinpath)/fixed.vhd $(ubinpath)/$(elf_kernel) 0
	#{TODO} main kernel here
	@dd if=/dev/zero of=$(outs) bs=512 count=2880 2>>/dev/null
	@dd if=$(boot)   of=$(outs) bs=512 count=1 conv=notrunc 2>>/dev/null
	@echo $(sudokey) | sudo -S mkdir -p $(mnts)
	@echo $(sudokey) | sudo -S mount -o loop $(outs) $(mnts)
	@echo $(sudokey) | sudo -S cp $(ubinpath)/$(elf_loader) $(mnts)/KEX.OBJ
	@echo $(sudokey) | sudo -S umount $(mnts)
	@perl configs/$(arch).bochsdbg.pl > $(ubinpath)/I686/mecocoa/bochsrc.bxrc
	#
	mkdir $(uobjpath)/accm-$(arch) -p
	aasm -felf accmlib/others.asm -o accmlib/oth.o
	cd accmlib && gcc -c *.c -m32 -nostdlib  -fno-pic -static 
	#
	aasm -felf subapps/helloa/helloa.asm -o subapps/helloa/helloa.o
	ld   -s -T subapps/helloa/helloa.ld -m elf_i386 -o $(uobjpath)/accm-$(arch)/a subapps/helloa/helloa.o accmlib/*.o
	ffset $(ubinpath)/fixed.vhd $(uobjpath)/accm-$(arch)/a 256
	#
	gcc subapps/hellob/*.c accmlib/*.o -o $(uobjpath)/accm-$(arch)/b -T subapps/hellob/hellob.ld -m32 -nostdlib  -fno-pic -static 
	ffset $(ubinpath)/fixed.vhd $(uobjpath)/accm-$(arch)/b 128
	#
	@echo
	@echo "You can now debug in bochs with the command:"
	@echo $(bochd) -f $(dstdir)/bochsrc.bxrc

run: build
	@sudo $(qemu) \
		-drive format=raw,file=$(outs),if=floppy \
		-drive file=$(ubinpath)/fixed.vhd,format=raw \
		-boot order=a -m 32

clean:
	@clear
	@-rm $(uobjpath)/mcca-$(arch)/* 1>/dev/null

%.o: %.cpp
	@mkdir $(uobjpath)/mcca-$(arch) -p
	@echo "CX $(notdir $<)"
	@$(CX) $< -o $(uobjpath)/mcca-$(arch)/$(notdir $@) -O0 #-Os

