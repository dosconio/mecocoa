# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64) Shell(Bash)
# AllAuthor: @dosconio
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3


iden=mccax86.img
boot=$(ubinpath)/boot-x86.bin
dstdir=E:/_bin/mecocoa
outs=$(ubinpath)/mecocoa/$(iden)
mnts=/mnt/floppy
arch=atx-x86-flap32
flag=-D_MCCA=0x8632 -D_ARC_x86=5

qemu=qemu-system-x86_64
bochd=E:/software/Bochs-2.7/bochsdbg.exe

CXF=-fno-rtti -fno-exceptions -fno-unwind-tables -static -nostdlib#-nodefaultlibs #
CX=g++ -I$(uincpath) -c $(flag) -m32 $(CXF) -std=c++2a -Wno-builtin-declaration-mismatch

ker_mod=$(uobjpath)/mcca-$(arch)/*

cppfile=$(wildcard mecocoa/*.cpp)
cppobjs=$(patsubst %cpp, %o, $(cppfile))
sudokey=k

build: clean $(cppobjs)
	@echo "MK mecocoa $(arch) loader"
	g++ -I$(uincpath) $(flag) -m32 $(ker_mod) prehost/$(arch)/$(arch).loader.cpp -o $(ubinpath)/$(arch).loader.elf -L$(ubinpath) -lm32d $(CXF) \
		-T prehost/$(arch)/$(arch).loader.ld  \
		-nostartfiles -Os
	strip --strip-all $(ubinpath)/$(arch).loader.elf
	@echo "MK mecocoa $(arch)"
	g++ -I$(uincpath) $(flag) -m32 $(ker_mod) prehost/$(arch)/$(arch).cpp -o $(ubinpath)/$(arch).elf -L$(ubinpath) -lm32d $(CXF) \
		-T prehost/$(arch)/$(arch).ld  \
		-nostartfiles -Os
	strip --strip-all $(ubinpath)/$(arch).elf
	ffset $(ubinpath)/fixed.vhd $(ubinpath)/$(arch).elf 0
	#{TODO} main kernel here
	@dd if=/dev/zero of=$(outs) bs=512 count=2880 2>>/dev/null
	@dd if=$(boot)   of=$(outs) bs=512 count=1 conv=notrunc 2>>/dev/null
	@echo $(sudokey) | sudo mount -o loop $(outs) $(mnts)
	@echo $(sudokey) | sudo cp $(ubinpath)/$(arch).loader.elf $(mnts)/KEX.OBJ
	@echo $(sudokey) | sudo umount $(mnts)
	@perl configs/$(arch).bochsdbg.pl > $(ubinpath)/mecocoa/bochsrc.bxrc
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
	@$(CX) $< -o $(uobjpath)/mcca-$(arch)/$(notdir $@) -Os

