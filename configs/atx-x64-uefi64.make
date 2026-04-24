# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64) Shell(Bash) Dest(atx-x64-uefi64){Arch(AMD64), BITS(64)}
# AllAuthor: @ArinaMgk (Phina.net)
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3

arch?=atx-x64-uefi64

MKDIR = mkdir -p
RM = rm -rf

# (GNU)
GPREF   = x86_64-linux-gnu-
CFLAGS += -nostdlib -fno-builtin -z norelro
CFLAGS += --static -mno-red-zone -m64  -mno-sse -mno-sse2 -mno-avx -mno-avx2
# -msoft-float
CFLAGS += -I$(uincpath) -D_MCCA=0x8664 -D_UEFI -D_DEBUG
CFLAGS += -fno-strict-aliasing -fno-exceptions -fno-stack-protector
CFLAGS += -Wextra -Wno-multichar
XFLAGS  = $(CFLAGS) -fno-rtti -fno-use-cxa-atexit
G_DBG   = gdb-multiarch
CC      = ${GPREF}gcc -O2
CX      = ${GPREF}g++ -O2 -std=c++23
OBJCOPY = ${GPREF}objcopy
OBJDUMP = ${GPREF}objdump

# (QEMU)
QARCH  = x86_64
QEMU   = qemu-system-$(QARCH)
QBOARD = atx
QFLAGS = -drive file=$(ubinpath)/AMD64/OVMF/OVMF_CODE.fd,format=raw,if=pflash -drive file=$(ubinpath)/AMD64/OVMF/OVMF_VARS.fd,format=raw,if=pflash # UEFI

#
LDFILE  = prehost/$(arch)/$(arch).ld
LDFLAGS = -T $(LDFILE)


archdir=$(ubinpath)/AMD64/mecocoa

#
asmfile=prehost/$(arch)/atx-x64.asm\
	$(ulibpath)/asm/x64/task.asm \
	$(ulibpath)/asm/x64/cpuid.asm \
	$(ulibpath)/asm/x64/sysman.asm \
	$(ulibpath)/asm/x64/inst/ioport.asm \
	$(ulibpath)/asm/x64/inst/manage.asm \
	$(ulibpath)/asm/x64/inst/interrupt.asm \
	$(ulibpath)/asm/x64/interrupt/ruptable.asm \
	

