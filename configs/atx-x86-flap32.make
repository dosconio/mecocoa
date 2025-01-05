# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64)
# LastCheck: 20240320
# AllAuthor: @dosconio
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3


iden=mccax86.img
boot=$(ubinpath)/boot-x86.bin
dstdir=E:/_bin/mecocoa
outs=$(ubinpath)/mecocoa/$(iden)
mnts=/mnt/floppy
arch=atx-x86-flap32

qemu=qemu-system-x86_64
bochd=E:/software/Bochs-2.7/bochsdbg.exe

CX=g++ -I$(uincpath) -c -D_MCCA -D_ARC_x86=5 -m32 -fleading-underscore -fno-pic -frtti -std=c++2a

ker_obj=$(uobjpath)/$(arch).obj 

cppfile=$(wildcard mecocoa/*.cpp)
cppobjs=$(patsubst %cpp, %o, $(cppfile))


#@ld $(ker_obj) $(uobjpath)/mcca-$(arch)/* -s -T prehost/$(arch)/$(arch).ld -L$(ubinpath) -lm32d -m elf_i386 -o $(ubinpath)/$(arch).elf
## -fno-exceptions to avoid __Unwind_Resume

build: clean $(cppobjs)
	@$(CX) prehost/$(arch)/$(arch).cpp -o $(ker_obj) -fno-exceptions
	# after-host...
	@g++ -o $(ubinpath)/$(arch).elf $(ker_obj) $(uobjpath)/mcca-$(arch)/* -fno-pic \
		-T prehost/$(arch)/$(arch).ld -L$(ubinpath) -static -lm32d -m32 -lstdc++ -nostartfiles
	@dd if=/dev/zero of=$(outs) bs=512 count=2880 2>>/dev/null
	@dd if=$(boot)   of=$(outs) bs=512 count=1 conv=notrunc 2>>/dev/null
	@sudo mount -o loop $(outs) $(mnts)
	@sudo cp $(ubinpath)/$(arch).elf $(mnts)/KEX.OBJ
	@sudo umount $(mnts)
	@perl configs/$(arch).bochsdbg.pl > $(ubinpath)/mecocoa/bochsrc.bxrc
	@echo
	@echo "You can now debug in bochs with the following command:"
	@echo $(bochd) -f $(dstdir)/bochsrc.bxrc

run: build
	sudo $(qemu) -drive format=raw,file=$(outs),if=floppy \
		-boot order=a -m 32

clean:
	@-rm $(uobjpath)/mcca-$(arch)/*

%.o: %.cpp
	@mkdir $(uobjpath)/mcca-$(arch) -p
	@echo "CX $(<)"
	@$(CX) $< -o $(uobjpath)/mcca-$(arch)/$(notdir $@) || ret 1 "!! Panic When: $(CC) $(attr) -c $< -o $(dest_obj)/$(cplpref)$(notdir $@)"

