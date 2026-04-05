# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64) Shell(Bash) Dest(qemuvirt-r64){Arch(RISCV), BITS(64)}
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
CFLAGS += -nostdlib -fno-builtin  -Wall -Wno-unused-variable -Wno-unused-function -Wno-parentheses # -g
CFLAGS += -march=rv64g -mabi=lp64 -mcmodel=medany
CFLAGS += -I$(uincpath) -D_MCCA=0x1064 -D_OPT_RISCV64 -D_DEBUG
CFLAGS += -fno-strict-aliasing -fno-exceptions -fno-stack-protector
XFLAGS  = $(CFLAGS) -fno-rtti -fno-use-cxa-atexit
G_DBG   = gdb-multiarch
CC      = ${GPREF}gcc -O2
CX      = ${GPREF}g++ -O2
OBJCOPY = ${GPREF}objcopy
OBJDUMP = ${GPREF}objdump

# (QEMU)
QARCH  = riscv64
QEMU   = qemu-system-$(QARCH)
QBOARD = virt
QFLAGS = -smp 1 -machine $(QBOARD) -bios none

#
LDFILE  = prehost/$(arch)/$(arch).ld
LDFLAGS = -T $(LDFILE).ignore

#
asmpref=_ag_
cplpref=_cc_
cpppref=_cx_
dest_obj=$(uobjpath)/mcca-$(arch)
define asm_to_o
$(dest_obj)/$(asmpref)$(notdir $(1:.S=.o)): $(1)
endef
define c_to_o
$(dest_obj)/$(cplpref)$(notdir $(1:.c=.o)): $(1)
endef
define cpp_to_o
$(dest_obj)/$(cpppref)$(notdir $(1:.cpp=.o)): $(1)
endef

#
asmfile=$(wildcard prehost/qemuvirt-r32/*.S)
cppfile=$(wildcard prehost/qemuvirt-r32/*.cpp) $(wildcard mecocoa/*.cpp) prehost/_auxiliary.cpp \
	$(ulibpath)/cpp/sort.cpp \
	$(ulibpath)/cpp/consio.cpp \
	$(ulibpath)/cpp/stream.cpp \
	$(ulibpath)/cpp/string.cpp \
	$(ulibpath)/cpp/interrupt.cpp \
	$(ulibpath)/cpp/Device/PLIC.cpp \
	$(ulibpath)/cpp/Device/UART.cpp \
	$(ulibpath)/cpp/Device/Timer.cpp \
	$(ulibpath)/cpp/system/paging.cpp \
	$(ulibpath)/cpp/Device/Storage.cpp \
	$(ulibpath)/cpp/lango/lango-cpp.cpp \
	$(ulibpath)/cpp/grp-base/bstring.cpp \
	$(ulibpath)/cpp/dat-block/mempool.cpp \
	$(ulibpath)/cpp/dat-block/bmmemoman.cpp \
	$(ulibpath)/cpp/nodes/dnode.cpp $(wildcard $(ulibpath)/cpp/nodes/dnode/*.cpp) \
	$(ulibpath)/cpp/filesystem/FAT.cpp $(wildcard $(ulibpath)/cpp/filesystem/FAT/*.cpp) \

cplfile=$(ulibpath)/c/mcore.c \
	$(ulibpath)/c/debug.c \
	$(ulibpath)/c/auxiliary/toxxxer.c \
	$(wildcard $(ulibpath)/c/dnode/*.c) \
	$(ulibpath)/c/ustring/astring/salc.c \
	$(ulibpath)/c/ustring/astring/StrHeap.c \


asmobjs=$(addprefix $(dest_obj)/$(asmpref),$(patsubst %S,%o,$(notdir $(asmfile))))
cppobjs=$(addprefix $(dest_obj)/$(cpppref),$(patsubst %cpp,%o,$(notdir $(cppfile))))
cplobjs=$(addprefix $(dest_obj)/$(cplpref),$(patsubst %c,%o,$(notdir $(cplfile))))

elf_kernel=mcca-$(arch).elf

archdir=$(ubinpath)/RISCV64/mecocoa
mntdir=/mnt/floppy
clang=clang-14
sudokey=k

.PHONY : build
build: clean prehost/$(arch)/fatvhd.ignore $(asmobjs) $(cppobjs) $(cplobjs)
	#echo [building] MCCA for $(arch)
	@echo MK $(elf_kernel)
	@perl configs/qemuvirt-riscv.pl r64 > $(LDFILE).ignore
	@${CC} -E -P -x c ${CFLAGS} $(LDFILE).ignore -D_DEV_GNU_AS -D__BITS__=64 > $(LDFILE)
	@mv $(LDFILE) $(LDFILE).ignore
	@${CC} ${XFLAGS} ${LDFLAGS} -o $(ubinpath)/${elf_kernel} $(uobjpath)/mcca-$(arch)/*.o
	# readelf -h $ubinpath/mcca-qemuvirt-r64.elf| grep Entry
	# bin_kernel : ${OBJCOPY} -O binary ${elf_kernel} ${BIN}
	@echo
	@echo Run \"make -f accmlib/accmrv64.make\" to build accm-r64

$(uobjpath)/sapp-$(arch)/loop_print_a: subapps/_basic/loop_print_a.cpp
	$(MKDIR) $(uobjpath)/sapp-$(arch)
	echo MK $<
	@${CX} ${XFLAGS} $< -o $(uobjpath)/sapp-$(arch)/loop_print_a \
		-L$(uobjpath)/accm-riscv64 -lriscv64 -T accmlib/accmrv.ld
$(archdir)/kerdisk.fat: $(uobjpath)/sapp-$(arch)/loop_print_a
	$(MKDIR) $(archdir)
	dd if=/dev/zero of=$@ bs=1M count=1
	mkfs.fat -n 'MECOCOA2' -s 2 -f 2 -R 32 -F 32 $@
	@echo $(sudokey) | sudo -S mkdir -p $(mntdir)
	@echo $(sudokey) | sudo -S mount -o loop $(archdir)/kerdisk.fat $(mntdir)
	@echo $(sudokey) | sudo -S cp $(uobjpath)/sapp-$(arch)/loop_print_a $(mntdir)/lpa.elf
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
	${RM} $(archdir)/kerdisk.fat
	@${MKDIR} $(uobjpath)/mcca-$(arch)

$(foreach src,$(asmfile),$(eval $(call asm_to_o,$(src))))
$(foreach src,$(cplfile),$(eval $(call c_to_o,$(src))))
$(foreach src,$(cppfile),$(eval $(call cpp_to_o,$(src))))

_ag_%.o:
	echo AS $(notdir $<)
	${CC} ${CFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@ -D_DEV_GNU_AS -D__BITS__=64

_cc_%.o:
	echo CC $(notdir $<)
	${CC} ${CFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

_cx_%.o:
	echo CX $(notdir $<)
	${CX} ${XFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@ \
		-fno-rtti

-include $(dest_obj)/*.d



