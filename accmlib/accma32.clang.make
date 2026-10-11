AASM = aasm # a/n/yasm
arch=a32

# ARM32 bare-metal target for the Mecocoa NoMMU port
CC = clang --target=armv7em-none-eabihf
CX = clang++ --target=armv7em-none-eabihf
AR = llvm-ar
# the clang driver drops -shared for this bare-metal target, so link the .so directly
LD = ld.lld

asmpref=_ag_
asmfile=$(wildcard accmlib/arch/arm32/*.S)

cplpref=_cc_
cplfile=$(wildcard $(ulibpath)/c/*.c) $(wildcard $(ulibpath)/c/**/*.c) $(wildcard $(ulibpath)/c/**/**/*.c) $(wildcard accmlib/*.c)

cpppref=_cx_
cppfile=$(wildcard $(ulibpath)/cpp/*.cpp) \
	$(wildcard $(ulibpath)/cpp/**/*.cpp) \
	$(wildcard $(ulibpath)/cpp/**/**/*.cpp) \
	$(wildcard $(ulibpath)/cpp/**/**/**/*.cpp) \
	$(wildcard accmlib/*.cpp) prehost/_auxiliary.cpp

dest_obj=$(uobjpath)/accm-$(arch)
# ARMv7E-M (DSP) + double-precision FPv5 (D16) + hard-float ABI
SYSROOT_TRIPLE=armv7em-mcca-eabihf
SYSROOT_USR_LIB=accmlib/sysroot/usr/lib/$(SYSROOT_TRIPLE)
SYSROOT_CRT0=$(SYSROOT_USR_LIB)/crt0.o
SYSROOT_LIBC=$(SYSROOT_USR_LIB)/libc.a
SYSROOT_LIBC_PIC=$(SYSROOT_USR_LIB)/libc-pic.a

COMWAN = -Wall -Wno-unused-variable -Wno-unused-function -Wno-parentheses -Wno-incompatible-library-redeclaration -Wno-invalid-constexpr -Wno-empty-body -Wno-unknown-warning-option
COMFLG = -O2 -fno-builtin -nostdlib -nostdinc++ -fno-stack-protector -fno-strict-aliasing $(COMWAN)
COMFLG += -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16
COMFLG += -ffunction-sections -fdata-sections
# the POSIX API layer names id_t/pid_t/uid_t as macros, so the C library must not typedef them
attr = -I$(uincpath) -I$(uincpath)/c/ISO_IEC_STD -I$(uincpath)/c/API-POSIX -Iaccmlib/sysroot/usr/include -D_ACCM=0x2032 -D_OPT_ARM32 -D_DEBUG
attr += -D_ID_T_DECLARED -D_PID_T_DECLARED -D_UID_T_DECLARED

CFLAGS_STA = $(COMFLG) $(attr)
XFLAGS_STA = $(CFLAGS_STA) -fno-exceptions -fno-rtti -fno-use-cxa-atexit

CFLAGS_PIC = $(COMFLG) -fPIC $(attr)
XFLAGS_PIC = $(CFLAGS_PIC) -fno-exceptions -fno-rtti -fno-use-cxa-atexit

define gas_to_o
$(dest_obj)/$(asmpref)$(notdir $(1:.S=.o)): $(1)
$(dest_obj)/$(asmpref)$(notdir $(1:.S=.pic.o)): $(1)
endef
define c_to_o
$(dest_obj)/$(cplpref)$(notdir $(1:.c=.o)): $(1)
$(dest_obj)/$(cplpref)$(notdir $(1:.c=.pic.o)): $(1)
endef
define cpp_to_o
$(dest_obj)/$(cpppref)$(notdir $(1:.cpp=.o)): $(1)
$(dest_obj)/$(cpppref)$(notdir $(1:.cpp=.pic.o)): $(1)
endef

asmobjs=$(addprefix $(dest_obj)/$(asmpref),$(patsubst %S,%o,$(notdir $(asmfile))))
cppobjs=$(addprefix $(dest_obj)/$(cpppref),$(patsubst %cpp,%o,$(notdir $(cppfile))))
cplobjs=$(addprefix $(dest_obj)/$(cplpref),$(patsubst %c,%o,$(notdir $(cplfile))))

asmobjs_pic=$(asmobjs:.o=.pic.o)
cppobjs_pic=$(cppobjs:.o=.pic.o)
cplobjs_pic=$(cplobjs:.o=.pic.o)

.PHONY: all clean
all: ${dest_obj}/lib$(arch).so $(SYSROOT_CRT0) $(SYSROOT_LIBC) $(SYSROOT_LIBC_PIC)

$(dest_obj):
	mkdir -p $@

$(SYSROOT_USR_LIB):
	mkdir -p $@

$(asmobjs) $(cppobjs) $(cplobjs) $(asmobjs_pic) $(cppobjs_pic) $(cplobjs_pic): | $(dest_obj)

${dest_obj}/lib$(arch).a: $(asmobjs) $(cppobjs) $(cplobjs)
	@-rm -f $@
	@echo "AR $(notdir $@)"
	@${AR} -rcs $@ $^

${dest_obj}/lib$(arch)-pi.a: $(asmobjs_pic) $(cppobjs_pic) $(cplobjs_pic)
	@-rm -f $@
	@echo "AR $(notdir $@)"
	@${AR} -rcs $@ $^

${dest_obj}/lib$(arch).so: $(asmobjs_pic) $(cppobjs_pic) $(cplobjs_pic)
	@-rm -f $@
	@echo "LD $(notdir $@)"
	@${LD} -shared -o $@ $^

# Publish under the sysroot triplet so every consumer links one canonical location;
# the x86 triplet keeps the same crt0.o/libc.a/libc-pic.a names.
$(SYSROOT_CRT0): ${dest_obj}/$(asmpref)others-a.pic.o | $(SYSROOT_USR_LIB)
	@echo "CP $(notdir $@)"
	@cp $< $@

$(SYSROOT_LIBC): ${dest_obj}/lib$(arch).a | $(SYSROOT_USR_LIB)
	@echo "CP $(notdir $@)"
	@cp $< $@

$(SYSROOT_LIBC_PIC): ${dest_obj}/lib$(arch)-pi.a | $(SYSROOT_USR_LIB)
	@echo "CP $(notdir $@)"
	@cp $< $@

$(foreach src,$(asmfile),$(eval $(call gas_to_o,$(src))))
$(foreach src,$(cplfile),$(eval $(call c_to_o,$(src))))
$(foreach src,$(cppfile),$(eval $(call cpp_to_o,$(src))))

clean:
	@-rm -rf $(dest_obj)

_ag_%.o:
	@echo AS $(notdir $<)
	@${CC} -c ${CFLAGS_STA} -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

_ag_%.pic.o:
	@echo AS $(notdir $<) DYN
	@${CC} -c ${CFLAGS_PIC} -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

_cc_%.o:
	@echo CC $(notdir $<)
	@${CC} -c ${CFLAGS_STA} -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

_cc_%.pic.o:
	@echo CC $(notdir $<) DYN
	@${CC} -c ${CFLAGS_PIC} -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

_cx_%.o:
	@echo CX $(notdir $<)
	@${CX} -c ${XFLAGS_STA} -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

_cx_%.pic.o:
	@echo CX $(notdir $<) DYN
	@${CX} -c ${XFLAGS_PIC} -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

-include $(dest_obj)/*.d
