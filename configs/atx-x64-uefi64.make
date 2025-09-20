# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64) Shell(Bash) Dest(atx-x64-uefi64){Arch(AMD64), BITS(64)}
# AllAuthor: @ArinaMgk (Phina.net)
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3

MKDIR = mkdir -p
RM = rm -rf

# (GNU)
GPREF   = #riscv64-unknown-elf-
CFLAGS += #-nostdlib -fno-builtin  -Wall -Wno-unused-variable -Wno-unused-function -Wno-parentheses # -g
CFLAGS += #-march=rv32g -mabi=ilp32
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
	@sudo mkdir -p $(mntdir)
	@sudo mount -o loop $(ubinpath)/$(arch).img $(mntdir)
	@sudo mkdir -p $(mntdir)/EFI/BOOT
	@sudo cp $(ubinpath)/AMD64/loader.efi $(mntdir)/EFI/BOOT/BOOTX64.EFI
	# tree $(mntdir)
	@sudo umount $(mntdir)

$(ubinpath)/$(arch).img: loader
	@echo MK DISK IMAGE
	qemu-img create -f raw $@ 100M > /dev/null
	mkfs.fat -n 'MECOCOA ' -s 2 -f 2 -R 32 -F 32 $@ > /dev/null

.PHONY : loader
loader:
	@echo MK $(arch) loader
	$(clang) $(CFLAGS) -target x86_64-pc-win32-coff -o $(uobjpath)/$(arch).loader.o -c prehost/$(arch)/$(arch).loader.cpp \
		-fno-rtti -fno-exceptions -fno-unwind-tables -static -nostdlib -fno-pic
	lld-link-14 /subsystem:efi_application /entry:EfiMain /out:$(ubinpath)/AMD64/loader.efi $(uobjpath)/$(arch).loader.o

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


