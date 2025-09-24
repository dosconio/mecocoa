# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64) Shell(Bash) Dest(atx-x64-uefi64){Arch(AMD64), BITS(64)}
# AllAuthor: @ArinaMgk (Phina.net)
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3

MKDIR = mkdir -p
RM = rm -rf

# (GNU)
GPREF   = #riscv64-unknown-elf-
CFLAGS += -nostdlib -fno-builtin -Wall -z norelro -fno-pie
CFLAGS += --static -ffreestanding -mno-red-zone -fno-exceptions -fno-rtti #-march=rv32g -mabi=ilp32
CFLAGS += -I$(uincpath) -D_MCCA=0x8664 -D_HIS_IMPLEMENT
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
asmfile=$(wildcard prehost/$(arch)/*.S)
asmobjs=$(patsubst %S, %o, $(asmfile))
cppfile=$(wildcard prehost/$(arch)/*.cpp) #$(ulibpath)/cpp/Device/UART.cpp $(ulibpath)/cpp/stream.cpp
cppobjs=$(patsubst %cpp, %o, $(cppfile))
cplfile=$(ulibpath)/c/mcore.c
cplobjs=$(patsubst %c, %o, $(cplfile))

elf_kernel=mcca-$(arch).elf

mntdir=/mnt/mcca-$(arch)

clang=clang-14

.PHONY : build
build: clean $(ubinpath)/$(arch).img # $(asmobjs) $(cppobjs) $(cplobjs)
	#echo [building] MCCA for $(arch)
	@echo MK $(elf_kernel)
	g++ $(CFLAGS) -std=c++17 -m64 -O2 \
		prehost/$(arch)/$(arch).cpp -T prehost/$(arch)/$(arch).ld -o ~/_obj/$(elf_kernel) 
	prehost/$(arch)/script-adapt.sh ~/_obj/$(elf_kernel) $(ubinpath)/$(elf_kernel)
	@sudo mkdir -p $(mntdir)
	@sudo mount -o loop $(ubinpath)/$(arch).img $(mntdir)
	@sudo mkdir -p $(mntdir)/EFI/BOOT
	@sudo cp $(ubinpath)/AMD64/loader.efi $(mntdir)/EFI/BOOT/BOOTX64.EFI
	@sudo cp $(ubinpath)/$(elf_kernel) $(mntdir)/kernel.elf
	tree $(mntdir)
	#cp to other file
	@sudo umount $(mntdir)


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

.PHONY : run
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
	@sudo mount -o loop $(ubinpath)/$(arch).img $(mntdir)
	tree $(mntdir)
	@sudo umount $(mntdir)
	@echo


#{TODO} debug

.PHONY : clean
clean:
	@echo ---- Mecocoa $(arch) ----#[clearing]
	${RM} $(uobjpath)/mcca-$(arch)/*
	${RM} $(ubinpath)/$(arch).img
	@${MKDIR} $(uobjpath)/mcca-$(arch)

%.o: %.S
	echo AS $(notdir $<)
	${CC} ${CFLAGS} -c -o $(uobjpath)/mcca-$(arch)/$(notdir $@) $<

%.o: %.c
	echo CC $(notdir $<)
	${CC} ${CFLAGS} -c -o $(uobjpath)/mcca-$(arch)/$(notdir $@) $<

%.o: %.cpp
	echo CX $(notdir $<)
	${CX} -c -o $(uobjpath)/mcca-$(arch)/$(notdir $@) $<\
		${CFLAGS} -fno-rtti


