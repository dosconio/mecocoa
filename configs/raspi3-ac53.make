# ASCII Makefile TAB4 LF
# Attribute: Shell(Bash) Dest(raspi3-ac53){Arch(ARM), BITS(64)}
# AllAuthor: @ArinaMgk (Phina.net)
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3
# Dependens: arm/aarch64-none-eabi-newlib

MKDIR = mkdir -p
RM = rm -rf

# (GNU)
GPREF   = aarch64-none-elf-
CFLAGS += -nostdlib -fno-builtin -Wall -Wno-unused-variable -Wno-unused-function -Wno-parentheses
CFLAGS += -I$(uincpath) -D_MCCA=0x10532064 -D_OPT_ARM64 -D_DEBUG
CFLAGS += -fno-strict-aliasing -fno-exceptions -fno-stack-protector
CFLAGS += -mcpu=cortex-a53 -mabi=lp64
CFLAGS += -g
XFLAGS  = $(CFLAGS) -fno-rtti -fno-use-cxa-atexit
G_DBG   = gdb-multiarch
CC      = ${GPREF}gcc -O2
CX      = ${GPREF}g++ -O2
OBJCOPY = ${GPREF}objcopy
OBJDUMP = ${GPREF}objdump

# (QEMU)
CORES  = 4
QARCH  = aarch64
QEMU   = qemu-system-$(QARCH)
QBOARD = raspi3b
QFLAGS = -smp $(CORES) -M $(QBOARD)

#
LDFILE  = prehost/$(arch)/$(arch).ld
LDFLAGS = -T $(LDFILE)

#
asmpref=_ag_
cplpref=_c_
cpppref=_x_
unicplpref=_cc_
unicpppref=_cx_
# Keep source-to-object mapping identical for prerequisites and linker inputs.
c_object=$(dest_obj)/$(if $(filter $(abspath $(ulibpath))/%,$(abspath $(1))),$(unicplpref),$(cplpref))$(notdir $(1:.c=.o))
cpp_object=$(dest_obj)/$(if $(filter $(abspath $(ulibpath))/%,$(abspath $(1))),$(unicpppref),$(cpppref))$(notdir $(1:.cpp=.o))
dest_obj=$(uobjpath)/mcca-$(arch)
define asm_to_o
$(dest_obj)/$(asmpref)$(notdir $(1:.S=.o)): $(1)
endef
define c_to_o
$(call c_object,$(1)): $(1) configs/raspi3-ac53.make
endef
define cpp_to_o
$(call cpp_object,$(1)): $(1) configs/raspi3-ac53.make
endef

#
asmfile=$(wildcard prehost/$(arch)/*.S)

cppfile=$(wildcard prehost/$(arch)/*.cpp) \
	$(ulibpath)/cpp/lango/lango-cpp.cpp \
#$(wildcard mecocoa/*.cpp) 

cplfile=# $(ulibpath)/c/mcore.c \


asmobjs=$(addprefix $(dest_obj)/$(asmpref),$(patsubst %S,%o,$(notdir $(asmfile))))
cppobjs=$(sort $(foreach src,$(cppfile),$(call cpp_object,$(src))))
cplobjs=$(sort $(foreach src,$(cplfile),$(call c_object,$(src))))

elf_kernel=mcca-$(arch).elf

.PHONY : build
build: clean $(asmobjs) $(cppobjs) $(cplobjs)
	#echo [building] MCCA for $(arch)
	@echo MK $(elf_kernel)
	@${CC} ${XFLAGS} ${LDFLAGS} -o $(ubinpath)/${elf_kernel} $(uobjpath)/mcca-$(arch)/*.o \
		-Wl,-Map=$(ubinpath)/$(elf_kernel).map

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
	${CC} ${CFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@ -D_DEV_GNU_AS -D__BITS__=64

_c_%.o:
	echo CC $(notdir $<)
	${CC} ${CFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

_cc_%.o:
	echo CC $(notdir $<)
	${CC} ${CFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

_x_%.o:
	echo CX $(notdir $<)
	${CX} ${XFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

_cx_%.o:
	echo CX $(notdir $<)
	${CX} ${XFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

-include $(wildcard $(asmobjs:.o=.d) $(gasobjs:.o=.d) $(cppobjs:.o=.d) $(cplobjs:.o=.d))
