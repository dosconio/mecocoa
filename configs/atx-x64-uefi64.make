# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64) Shell(Bash) Dest(atx-x64-uefi64){Arch(AMD64), BITS(64)}
# AllAuthor: @ArinaMgk (Phina.net)
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3

arch?=atx-x64-uefi64

MKDIR = mkdir -p
RM = rm -rf

# (GNU)
GPREF   = #riscv64-unknown-elf-
CFLAGS += -nostdlib -fno-builtin -z norelro -nostdlib -fno-builtin
CFLAGS += --static -mno-red-zone -m64  -O0
CFLAGS += -I$(uincpath) -D_MCCA=0x8664 -D_HIS_IMPLEMENT -D_DEBUG
CFLAGS += -fno-strict-aliasing -fno-exceptions -ffreestanding # -Wall -fno-pie
XFLAGS  = $(CFLAGS) -fno-rtti -std=c++23
G_DBG   = gdb-multiarch
CC      = ${GPREF}gcc
CX      = ${GPREF}g++
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
#
asmfile=$(ulibpath)/asm/x64/inst/ioport.asm \
	$(ulibpath)/asm/x64/inst/manage.asm \
	$(ulibpath)/asm/x64/inst/interrupt.asm

cppfile=$(wildcard mecocoa/*.cpp) \
	$(ulibpath)/cpp/stream.cpp \
	$(ulibpath)/cpp/interrupt.cpp \
	$(ulibpath)/cpp/lango/lango-cpp.cpp \
	$(ulibpath)/cpp/Device/Bus/PCI.cpp \
	$(ulibpath)/cpp/Device/USB/USB-Device.cpp \
	$(ulibpath)/cpp/Device/USB/xHCI/xHCI.cpp \
	$(ulibpath)/cpp/Device/Keyboard.cpp \
	$(ulibpath)/cpp/Device/Mouse.cpp \
	$(ulibpath)/cpp/Device/Video.cpp $(ulibpath)/cpp/Device/Video-VideoConsole.cpp \

cplfile=$(ulibpath)/c/mcore.c\
	$(ulibpath)/c/debug.c \
	$(ulibpath)/c/console/conformat.c \
	$(ulibpath)/c/data/font/font-8x5.c \
	$(ulibpath)/c/data/font/font-16x8.c \


asmobjs=$(patsubst %asm, %o, $(asmfile))
cppobjs=$(patsubst %cpp, %o, $(cppfile))
cplobjs=$(patsubst %c, %o, $(cplfile))
elf_kernel=mcca-$(arch).elf

mntdir=/mnt/mcca-$(arch)
clang=clang-14
sudokey=k

.PHONY : build
build: clean $(ubinpath)/$(arch).img $(asmobjs) $(cppobjs) $(cplobjs)
	#echo [building] MCCA for $(arch)
# 	@echo AR $(elf_kernel)
# 	@ar -rcs $(uobjpath)/mcca-$(arch)/lib$(elf_kernel).a $(uobjpath)/mcca-$(arch)/*
	@echo MK $(elf_kernel)
	$(CX) $(XFLAGS) \
		prehost/$(arch)/$(arch).cpp -T prehost/$(arch)/$(arch).ld -o $(ubinpath)/$(elf_kernel) \
		-Wl,-Map=$(ubinpath)/$(elf_kernel).map \
		$(uobjpath)/mcca-$(arch)/*
#		-L $(uobjpath)/mcca-$(arch) -l$(elf_kernel)
		

	# OUTDATED # prehost/$(arch)/script-adapt.sh ~/_obj/$(elf_kernel) $(ubinpath)/$(elf_kernel)
	@echo $(sudokey) | sudo -S mkdir -p $(mntdir)
	@echo $(sudokey) | sudo -S mount -o loop $(ubinpath)/$(arch).img $(mntdir)
	@echo $(sudokey) | sudo -S mkdir -p $(mntdir)/EFI/BOOT
	@echo $(sudokey) | sudo -S cp $(ubinpath)/AMD64/loader.efi $(mntdir)/EFI/BOOT/BOOTX64.EFI
	@echo $(sudokey) | sudo -S cp $(ubinpath)/$(elf_kernel) $(mntdir)/kernel.elf
	tree $(mntdir)
	@echo $(sudokey) | sudo -S umount $(mntdir)
	# update
	qemu-img convert -f raw -O vpc $(ubinpath)/$(arch).img $(ubinpath)/$(arch).vhd


$(ubinpath)/$(arch).img: loader
	@echo MK DISK IMAGE
	qemu-img create -f raw $@ 100M > /dev/null
	mkfs.fat -n 'MECOCOA ' -s 2 -f 2 -R 32 -F 32 $@ > /dev/null


edkdir=/home/phina/soft/edk2
.PHONY : loader
loader:
	@echo MK $(arch) loader
	mkdir -p $(edkdir)/MccaLoaderPkg
	cp prehost/$(arch)/$(arch).loader/*    $(edkdir)/MccaLoaderPkg/
	cp prehost/$(arch)/$(arch).loader.cfg  $(edkdir)/Conf/target.txt
	cd $(edkdir) && bash $(ulibpath)/../../mecocoa/prehost/$(arch)/$(arch).loader/build.sh
	cp $(edkdir)/Build/MccaLoaderX64/DEBUG_CLANGDWARF/X64/Loader.efi $(ubinpath)/AMD64/loader.efi

.PHONY : run run-only
run: build
	@echo [ running] MCCA for $(arch)
	@${QEMU} \
	    -drive if=pflash,format=raw,readonly=on,file=$(ubinpath)/AMD64/OVMF/OVMF_CODE.fd \
		-drive if=pflash,format=raw,file=$(ubinpath)/AMD64/OVMF/OVMF_VARS.fd \
		-drive if=ide,index=0,media=disk,format=raw,file=$(ubinpath)/$(arch).img \
		-device nec-usb-xhci,id=xhci \
		-device usb-mouse \
		-device usb-kbd \
		-monitor stdio \
		-m 1G # -enable-kvm
	@echo
	@echo 'Mount the image:' 
	@echo '  mount -o loop $(ubinpath)/$(arch).img $(mntdir)'
	@echo $(sudokey) | sudo -S mount -o loop $(ubinpath)/$(arch).img $(mntdir)
	tree $(mntdir)
	@echo $(sudokey) | sudo -S umount $(mntdir)
	@echo
run-only:
	@${QEMU} \
	    -drive if=pflash,format=raw,readonly=on,file=$(ubinpath)/AMD64/OVMF/OVMF_CODE.fd \
		-drive if=pflash,format=raw,file=$(ubinpath)/AMD64/OVMF/OVMF_VARS.fd \
		-drive if=ide,index=0,media=disk,format=raw,file=$(ubinpath)/$(arch).img \
		-device nec-usb-xhci,id=xhci \
		-device usb-mouse \
		-device usb-kbd \
		-monitor stdio \
		-m 1G # -enable-kvm

#{TODO} debug

.PHONY : clean
clean:
	@echo ---- Mecocoa $(arch) ----#[clearing]
	${RM} $(uobjpath)/mcca-$(arch)/*
	${RM} $(ubinpath)/$(arch).img
	@${MKDIR} $(uobjpath)/mcca-$(arch)

%.o: %.asm
	echo AS $(notdir $<)
	aasm -f elf64 -o $(uobjpath)/mcca-$(arch)/_ae_$(notdir $@) $<

%.o: %.S
	echo AS $(notdir $<)
	${CC} ${CFLAGS} -c -o $(uobjpath)/mcca-$(arch)/$(notdir $@) $<

%.o: %.c
	echo CC $(notdir $<)
	${CC} ${CFLAGS} -c -o $(uobjpath)/mcca-$(arch)/$(notdir $@) $<

%.o: %.cpp
	echo CX $(notdir $<)
	${CX} ${XFLAGS} -c -o $(uobjpath)/mcca-$(arch)/$(notdir $@) $<


