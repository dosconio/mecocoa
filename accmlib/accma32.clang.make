AASM = aasm # a/n/yasm
arch=a32

# ARM32 bare-metal target for the Mecocoa NoMMU port
CC = arm-none-eabi-gcc -O2
CX = arm-none-eabi-g++ -O2
AR = arm-none-eabi-ar

asmpref=_ag_
asmfile=$(wildcard accmlib/arch/arm32/*.S)

cplpref=_cc_
cplfile=

cpppref=_cx_
cppfile=$(wildcard accmlib/lib.cpp)

dest_obj=$(uobjpath)/accm-$(arch)

# minimal source set for now: the shared user sources under $(ulibpath) are not ARM-clean yet
COMWAN = -Wall -Wno-unused-variable -Wno-unused-function -Wno-parentheses
COMFLG = -fno-builtin -nostdlib -fno-stack-protector -fno-strict-aliasing $(COMWAN)
COMFLG += -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16
COMFLG += -ffunction-sections -fdata-sections
attr = -I$(uincpath) -Iaccmlib/sysroot/usr/include -D_ACCM=0x2032 -D_DEBUG

CFLAGS_STA = $(COMFLG) $(attr)
XFLAGS_STA = $(CFLAGS_STA) -fno-exceptions -fno-rtti -fno-use-cxa-atexit

CFLAGS_PIC = $(COMFLG) -fPIC $(attr)
XFLAGS_PIC = $(CFLAGS_PIC) -fno-exceptions -fno-rtti -fno-use-cxa-atexit

define gas_to_o
$(dest_obj)/$(asmpref)$(notdir $(1:.S=.o)): $(1)
$(dest_obj)/$(asmpref)$(notdir $(1:.S=.pic.o)): $(1)
endef
define cpp_to_o
$(dest_obj)/$(cpppref)$(notdir $(1:.cpp=.o)): $(1)
$(dest_obj)/$(cpppref)$(notdir $(1:.cpp=.pic.o)): $(1)
endef

asmobjs=$(addprefix $(dest_obj)/$(asmpref),$(patsubst %S,%o,$(notdir $(asmfile))))
cppobjs=$(addprefix $(dest_obj)/$(cpppref),$(patsubst %cpp,%o,$(notdir $(cppfile))))

asmobjs_pic=$(asmobjs:.o=.pic.o)
cppobjs_pic=$(cppobjs:.o=.pic.o)

.PHONY: all clean
all: ${dest_obj}/lib$(arch).a ${dest_obj}/lib$(arch)-pi.a ${dest_obj}/lib$(arch).so

$(dest_obj):
	mkdir -p $@

$(asmobjs) $(cppobjs) $(asmobjs_pic) $(cppobjs_pic): | $(dest_obj)

${dest_obj}/lib$(arch).a: $(asmobjs) $(cppobjs)
	@-rm -f $@
	@echo "AR $(notdir $@)"
	@${AR} -rcs $@ $^

${dest_obj}/lib$(arch)-pi.a: $(asmobjs_pic) $(cppobjs_pic)
	@-rm -f $@
	@echo "AR $(notdir $@)"
	@${AR} -rcs $@ $^

${dest_obj}/lib$(arch).so: $(asmobjs_pic) $(cppobjs_pic)
	@-rm -f $@
	@echo "LD $(notdir $@)"
	@${CC} -shared -nostdlib -o $@ $^

$(foreach src,$(asmfile),$(eval $(call gas_to_o,$(src))))
$(foreach src,$(cppfile),$(eval $(call cpp_to_o,$(src))))

clean:
	@-rm -rf $(dest_obj)

_ag_%.o:
	@echo AS $(notdir $<)
	@${CC} -c ${CFLAGS_STA} -o $@ $<

_ag_%.pic.o:
	@echo AS $(notdir $<) DYN
	@${CC} -c ${CFLAGS_PIC} -o $@ $<

_cx_%.o:
	@echo CX $(notdir $<)
	@${CX} -c ${XFLAGS_STA} -o $@ $<

_cx_%.pic.o:
	@echo CX $(notdir $<) DYN
	@${CX} -c ${XFLAGS_PIC} -o $@ $<
