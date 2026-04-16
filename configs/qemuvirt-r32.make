# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64) Shell(Bash) Dest(qemuvirt-r32){Arch(RISCV), BITS(32)}
# AllAuthor: @ArinaMgk (Phina.net)
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3

MKDIR = mkdir -p
RM = rm -rf

# (GNU)
RISCV_ELF_EXISTS := $(shell command -v riscv64-elf-gcc 2>/dev/null)
RISCV_UNKNOWN_EXISTS := $(shell command -v riscv64-unknown-elf-gcc 2>/dev/null)
ifeq ($(RISCV_ELF_EXISTS),)
    GPREF := riscv64-unknown-elf-
else
    GPREF := riscv64-elf-
endif
CFLAGS += -nostdlib -fno-builtin -Wall -Wno-unused-variable -Wno-unused-function -Wno-parentheses -Wno-comment
CFLAGS += -march=rv32g -mabi=ilp32
CFLAGS += -I$(uincpath) -D_MCCA=0x1032 -D_OPT_RISCV32 -D_DEBUG
CFLAGS += -fno-strict-aliasing -fno-exceptions -fno-stack-protector
CFLAGS += -g
XFLAGS  = $(CFLAGS) -fno-rtti -fno-use-cxa-atexit
G_DBG   = gdb
CC      = ${GPREF}gcc -O2
CX      = ${GPREF}g++ -O2
OBJCOPY = ${GPREF}objcopy
OBJDUMP = ${GPREF}objdump

# (QEMU)
CORES  = 1
QARCH  = riscv32
QEMU   = qemu-system-$(QARCH)
QBOARD = virt
QFLAGS = -smp $(CORES) -machine $(QBOARD) -bios none

#
LDFILE  = prehost/$(arch)/$(arch).ld
LDFLAGS = -T $(LDFILE).ignore

#
gasfile=$(wildcard prehost/$(arch)/*.S)
cppfile=$(wildcard prehost/$(arch)/*.cpp) 
include Makefile.inc

elf_kernel=mcca-$(arch).elf

archdir=$(ubinpath)/RISCV32/mecocoa
mntdir=/mnt/floppy
clang=clang-14
sudokey=k

.PHONY : build
build: clean prehost/$(arch)/fatvhd.ignore $(gasobjs) $(cppobjs) $(cplobjs)
	#echo [building] MCCA for $(arch)
	@echo MK $(elf_kernel)
	@perl configs/qemuvirt-riscv.pl r32 > $(LDFILE).ignore
	@${CC} -E -P -x c ${CFLAGS} $(LDFILE).ignore -D_DEV_GNU_AS -D__BITS__=32 > $(LDFILE)
	@mv $(LDFILE) $(LDFILE).ignore
	# keep entry (not only code segment) at 0x80000000
	@${CC} ${XFLAGS} ${LDFLAGS} -o $(ubinpath)/${elf_kernel} $(uobjpath)/mcca-$(arch)/*.o \
		-Wl,-Map=$(ubinpath)/$(elf_kernel).map
	# readelf -h $ubinpath/mcca-qemuvirt-r32.elf| grep Entry
	# bin_kernel : ${OBJCOPY} -O binary ${elf_kernel} ${BIN}
	@echo
	@echo Run \"make -f accmlib/accmrv32.make\" to build accm-r32

$(uobjpath)/sapp-$(arch)/loop_echo: subapps/_basic/loop_echo.cpp
	$(MKDIR) $(uobjpath)/sapp-$(arch)
	echo MK subapps/_basic/loop_echo.cpp
	@${CX} ${XFLAGS} $< prehost/_auxiliary.cpp -o $(uobjpath)/sapp-$(arch)/loop_echo  \
		-L$(uobjpath)/accm-riscv32 -lriscv32 -T accmlib/accmrv.ld
$(archdir)/kerdisk.fat: $(uobjpath)/sapp-$(arch)/loop_echo
	$(MKDIR) $(archdir)
	dd if=/dev/zero of=$@ bs=1M count=1
	mkfs.fat -n 'MECOCOA2' -s 2 -f 2 -R 32 -F 32 $@
	@echo $(sudokey) | sudo -S mkdir -p $(mntdir)
	@echo $(sudokey) | sudo -S mount -o loop $(archdir)/kerdisk.fat $(mntdir)
	@echo $(sudokey) | sudo -S cp $(uobjpath)/sapp-$(arch)/loop_echo $(mntdir)/lpa.elf
	tree $(mntdir)
	@echo $(sudokey) | sudo -S umount $(mntdir)
prehost/$(arch)/fatvhd.ignore: $(archdir)/kerdisk.fat
	cp $(archdir)/kerdisk.fat prehost/$(arch)/fatvhd.ignore

.PHONY : run run-only debug
run: build
	@echo [ running] MCCA for $(arch)
	@echo "( Press ^A and then X to exit QEMU )"
	${QEMU} ${QFLAGS} -nographic -kernel $(ubinpath)/${elf_kernel}
# 	@${QEMU} -M ? | grep virt >/dev/null || exit
run-only:
	${QEMU} ${QFLAGS} -kernel $(ubinpath)/${elf_kernel}


debug: build
	# sudo lsof -t -i :1234 | xargs sudo kill -9
	@${QEMU} ${QFLAGS} -nographic -kernel $(ubinpath)/${elf_kernel} -s -S &
	@${G_DBG} $(ubinpath)/${elf_kernel} -q -x configs/qemuvirt-rv.gdbinit

.PHONY : clean
clean:
	@echo ---- Mecocoa $(arch) ----#[clearing]
# 	${RM} $(uobjpath)/mcca-$(arch)/*
	${RM} $(uobjpath)/sapp-$(arch)/*
	${RM} $(archdir)/kerdisk.fat
	@${MKDIR} $(uobjpath)/mcca-$(arch)

$(foreach src,$(gasfile),$(eval $(call gas_to_o,$(src))))
$(foreach src,$(cplfile),$(eval $(call c_to_o,$(src))))
$(foreach src,$(cppfile),$(eval $(call cpp_to_o,$(src))))

_ag_%.o:
	echo AS $(notdir $<)
	${CC} ${CFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@ -D_DEV_GNU_AS -D__BITS__=32

_cc_%.o:
	echo CC $(notdir $<)
	${CC} ${CFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

_cx_%.o:
	echo CX $(notdir $<)
	${CX} ${XFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@ \
		-fno-rtti

-include $(dest_obj)/*.d
