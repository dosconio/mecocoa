# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64) Shell(Bash) Dest(qemuvirt-r32){Arch(RISCV), BITS(32)}
# AllAuthor: @ArinaMgk (Phina.net)
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3

MKDIR = mkdir -p
RM = rm -rf

# (GNU)
GPREF   = riscv64-unknown-elf-
CFLAGS += -nostdlib -fno-builtin -g -Wall -Wno-unused-variable -Wno-unused-function -Wno-parentheses
CFLAGS += -march=rv32g -mabi=ilp32
CFLAGS += -I$(uincpath) -D_MCCA=0x1032 -D_HIS_IMPLEMENT# 1032 for RV32
G_DBG   = gdb-multiarch
CC      = ${GPREF}gcc
CX      = ${GPREF}g++
OBJCOPY = ${GPREF}objcopy
OBJDUMP = ${GPREF}objdump

# (QEMU)
QARCH  = riscv32
QEMU   = qemu-system-$(QARCH)
QBOARD = virt
QFLAGS = -nographic -smp 1 -machine $(QBOARD) -bios none

#
LDFLAGS = -T prehost/$(arch)/$(arch).ld
#
asmfile=$(wildcard prehost/$(arch)/*.S)
asmobjs=$(patsubst %S, %o, $(asmfile))
cppfile=$(wildcard prehost/$(arch)/*.cpp) $(ulibpath)/cpp/Device/UART.cpp $(ulibpath)/cpp/stream.cpp
cppobjs=$(patsubst %cpp, %o, $(cppfile))
cplfile=$(ulibpath)/c/mcore.c
cplobjs=$(patsubst %c, %o, $(cplfile))

elf_kernel=mcca-$(arch).elf

.PHONY : build
build: clean $(asmobjs) $(cppobjs) $(cplobjs)
	#echo [building] MCCA for $(arch)
	@echo MK $(elf_kernel)
	@perl configs/qemuvirt-r32.pl > prehost/$(arch)/$(arch).ld
	@${CC} ${CFLAGS} ${LDFLAGS} -o $(ubinpath)/${elf_kernel} $(uobjpath)/mcca-$(arch)/* # keep entry (not only code segment) at 0x80000000
	# @${CC} ${CFLAGS} ${LDFLAGS} -o $(ubinpath)/${elf_kernel} prehost/$(arch)/*.S $(uobjpath)/mcca-$(arch)/*
	# readelf -h $ubinpath/mcca-qemuvirt-r32.elf| grep Entry
	# bin_kernel : ${OBJCOPY} -O binary ${elf_kernel} ${BIN}

.PHONY : run
run: build
	@echo [ running] MCCA for $(arch)
	@echo "( Press ^A and then X to exit QEMU )"
	@${QEMU} ${QFLAGS} -kernel $(ubinpath)/${elf_kernel}
# 	@${QEMU} -M ? | grep virt >/dev/null || exit


#{TODO} debug

.PHONY : clean
clean:
	@echo ---- Mecocoa $(arch) ----#[clearing]
	${RM} $(uobjpath)/mcca-$(arch)/*
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


