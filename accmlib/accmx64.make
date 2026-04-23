AASM = aasm # a/n/yasm
arch=atx-x64
attr = -D_DEBUG -D_ACCM=0x8664 -I$(uincpath)

asmpref=_ae_
asmfile=$(wildcard $(ulibpath)/asm/x64/*.asm) $(wildcard $(ulibpath)/asm/x64/**/*.asm) $(wildcard accmlib/x64/*.asm)
asmobjs=$(patsubst %asm, %o, $(asmfile))

cplpref=_cc_
cplfile=$(wildcard $(ulibpath)/c/*.c) $(wildcard accmlib/*.c)
cplobjs=$(patsubst %c, %o, $(cplfile))

cpppref=_cx_
cppfile=$(wildcard $(ulibpath)/cpp/*.cpp) $(wildcard accmlib/*.cpp) prehost/_auxiliary.cpp
cppobjs=$(patsubst %cpp, %o, $(cppfile))

dest_obj=$(uobjpath)/accm-$(arch)
COMWAN = -Wno-builtin-declaration-mismatch
COMFLG = -m64 -static -fno-builtin -nostdlib -fno-stack-protector  -O3 $(COMWAN)
CC=gcc $(COMFLG)
CX=g++ $(COMFLG) -std=c++2a -fno-exceptions  -fno-unwind-tables -fno-rtti -Wno-volatile
LD=ld -m elf_x64

# do in linux, or output win-format

.PHONY: all delall
all: delall $(asmobjs) $(cplobjs) $(cppobjs)
	@${AR} -rcs ${dest_obj}/lib$(arch).a ${dest_obj}/*.o

delall:
	mkdir $(uobjpath)/accm-$(arch) -p
	@-rm ${dest_obj}/lib$(arch).a
	@-rm -rf $(uobjpath)/accm-$(arch)/*

%.o: %.asm
	@echo "AS $(<)"
	@${AASM} -felf64 ${attr} $(<) -o $(dest_obj)/$(asmpref)$(notdir $(@))

%.o: %.c
	@echo "CC $(<)"
	@$(CC) $(attr) -c $< -o $(dest_obj)/$(cplpref)$(notdir $@) ||\
		ret 1 "!! Panic When: $(CC) $(attr) -c $< -o $(dest_obj)/$(cplpref)$(notdir $@)"
%.o: %.cpp
	@echo "CX $(<)"
	@$(CX) $(attr) -c $< -o $(dest_obj)/$(cpppref)$(notdir $@) ||\
		ret 1 "!! Panic When: $(CX) $(attr) -c $< -o $(dest_obj)/$(cplpref)$(notdir $@)"

