# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64) Shell(Bash)
# AllAuthor: @dosconio
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3

# ln -s /mnt/hgfs/her /her
uherpath=/her
archdir=$(ubinpath)/I686/mecocoa


iden=mccax86.img
boot=$(ubinpath)/boot-x86.bin
outs=$(archdir)/$(iden)
mnts=/mnt/floppy
mntdir=/mnt/floppy
arch=atx-x86-flap32
flag=-D_MCCA=0x8632 -D_ARC_x86=5 -D_DEBUG 

qemu=qemu-system-i386 # not support ia32e
bochd=C:/Soft/Bochs-2.7/bochsdbg.exe

CXF1=-m32 -mno-red-zone -mno-sse -mno-sse2 -mno-sse3 -mno-ssse3 -mno-sse4
CXF2=-ffreestanding -fno-omit-frame-pointer -fno-stack-protector -fno-pic -fno-exceptions -fno-unwind-tables -fno-builtin -fno-strict-aliasing -ffunction-sections -fdata-sections
CXF=$(CXF1) $(CXF2) -fno-rtti -fno-use-cxa-atexit -static -nostdlib 
CXW=-Wno-builtin-declaration-mismatch -Wno-volatile -Wno-multichar
CX=g++ -I$(uincpath) -Idepends/freetype/x86/include -c $(flag) $(CXF) $(CXW) -std=c++2a

