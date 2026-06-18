AASM = aasm # a/n/yasm
override arch=x86

# LLVM compiler and target setup
CC = clang -target i686-unknown-none-elf
CX = clang++ -target i686-unknown-none-elf
AR = llvm-ar
LD = ld.lld -m elf_i386

attr = -D_DEBUG -D_ACCM=0x8632 -I$(uincpath) -I$(uincpath)/c/ISO_IEC_STD -I$(uincpath)/c/API-POSIX -Iaccmlib/sysroot/usr/include

asmpref=_ae_
CRT0_SRC=accmlib/arch/x86/crt0.asm
CRT0_OBJ=$(dest_obj)/crt0.o
SYSROOT_TRIPLE=i686-mcca
SYSROOT_USR_LIB=accmlib/sysroot/usr/lib/$(SYSROOT_TRIPLE)
SYSROOT_CRT0=$(SYSROOT_USR_LIB)/crt0.o
SYSROOT_LIBC=$(SYSROOT_USR_LIB)/libc.a
asmfile=$(filter-out $(CRT0_SRC),$(wildcard $(ulibpath)/asm/x86/*.asm) $(wildcard $(ulibpath)/asm/x86/**/*.asm) $(wildcard accmlib/arch/x86/*.asm))

cplpref=_cc_
cplfile=$(wildcard $(ulibpath)/c/*.c) $(wildcard $(ulibpath)/c/**/*.c) $(wildcard $(ulibpath)/c/**/**/*.c) $(wildcard accmlib/*.c)

cpppref=_cx_
cppfile=$(wildcard $(ulibpath)/cpp/*.cpp) \
	$(wildcard $(ulibpath)/cpp/**/*.cpp) \
	$(wildcard $(ulibpath)/cpp/**/**/*.cpp) \
	$(wildcard $(ulibpath)/cpp/**/**/**/*.cpp) \
	$(wildcard accmlib/*.cpp) prehost/_auxiliary.cpp

dest_obj=$(uobjpath)/accm-$(arch)
COMWAN = -Wno-incompatible-library-redeclaration -Wno-invalid-constexpr -Wno-empty-body
COMFLG = -m32 -static -fno-builtin -nostdlib -fno-stack-protector -O2 -fno-strict-aliasing $(COMWAN)
CFLAGS=$(COMFLG) $(attr)
XFLAGS=$(CFLAGS) -std=c++2a -fno-exceptions -fno-unwind-tables -fno-rtti -Wno-volatile

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
all: $(SYSROOT_CRT0) $(SYSROOT_LIBC)

$(dest_obj):
	mkdir -p $@

$(CRT0_OBJ) $(asmobjs) $(cplobjs) $(cppobjs): | $(dest_obj)

$(SYSROOT_USR_LIB):
	mkdir -p $@

$(CRT0_OBJ): $(CRT0_SRC)
	@echo AS $(notdir $<)
	@aasm -f elf -o $@ $< -MD $(patsubst %.o,%.d,$@)

$(SYSROOT_CRT0): $(CRT0_OBJ) | $(SYSROOT_USR_LIB)
	@echo "CP $(notdir $@)"
	@cp $(CRT0_OBJ) $@

${dest_obj}/lib$(arch).a: $(asmobjs) $(cplobjs) $(cppobjs)
	@-rm -f $@
	@echo "AR $(notdir $@)"
	@${AR} -rcs $@ $^

$(SYSROOT_LIBC): ${dest_obj}/lib$(arch).a | $(SYSROOT_USR_LIB)
	@echo "CP $(notdir $@)"
	@cp $< $@


$(foreach src,$(asmfile),$(eval $(call asm_to_o,$(src))))
$(foreach src,$(gasfile),$(eval $(call gas_to_o,$(src))))
$(foreach src,$(cplfile),$(eval $(call c_to_o,$(src))))
$(foreach src,$(cppfile),$(eval $(call cpp_to_o,$(src))))

# do in linux, or output win-format


_ae_%.o:
	@echo AS $(notdir $<)
	@aasm -f elf        -o $@ $< -MD $(patsubst %.o,%.d,$@)

_cc_%.o:
	@echo CC $(notdir $<)
	@${CC} ${CFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

_cx_%.o:
	@echo CX $(notdir $<)
	@${CX} ${XFLAGS} -c -o $@ $< -MMD -MF $(patsubst %.o,%.d,$@) -MT $@

-include $(dest_obj)/*.d
