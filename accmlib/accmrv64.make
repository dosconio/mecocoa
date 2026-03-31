AASM = aasm # a/n/yasm
arch=riscv64

asmpref=_ag_
asmfile=$(wildcard accmlib/riscv/*.S)
asmobjs=$(patsubst %S, %o, $(asmfile))

cplpref=_cc_
cplfile=$(wildcard $(ulibpath)/c/*.c) $(wildcard accmlib/*.c)
cplobjs=$(patsubst %c, %o, $(cplfile))

cpppref=_cx_
cppfile=$(wildcard $(ulibpath)/cpp/*.cpp) $(wildcard accmlib/*.cpp)
cppobjs=$(patsubst %cpp, %o, $(cppfile))

dest_obj=$(uobjpath)/accm-$(arch)

GPREF   = riscv64-elf-
CFLAGS += -nostdlib -fno-builtin -Wall -Wno-unused-variable -Wno-unused-function -Wno-parentheses
CFLAGS += -march=rv64g -mabi=lp64
CFLAGS += -I$(uincpath) -D_ACCM=0x1064 -D_OPT_RISCV64 -D_DEBUG
CFLAGS += -fno-strict-aliasing -fno-exceptions -fno-stack-protector
CFLAGS += -g
XFLAGS  = $(CFLAGS) -fno-rtti -fno-use-cxa-atexit
G_DBG   = gdb-multiarch
CC      = ${GPREF}gcc -O2
CX      = ${GPREF}g++ -O2

# do in linux, or output win-format

.PHONY: all delall
all: delall $(asmobjs) $(cplobjs) $(cppobjs)
	@echo AR lib$(arch).a
	@${AR} -rcs ${dest_obj}/lib$(arch).a ${dest_obj}/*.o

delall:
	mkdir $(uobjpath)/accm-$(arch) -p
	@-rm ${dest_obj}/lib$(arch).a
	@-rm -rf $(uobjpath)/accm-$(arch)/*

%.o: %.S
	@echo "AS $(<)"
	@${CC} -c ${CFLAGS} $(<) -o $(dest_obj)/$(asmpref)$(notdir $(@))

%.o: %.c
	@echo "CC $(<)"
	@$(CC) $(CFLAGS) -c $< -o $(dest_obj)/$(cplpref)$(notdir $@) ||\
		ret 1 "!! Panic When: $(CC) $(CFLAGS) -c $< -o $(dest_obj)/$(cplpref)$(notdir $@)"
%.o: %.cpp
	@echo "CX $(<)"
	@$(CX) $(XFLAGS) -c $< -o $(dest_obj)/$(cpppref)$(notdir $@) ||\
		ret 1 "!! Panic When: $(CX) $(XFLAGS) -c $< -o $(dest_obj)/$(cplpref)$(notdir $@)"

