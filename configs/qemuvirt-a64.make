# ASCII Makefile TAB4 LF
# Attribute: Ubuntu(64) Shell(Bash) Dest(.){Arch(ARM), BITS(64)}
# AllAuthor: @ArinaMgk (Phina.net)
# ModuTitle: Build for Mecocoa
# Copyright: Dosconio Mecocoa, BCD License Version 3

arch?=qemuvirt-a64

mnts=/mnt/floppy

MKDIR = mkdir -p
RM = rm -rf

# (GNU)
GPREF   = aarch64-none-elf-
CFLAGS += # -z norelro  # hosted compiler option
CFLAGS += -nostdlib -fno-builtin
CFLAGS += --static #-mno-red-zone -mcpu=max
CFLAGS += -I$(uincpath) -D_MCCA=0x2064 -D_HIS_IMPLEMENT -D_DEBUG
CFLAGS += -fno-strict-aliasing -fno-exceptions -fno-stack-protector
CFLAGS += -Wextra -Wno-multichar
XFLAGS  = $(CFLAGS) -fno-rtti -fno-use-cxa-atexit
G_DBG   = gdb-multiarch
CC      = ${GPREF}gcc -O2
CX      = ${GPREF}g++ -O2 -std=c++2a
OBJCOPY = ${GPREF}objcopy
OBJDUMP = ${GPREF}objdump

# (QEMU)
QARCH  = aarch64
QEMU   = qemu-system-$(QARCH)
QBOARD = virt

#
LDFILE  = prehost/$(arch)/$(arch).ld
LDFLAGS = -T $(LDFILE) 

asmpref=_as_
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
asmfile=prehost/$(arch)/$(arch).S\


cppfile=


cplfile=



asmobjs=$(addprefix $(dest_obj)/$(asmpref),$(patsubst %S,%o,$(notdir $(asmfile))))
cppobjs=$(addprefix $(dest_obj)/$(cpppref),$(patsubst %cpp,%o,$(notdir $(cppfile))))
cplobjs=$(addprefix $(dest_obj)/$(cplpref),$(patsubst %c,%o,$(notdir $(cplfile))))
elf_kernel=ARM/mecocoa/mcca-$(arch).elf

mntdir=/mnt/mcca-$(arch)
sudokey=k

.PHONY : build
build: clean $(asmobjs) $(cppobjs) $(cplobjs)
	@echo MK $(elf_kernel)
	$(CX) $(XFLAGS) $(LDFLAGS) \
		-o $(ubinpath)/$(elf_kernel) \
		-Wl,-Map=$(ubinpath)/$(elf_kernel).map \
		prehost/$(arch)/$(arch).cpp \
		$(uobjpath)/mcca-$(arch)/*.o
#

iden=$(arch).img
outs=$(ubinpath)/ARM/mecocoa/$(iden)
.PHONY : run run-only
run: build run-only
run-only:
	$(QEMU) \
		-M $(QBOARD) \
		-cpu max \
		-m 64M \
		-kernel $(ubinpath)/$(elf_kernel) \
		-nographic
# 		-append "console=ttyAMA0" \
# 		-s -S

.PHONY : clean
clean:
	@echo ---- Mecocoa $(arch) ----#[clearing]
# 	${RM} $(uobjpath)/mcca-$(arch)/*
# 	${RM} $(ubinpath)/$(arch).img
	@${MKDIR} $(uobjpath)/mcca-$(arch)


$(foreach src,$(asmfile),$(eval $(call asm_to_o,$(src))))
$(foreach src,$(cplfile),$(eval $(call c_to_o,$(src))))
$(foreach src,$(cppfile),$(eval $(call cpp_to_o,$(src))))

_as_%.o:
	echo AS $(notdir $<)
	${CC}           -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@)  -D_MCCA=0x2064

_cc_%.o:
	echo CC $(notdir $<)
	${CC} ${CFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

_cx_%.o:
	echo CX $(notdir $<)
	${CX} ${XFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

-include $(dest_obj)/*.d
