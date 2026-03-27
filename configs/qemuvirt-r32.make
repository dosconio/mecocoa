# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64) Shell(Bash) Dest(qemuvirt-r32){Arch(RISCV), BITS(32)}
# AllAuthor: @ArinaMgk (Phina.net)
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3

MKDIR = mkdir -p
RM = rm -rf

# (GNU)
GPREF   = riscv64-unknown-elf-
CFLAGS += -nostdlib -fno-builtin -Wall -Wno-unused-variable -Wno-unused-function -Wno-parentheses
CFLAGS += -march=rv32g -mabi=ilp32
CFLAGS += -I$(uincpath) -D_MCCA=0x1032 -D_OPT_RISCV32 -D_HIS_IMPLEMENT -D_DEBUG
CFLAGS += -fno-strict-aliasing -fno-exceptions -fno-stack-protector
CFLAGS += -g
XFLAGS  = $(CFLAGS) -fno-rtti -fno-use-cxa-atexit
G_DBG   = gdb-multiarch
CC      = ${GPREF}gcc -O2
CX      = ${GPREF}g++ -O2
OBJCOPY = ${GPREF}objcopy
OBJDUMP = ${GPREF}objdump

# (QEMU)
CORES  = 1
QARCH  = riscv32
QEMU   = qemu-system-$(QARCH)
QBOARD = virt
QFLAGS = -nographic -smp $(CORES) -machine $(QBOARD) -bios none

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
asmfile=$(wildcard prehost/$(arch)/*.S)

cppfile=$(wildcard prehost/$(arch)/*.cpp) $(wildcard mecocoa/*.cpp) \
	$(ulibpath)/cpp/sort.cpp \
	$(ulibpath)/cpp/consio.cpp \
	$(ulibpath)/cpp/stream.cpp \
	$(ulibpath)/cpp/interrupt.cpp \
	$(ulibpath)/cpp/Device/PLIC.cpp \
	$(ulibpath)/cpp/Device/UART.cpp \
	$(ulibpath)/cpp/Device/Timer.cpp \
	$(ulibpath)/cpp/system/paging.cpp \
	$(ulibpath)/cpp/lango/lango-cpp.cpp \
	$(ulibpath)/cpp/dat-block/mempool.cpp \
	$(ulibpath)/cpp/dat-block/bmmemoman.cpp \
	$(ulibpath)/cpp/nodes/dnode.cpp $(wildcard $(ulibpath)/cpp/nodes/dnode/*.cpp) \

cplfile=$(ulibpath)/c/mcore.c \
	$(ulibpath)/c/debug.c \
	$(wildcard $(ulibpath)/c/dnode/*.c) \
	$(ulibpath)/c/ustring/astring/salc.c \
	$(ulibpath)/c/ustring/astring/StrHeap.c \

asmobjs=$(addprefix $(dest_obj)/$(asmpref),$(patsubst %S,%o,$(notdir $(asmfile))))
cppobjs=$(addprefix $(dest_obj)/$(cpppref),$(patsubst %cpp,%o,$(notdir $(cppfile))))
cplobjs=$(addprefix $(dest_obj)/$(cplpref),$(patsubst %c,%o,$(notdir $(cplfile))))

elf_kernel=mcca-$(arch).elf

.PHONY : build
build: clean $(asmobjs) $(cppobjs) $(cplobjs)
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

.PHONY : run
run: build
	@echo [ running] MCCA for $(arch)
	@echo "( Press ^A and then X to exit QEMU )"
	${QEMU} ${QFLAGS} -kernel $(ubinpath)/${elf_kernel}
# 	@${QEMU} -M ? | grep virt >/dev/null || exit


debug: build
	# sudo lsof -t -i :1234 | xargs sudo kill -9
	@${QEMU} ${QFLAGS} -kernel $(ubinpath)/${elf_kernel} -s -S &
	@${G_DBG} $(ubinpath)/${elf_kernel} -q -x configs/qemuvirt-rv.gdbinit

.PHONY : clean
clean:
	@echo ---- Mecocoa $(arch) ----#[clearing]
# 	${RM} $(uobjpath)/mcca-$(arch)/*
	@${MKDIR} $(uobjpath)/mcca-$(arch)

$(foreach src,$(asmfile),$(eval $(call asm_to_o,$(src))))
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
