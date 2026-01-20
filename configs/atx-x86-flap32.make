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
flag=-D_MCCA=0x8632 -D_ARC_x86=5 -D_DEBUG 

qemu=qemu-system-i386 # not support ia32e
bochd=C:/Soft/Bochs-2.7/bochsdbg.exe

COMWAN = -Wno-builtin-declaration-mismatch
CXF1=-m32 -mno-red-zone -mno-sse -mno-sse2 -mno-sse3 -mno-ssse3 -mno-sse4 # -mgeneral-regs-only
CXF2=-fno-stack-protector -fno-pic -fno-exceptions -fno-unwind-tables -fno-builtin
CXF=$(CXF1) $(CXF2) -fno-rtti -static -nostdlib $(COMWAN)
CXW=-Wno-builtin-declaration-mismatch -Wno-volatile -Wno-multichar
CX=g++ -I$(uincpath) -c $(flag) $(CXF) $(CXW) -std=c++2a

ker_mod=$(uobjpath)/mcca-$(arch)/*

cppfile=$(wildcard mecocoa/*.cpp) $(wildcard filesys/*.cpp)
cppobjs=$(patsubst %cpp, %o, $(cppfile))

sudokey=k
elf_loader=I686/mecocoa/mcca-$(arch).loader.elf
elf_kernel=I686/mecocoa/mcca-$(arch).elf

# cfdisk of fixed2.vhd
# Device          Boot     Start      End  Sectors   Size  Id Type
# fixed2.vhd1               2048     4096     2049     1M  83 Linux           
# fixed2.vhd2               4097   163295   159199  77.7M   5 Extended
# ├─fixed2.vhd5   *         8192    16384     8193     4M  99 unknown
# ├─fixed2.vhd6            18433    32768    14336     7M  83 Linux
# └─fixed2.vhd7            34817   163295   128479  62.7M   c W95 FAT32 (LBA)

.PHONY: build install lib accm run clean

build: clean lib $(cppobjs)
	@echo "MK $(arch) real16 support"
	aasm prehost/$(arch)/atx-x86-cppweaks.asm -felf   -o $(uobjpath)/mcca-$(arch)/mcca-$(arch)-elf16.o
	aasm prehost/$(arch)/atx-x86-loader.asm -felf     -o $(uobjpath)/mcca-$(arch)/mcca-$(arch)-elf64.o
	@echo "MK $(arch) loader"
	g++ -I$(uincpath) $(flag) -m32 prehost/$(arch)/$(arch).loader.cpp \
		prehost/$(arch)/$(arch).auf.cpp $(uobjpath)/mcca-$(arch)/mcca-$(arch)-elf64.o $(uobjpath)/CGMin32/_ae_manage.o\
		-o $(ubinpath)/$(elf_loader) -L$(ubinpath) -lm32d $(CXF) \
		-T prehost/$(arch)/$(arch).loader.ld  \
		-nostartfiles -O0 \
		-Wl,-Map=$(ubinpath)/$(elf_loader).map
	strip --strip-all $(ubinpath)/$(elf_loader)
	rm $(uobjpath)/mcca-$(arch)/mcca-$(arch)-elf64.o
	#
	@echo "MK $(arch)"
	$(CX) prehost/$(arch)/grubhead.S -o $(uobjpath)/mcca-$(arch).grub.o
	g++ -I$(uincpath) $(flag) -m32 $(uobjpath)/mcca-$(arch).grub.o $(ker_mod) prehost/$(arch)/$(arch).cpp prehost/$(arch)/$(arch).auf.cpp -o $(ubinpath)/$(elf_kernel) -L$(ubinpath) -lm32d $(CXF) \
		-T prehost/$(arch)/$(arch).ld  \
		-nostartfiles -O0 \
		-Wl,-Map=$(ubinpath)/$(elf_kernel).map
	strip --strip-all $(ubinpath)/$(elf_kernel)
	@dd if=/dev/zero of=$(outs) bs=512 count=2880 2>>/dev/null
	@dd if=$(boot)   of=$(outs) bs=512 count=1 conv=notrunc 2>>/dev/null
	@echo $(sudokey) | sudo -S mkdir -p $(mnts)
	@echo $(sudokey) | sudo -S mount -o loop $(outs) $(mnts)
	@echo $(sudokey) | sudo -S cp $(ubinpath)/$(elf_loader) $(mnts)/KEX.OBJ
	@echo $(sudokey) | sudo -S umount $(mnts)
	@perl configs/$(arch).bochsdbg.pl > $(ubinpath)/I686/mecocoa/bochsrc.bxrc
	@perl configs/$(arch).bochsdbg-lin.pl > $(ubinpath)/I686/mecocoa/bochsrc-lin.bxrc
	#
	echo MK appinit
	g++ -I$(uincpath) $(flag) -m32 $(CXF) $(CXW) -std=c++2a \
		-o $(uobjpath)/sapp-$(arch)/init subapps/appinit.cpp -L$(uobjpath)/accm-$(arch) -l$(arch)
	echo MK subappc
	g++ -I$(uincpath) $(flag) -m32 $(CXF) $(CXW) -std=c++2a \
		subapps/helloc/* -o $(uobjpath)/sapp-$(arch)/c  -L$(uobjpath)/accm-$(arch) -l$(arch)
	# --- write out ---
	@echo $(sudokey) | sudo -S kpartx -av $(ubinpath)/fixed2.vhd  >/dev/null # ls /dev/mapper/loop*p* && sudo mkfs.vfat -F 32 -n "DATA" /dev/mapper/loop*p7
	@echo $(sudokey) | sudo -S mount /dev/mapper/loop*p7 $(mnts) #sudo fsck.vfat -v /dev/mapper/loop0p7 # fdisk # blkid
	@echo $(sudokey) | sudo -S cp $(ubinpath)/$(elf_kernel)     $(mnts)/mx86.elf
	@echo $(sudokey) | sudo -S cp $(uobjpath)/sapp-$(arch)/init $(mnts)/init
	@echo $(sudokey) | sudo -S cp $(uobjpath)/sapp-$(arch)/c    $(mnts)/c
	@tree $(mnts) -s
	@echo $(sudokey) | sudo -S umount $(mnts)
	@echo $(sudokey) | sudo -S kpartx -dv $(ubinpath)/fixed2.vhd >/dev/null
	#
	@echo
	@echo Run \"make -f accmlib/accmx86.make\" to build accm-x86
	@echo "You can now debug in bochs with the command:"
	@echo "  " $(bochd) -f $(dstdir)/bochsrc.bxrc
	@echo "  " bochs -f $(ubinpath)/I686/mecocoa/bochsrc-lin.bxrc -debugger

install:
	@echo $(sudokey) | sudo -S cp $(ubinpath)/$(elf_kernel)     /boot/mx86.elf

lib:
	cd $(ulibpath)/.. && make mx86 -j

accm:
	make -f accmlib/accmx86.make

subappa:
	echo MK subappa
	aasm -felf subapps/helloa/helloa.asm -o subapps/helloa/helloa.o
	ld   -s -m elf_i386 -o $(uobjpath)/app-$(arch)/a subapps/helloa/helloa.o accmlib/*.o
	# ...
subappb:
	echo MK subappb
	gcc subapps/hellob/*.c accmlib/*.o -o $(uobjpath)/app-$(arch)/b -m32 -nostdlib  -fno-pic -static -I$(uincpath) -D_ACCM=0x8632
	# ...

run: build run-only
run-only:
	$(qemu) \
		-drive format=raw,file=$(outs),if=floppy \
		-boot order=a -m 32\
		-drive file=$(ubinpath)/fixed1.vhd,format=vpc,if=none,id=disk0 \
		-device ide-hd,drive=disk0,bus=ide.0,unit=0 \
		-drive file=$(ubinpath)/fixed2.vhd,format=vpc,if=none,id=disk1 \
		-device ide-hd,drive=disk1,bus=ide.0,unit=1 \
		-audiodev pa,id=speaker -machine pcspk-audiodev=speaker \
		-enable-kvm -cpu host || $(qemu) \
		-drive format=raw,file=$(outs),if=floppy \
		-boot order=a -m 32\
		-drive file=$(ubinpath)/fixed1.vhd,format=vpc,if=none,id=disk0 \
		-device ide-hd,drive=disk0,bus=ide.0,unit=0 \
		-drive file=$(ubinpath)/fixed2.vhd,format=vpc,if=none,id=disk1 \
		-device ide-hd,drive=disk1,bus=ide.0,unit=1 \
		-audiodev dsound,id=speaker -machine pcspk-audiodev=speaker

clean:
	@echo ---- Mecocoa $(arch) ----#[clearing]
	@-rm $(uobjpath)/mcca-$(arch)/* 1>/dev/null

%.o: %.cpp
	@mkdir $(uobjpath)/mcca-$(arch) -p
	@echo "CX $(notdir $<)"
	@$(CX) $< -o $(uobjpath)/mcca-$(arch)/$(notdir $@) -O0 #-Os