cppfile=\
	$(ulibpath)/cpp/color.cpp \
	$(ulibpath)/cpp/Witch/Form.cpp \
	\
	$(ulibpath)/cpp/Device/ACPI.cpp \
	$(ulibpath)/cpp/Device/Bus/PCI.cpp \
	$(ulibpath)/cpp/Device/Keyboard.cpp \
	$(ulibpath)/cpp/Device/Timer.cpp \
	$(ulibpath)/cpp/Device/Mouse.cpp \
	$(ulibpath)/cpp/Device/Video.cpp $(ulibpath)/cpp/Device/Video-VideoConsole2.cpp \
	$(wildcard $(ulibpath)/cpp/Device/USB/*.cpp) $(wildcard $(ulibpath)/cpp/Device/USB/xHCI/*.cpp) \

cplfile=\
	$(ulibpath)/c/driver/keyboard.c \
	$(ulibpath)/c/data/font/font-8x5.c \
	$(ulibpath)/c/data/font/font-16x8.c \

include Makefile.inc

elf_kernel=mcca-$(arch).elf

mntdir=/mnt/floppy
clang=clang-14
sudokey=k
uherpath=/her

.PHONY : build
build: clean $(archdir)/kerdisk.fat $(ubinpath)/$(arch).img $(asmobjs) $(cppobjs) $(cplobjs)
	@echo MK $(elf_kernel)
	$(CX) $(XFLAGS) \
		-T prehost/$(arch)/$(arch).ld -o $(ubinpath)/$(elf_kernel) \
		-Wl,-Map=$(ubinpath)/$(elf_kernel).map \
		prehost/$(arch)/$(arch).cpp \
		$(uobjpath)/mcca-$(arch)/*.o \
	# OUTDATED # prehost/$(arch)/script-adapt.sh ~/_obj/$(elf_kernel) $(ubinpath)/$(elf_kernel)
	mkdir -p $(uobjpath)/sapp-$(arch)
	echo MK appinit
	$(CX) -Iaccmlib $(XFLAGS) \
		-o $(uobjpath)/sapp-$(arch)/init $(uherpath)/COTLAB/src/cotlab.cpp -L$(uobjpath)/accm-atx-x64 -latx-x64 -e _start
	echo MK subappc
	$(CX) -Iaccmlib $(XFLAGS) \
		subapps/hellocpp/* -o $(uobjpath)/sapp-$(arch)/c  -L$(uobjpath)/accm-atx-x64 -latx-x64 -e _start
	echo MK a
	aasm subapps/helloa/helloa-x64.asm -felf64 -o subapps/helloa/helloa-x64.o
	ld   -s -m elf_x86_64 -o $(uobjpath)/app-$(arch)/a subapps/helloa/helloa-x64.o -Ttext-segment=0x10000 -e main
	#
	@echo $(sudokey) | sudo -S mkdir -p $(mntdir)
	@echo $(sudokey) | sudo -S mount -o loop $(archdir)/kerdisk.fat $(mntdir)
	#
	mkdir -p $(uobjpath)/app-$(arch)
	@echo $(sudokey) | sudo -S cp $(uobjpath)/sapp-$(arch)/init $(mntdir)/init
	@echo $(sudokey) | sudo -S cp $(uobjpath)/sapp-$(arch)/c    $(mntdir)/c
	@echo $(sudokey) | sudo -S cp $(uobjpath)/app-$(arch)/a $(mntdir)/appa.elf
	tree $(mntdir)
	@echo $(sudokey) | sudo -S umount $(mntdir)
	@echo $(sudokey) | sudo -S mount -o loop $(ubinpath)/$(arch).img $(mntdir)
	@echo $(sudokey) | sudo -S mkdir -p $(mntdir)/EFI/BOOT
	@echo $(sudokey) | sudo -S cp $(ubinpath)/AMD64/loader.efi $(mntdir)/EFI/BOOT/BOOTX64.EFI
	@echo $(sudokey) | sudo -S cp $(ubinpath)/$(elf_kernel) $(mntdir)/kernel.elf
	@echo $(sudokey) | sudo -S cp $(archdir)/kerdisk.fat $(mntdir)/
	tree $(mntdir)
	@echo $(sudokey) | sudo -S umount $(mntdir)
	# update
	qemu-img convert -f raw -O vpc $(ubinpath)/$(arch).img $(ubinpath)/$(arch).vhd

$(archdir)/kerdisk.fat:
	dd if=/dev/zero of=$@ bs=1M count=32
	mkfs.fat -n 'MECOCOA2' -s 2 -f 2 -R 32 -F 32 $@

$(ubinpath)/$(arch).img: loader
	@echo MK DISK IMAGE
	qemu-img create -f raw $@ 100M > /dev/null
	mkfs.fat -n 'MECOCOA ' -s 2 -f 2 -R 32 -F 32 $@ > /dev/null


edkdir=/home/$(USER)/soft/edk2
.PHONY : loader
loader:
	@echo MK $(arch) loader
	mkdir -p $(edkdir)/MccaLoaderPkg
	cp prehost/$(arch)/$(arch).loader/*    $(edkdir)/MccaLoaderPkg/
	cp prehost/$(arch)/$(arch).loader.cfg  $(edkdir)/Conf/target.txt
	cd $(edkdir) && bash $(ulibpath)/../../mecocoa/prehost/$(arch)/$(arch).loader/build.sh
	cp $(edkdir)/Build/MccaLoaderX64/DEBUG_CLANGDWARF/X64/Loader.efi $(ubinpath)/AMD64/loader.efi

.PHONY : run run-only
qemu_args=\
	-drive if=pflash,format=raw,readonly=on,file=$(ubinpath)/AMD64/OVMF/OVMF_CODE.fd \
	-drive if=pflash,format=raw,file=$(ubinpath)/AMD64/OVMF/OVMF_VARS.fd \
	-drive if=ide,index=0,media=disk,format=raw,file=$(ubinpath)/$(arch).img \
	-device nec-usb-xhci,id=xhci \
	-device usb-mouse \
	-device usb-kbd \
	-monitor stdio \
	-no-reboot -no-shutdown  \

run: build
	@echo [ running] MCCA for $(arch)
	@${QEMU} $(qemu_args) -m 512M -enable-kvm || ${QEMU} $(qemu_args) -m 512M
	@echo
	@echo 'Mount the image:' 
	@echo '  mount -o loop $(ubinpath)/$(arch).img $(mntdir)'
	@echo $(sudokey) | sudo -S mount -o loop $(ubinpath)/$(arch).img $(mntdir)
	tree $(mntdir)
	@echo $(sudokey) | sudo -S umount $(mntdir)
	@echo
run-only:
	@${QEMU} $(qemu_args) -m 512M -enable-kvm || ${QEMU} $(qemu_args) -m 512M

#{TODO} debug

.PHONY : clean
clean:
	@echo ---- Mecocoa $(arch) ----#[clearing]
# 	${RM} $(uobjpath)/mcca-$(arch)/*
	${RM} $(archdir)/kerdisk.fat
	${RM} $(ubinpath)/$(arch).img
	@${MKDIR} $(uobjpath)/mcca-$(arch)

_ae_%.o:
	echo AS $(notdir $<)
	aasm -f elf64      -o $@ $< -D_MCCA=0x8664 -D_UEFI -MD $(patsubst %.o,%.d,$@)

_cc_%.o:
	echo CC $(notdir $<)
	${CC} ${CFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

_cx_%.o:
	echo CX $(notdir $<)
	${CX} ${XFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

-include $(dest_obj)/*.d