ker_mod=$(uobjpath)/mcca-$(arch)/*.o

cppfile=$(wildcard mecocoa/*.cpp) $(wildcard devdriv/*.cpp) $(wildcard devdriv/**/*.cpp) $(wildcard depends/*.cpp)
cppobjs=$(patsubst %.cpp, $(uobjpath)/mcca-$(arch)/%.o, $(notdir $(cppfile)))
VPATH = $(sort $(dir $(cppfile)))

sudokey=k
elf_kernel=$(archdir)/mcca-$(arch).elf
iso_kernel=$(ubinpath)/mcca.iso

.PHONY: build install lib accm run clean

build: lib accm prehost/$(arch)/fatvhd.ignore $(cppobjs) build_util
	@echo "MK $(arch) real16 support"
	aasm prehost/$(arch)/atx-x86.asm        -felf   -o $(uobjpath)/mcca-$(arch)/mcca-$(arch)-elf16.o  -Iinclude/
	aasm prehost/$(arch)/atx-ladder.asm     -felf   -o $(uobjpath)/mcca-$(arch)/mcca-$(arch)-ladder.o -Iinclude/ -D_MCCA=0x8632
	aasm prehost/$(arch)/atx-x86-loader.asm -felf   -o $(uobjpath)/mcca-$(arch)/mcca-$(arch)-elf64.o
	@echo "CX _auxiliary.cpp"
	$(CX) -O2 prehost/_auxiliary.cpp -o $(uobjpath)/mcca-$(arch)._auxiliary.o
	#
	@echo "CX $(arch).cpp"
	$(CX) -O2 prehost/$(arch)/$(arch).cpp -o $(uobjpath)/mcca-$(arch).kernel.o
	@echo "MK $(arch)"
	$(CX) prehost/$(arch)/grubhead.S -o $(uobjpath)/mcca-$(arch).grub.o
	ld -m elf_i386 -static -nostdlib --gc-sections \
		$(uobjpath)/mcca-$(arch).grub.o \
		$(ker_mod) \
		$(uobjpath)/mcca-$(arch).kernel.o \
		$(uobjpath)/mcca-$(arch)._auxiliary.o \
		-o $(elf_kernel) \
		-L depends/freetype/x86 -lfreetype \
		-L$(ubinpath) -lm32d \
		-T prehost/$(arch)/$(arch).ld  \
		-Map $(elf_kernel).map
	strip --strip-all $(elf_kernel)

	# ---- Floppy (Outdated) ----

	# --- CD ---
	mkdir -p $(ubinpath)/mecocoa/boot/grub
	mkdir -p $(ubinpath)/mecocoa/apps
	mkdir -p $(ubinpath)/mecocoa/demo
	-rm -rf  $(ubinpath)/mecocoa/apps/*
	-rm -rf $(iso_kernel)
	cp configs/grub-x86.txt $(ubinpath)/mecocoa/boot/grub/grub.cfg
	cp $(elf_kernel) $(ubinpath)/mecocoa/boot/
	cp $(uobjpath)/sapp-$(arch)/*    $(ubinpath)/mecocoa/apps/
	echo "ようこそ，メココAの世界へ！" | sudo tee "$(ubinpath)/mecocoa/ciallo.txt" > /dev/null
	cp $(ulibpath)/../.picture/phina.head.bmp  $(ubinpath)/mecocoa/demo/
	cd $(ubinpath) && grub-mkrescue -o mcca.iso mecocoa

	# --- write out ---
	@echo MK  $(arch) patadisk 0:0
	@echo $(sudokey) | sudo -S kpartx -av $(ubinpath)/fixed2.vhd  >/dev/null # ls /dev/mapper/loop*p* && sudo mkfs.vfat -F 32 -n "DATA" /dev/mapper/loop*p7
	@echo $(sudokey) | sudo -S mkfs.fat -F 32 -n "DATA" /dev/mapper/loop*p7 >/dev/null
	@echo $(sudokey) | sudo -S mount /dev/mapper/loop*p7 $(mnts) #sudo fsck.vfat -v /dev/mapper/loop0p7 # fdisk # blkid
	@echo $(sudokey) | sudo -S cp $(elf_kernel)     $(mnts)/mx86.elf
	@echo $(sudokey) | sudo -S cp depends/fonts/simsun.ttf                $(mnts)/
	@tree $(mnts) -s
	@echo $(sudokey) | sudo -S umount $(mnts)
	@echo $(sudokey) | sudo -S kpartx -dv $(ubinpath)/fixed2.vhd >/dev/null
	#
	@echo
	@echo Run \"make -f accmlib/accmx86.make\" to build accm-x86
	@echo "You can now debug in bochs with the command:"
	@echo "  " $(bochd) -f %ubinpath%/I686/mecocoa/bochsrc.bxrc
	@echo "  " bochs -f $(archdir)/bochsrc-lin.bxrc -debugger

ACCM_INCF=-I$(uincpath) -I$(uincpath)/c/API-POSIX
ACCM_LIBS=accm-x86
#[Tool System] gcc clang
TOOLSYS=clang
build_util:
	@make -f subapps/Makefile.$(TOOLSYS).x86 \
		arch=$(arch) \
		uherpath=$(uherpath) \
		uobjpath=$(uobjpath) \
		uincpath=$(uincpath) \
		ulibpath=$(ulibpath) \
		ACCM_INCF="$(ACCM_INCF)" \
		flag="$(flag)" \
		CXF="$(CXF)" \
		CXF2="$(CXF2)" \
		CXW="$(CXW)" \
		ACCM_LIBS="$(ACCM_LIBS)"


install:
	@echo $(sudokey) | sudo -S cp $(elf_kernel)     /boot/mx86.elf

lib:
	@echo MK lib for MCCA-x86
	cd $(ulibpath)/.. && make mx86 -j

accm:
	@echo MK lib for ACCM-x86
	make -f accmlib/accmx86.$(TOOLSYS).make

prehost/$(arch)/fatvhd.ignore: build_util
	@echo MK  $(arch) memdisk
	dd if=/dev/zero of=$@ bs=1M count=1
	mkfs.fat -n 'MECOCOA2' -s 2 -f 2 -R 32 -F 32 $@
	@echo $(sudokey) | sudo -S mount -o loop $@ $(mntdir)
	@echo $(sudokey) | sudo -S mv $(uobjpath)/sapp-$(arch)/init $(mntdir)/init
	@echo $(sudokey) | sudo -S cp $(uobjpath)/sapp-$(arch)/cot  $(mntdir)/cot
	# from uni-utils:
	@echo $(sudokey) | sudo -S mv $(uobjpath)/sapp-$(arch)/ls     $(mntdir)/ls
	@echo $(sudokey) | sudo -S mv $(uobjpath)/sapp-$(arch)/ribbon $(mntdir)/ribbon
	@echo $(sudokey) | sudo -S umount $(mntdir)

$(uobjpath)/mcca-$(arch)/memodisk.o: prehost/$(arch)/fatvhd.ignore

qemu_args=-smp 4,cores=2,threads=2 -M pc \
	-cdrom $(iso_kernel) \
	-boot order=d -m 1G\
	-drive format=raw,file=$(outs),if=floppy \
	-drive file=$(ubinpath)/fixed2.vhd,format=vpc,if=none,id=disk0 \
	-device ide-hd,drive=disk0,bus=ide.0,unit=0 \
	-serial stdio\

# [SATA]
#	-device ahci,id=ahci0 \
#   -device ide-hd,drive=disk1,bus=ahci0.0 \
# [PATA]
#	-drive file=$(ubinpath)/fixed2.vhd,format=vpc,if=none,id=disk1 \
# 	-drive file=/home/phina/mcca/disk_pata01.vhd,format=raw,if=none,id=disk1 \
#	-device ide-hd,drive=disk1,bus=ide.0,unit=1 \


run: build run-only
run-only:
	$(qemu) \
		$(qemu_args) -audiodev pa,id=speaker -machine pcspk-audiodev=speaker \
		-enable-kvm -cpu host || $(qemu) \
		$(qemu_args) -audiodev dsound,id=speaker -machine pcspk-audiodev=speaker

clean:
	@echo ---- Mecocoa $(arch) ----#[clearing]
	@-rm $(uobjpath)/mcca-$(arch)/* 1>/dev/null
	@make -f subapps/Makefile.gcc.x86 clean \
		arch=$(arch) \
		uobjpath=$(uobjpath)
clean-app:
	@-rm -r $(uobjpath)/sapp-$(arch)/*

$(uobjpath)/mcca-$(arch)/%.o: %.cpp
	@mkdir $(uobjpath)/mcca-$(arch) -p
	@echo "CX $(notdir $<)"
	@$(CX) $< -o $@ -O0 -MMD -MF $(patsubst %.o,%.d,$@) -MT $@
	@if [ "$(notdir $<)" = "freetype.cpp" ]; then objcopy --rename-section .text=.ext.freetype --rename-section .rodata=.ext.freetype --rename-section .data=.ext.freetype $@; fi

-include $(uobjpath)/mcca-$(arch)/*.d
