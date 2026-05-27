AASM = aasm # a/n/yasm
override arch=x64
attr = -D_DEBUG -D_ACCM=0x8664 -I$(uincpath)  -I$(uincpath)/c/API-POSIX

asmpref=_ae_
asmfile=$(wildcard $(ulibpath)/asm/x64/*.asm) $(wildcard $(ulibpath)/asm/x64/**/*.asm) $(wildcard accmlib/x64/*.asm)

cplpref=_cc_
cplfile=$(wildcard $(ulibpath)/c/*.c) $(wildcard $(ulibpath)/c/**/*.c) $(wildcard $(ulibpath)/c/**/**/*.c) $(wildcard accmlib/*.c)

cpppref=_cx_
cppfile=$(wildcard $(ulibpath)/cpp/*.cpp) $(wildcard $(ulibpath)/cpp/dat-block/*.cpp) $(wildcard accmlib/*.cpp) prehost/_auxiliary.cpp

dest_obj=$(uobjpath)/accm-$(arch)
COMWAN = -Wno-builtin-declaration-mismatch
COMFLG = -m64 -static -fno-builtin -nostdlib -fno-stack-protector  -O2 $(COMWAN)
CC=gcc 
CFLAGS=$(COMFLG) $(attr)
CX=g++ 
XFLAGS=$(CFLAGS) -std=c++2a -fno-exceptions  -fno-unwind-tables -fno-rtti -Wno-volatile
LD=ld -m elf_x64

define asm_to_o
$(dest_obj)/$(asmpref)$(notdir $(1:.asm=.o)): $(1)
endef
define gas_to_o
$(dest_obj)/$(gaspref)$(notdir $(1:.S=.o)): $(1)
endef
define c_to_o
$(dest_obj)/$(cplpref)$(notdir $(1:.c=.o)): $(1)
endef
define cpp_to_o
$(dest_obj)/$(cpppref)$(notdir $(1:.cpp=.o)): $(1)
endef

asmobjs=$(addprefix $(dest_obj)/$(asmpref),$(patsubst %asm,%o,$(notdir $(asmfile))))
gasobjs=$(addprefix $(dest_obj)/$(gaspref),$(patsubst %S,%o,$(notdir $(gasfile))))
cppobjs=$(addprefix $(dest_obj)/$(cpppref),$(patsubst %cpp,%o,$(notdir $(cppfile))))
cplobjs=$(addprefix $(dest_obj)/$(cplpref),$(patsubst %c,%o,$(notdir $(cplfile))))

.PHONY: all clean
all: ${dest_obj}/lib$(arch).a

$(dest_obj):
	mkdir -p $@

$(asmobjs) $(cplobjs) $(cppobjs): | $(dest_obj)

${dest_obj}/lib$(arch).a: $(asmobjs) $(cplobjs) $(cppobjs)
	@-rm -f $@
	@echo "AR $(notdir $@)"
	@${AR} -rcs $@ $^


$(foreach src,$(asmfile),$(eval $(call asm_to_o,$(src))))
$(foreach src,$(gasfile),$(eval $(call gas_to_o,$(src))))
$(foreach src,$(cplfile),$(eval $(call c_to_o,$(src))))
$(foreach src,$(cppfile),$(eval $(call cpp_to_o,$(src))))


_ae_%.o:
	@echo AS $(notdir $<)
	@aasm -f elf64      -o $@ $< -MD $(patsubst %.o,%.d,$@)

_cc_%.o:
	@echo CC $(notdir $<)
	@${CC} ${CFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

_cx_%.o:
	@echo CX $(notdir $<)
	@${CX} ${XFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

-include $(dest_obj)/*.d

